#pragma once

#include <jamutils/FD.h>
#include <jamutils/M_Map.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <thread>
#include <print>
#include <iostream>
#include <system_error>
#include <stdexcept>
#include <format>
#include <cstring>
#include <array>
#include <chrono>

#include "message_bufffer.h"
#include "nasdaq.h"
#include "mold_udp_64.h"
#include "itch.h"

template <std::size_t BufferSize>
class Downstream_Server
{
  public:
    Downstream_Server(const std::string_view session,
                      const std::string_view group,
                      const std::uint16_t port,
                      const std::uint8_t ttl,
                      const bool loopback,
                      const double replay_speed,
                      const nasdaq::Market_Phase start_phase,
                      std::shared_ptr<jam_utils::M_Map> mapped_file,
                      Message_Buffer<BufferSize>& message_buffer)
        : mapped_file_{std::move(mapped_file)}, msg_buf_{message_buffer},
          replay_ctx_{.speed{replay_speed}, .skip_before{nasdaq::market_phase_to_timestamp(start_phase)}},
          sock_{socket(AF_INET, SOCK_DGRAM, 0)},
          header_{session}
    {
        constexpr auto opt{1};
        if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ||
            setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0 ||
            setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }

        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port);
        if (const auto ret{inet_pton(AF_INET, group.data(), &addr_.sin_addr)}; ret == 0)
        {
            throw std::invalid_argument(std::format("invalid ip format for downstream group {}", group));
        }
        else if (ret < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
    }

    void start()
    {
        while (file_pos_ < file_len_)
        {
            fill_feed_buffer();
            if (replay_ctx_.current_timestamp < replay_ctx_.skip_before)
            {
                continue;
            }
            handle_replay_timing();
            send_feed_buffer();
        }
        end_of_session();
    }

  private:
    void fill_feed_buffer()
    {
        header_.msg_count = 0;
        header_.sequence_num = htobe64(mold_seq_num_);
        feed_buff_size_ = mold_udp_64::downstream_header_size;

        while (file_pos_ < file_len_)
        {
            if (file_pos_ + itch::len_prefix_size > file_len_) [[unlikely]]
            {
                throw std::runtime_error("unexpected trailing bytes at eof");
            }

            std::uint16_t msg_len;
            std::memcpy(&msg_len, &mapped_file_->addr<std::byte>()[file_pos_], itch::len_prefix_size);
            msg_len = ntohs(msg_len);

            const std::size_t total_msg_size{itch::len_prefix_size + msg_len};

            if (file_pos_ + total_msg_size > file_len_) [[unlikely]]
            {
                throw std::runtime_error("ITCH message length exceeds file size");
            }
            if (feed_buff_size_ + total_msg_size > mold_udp_64::packet_max_size)
            {
                break; // done
            }

            if (header_.msg_count == 0)
            {
                replay_ctx_.current_timestamp =
                    std::chrono::nanoseconds{itch::extract_timestamp(&mapped_file_->addr<std::byte>()[file_pos_])};
            }

            std::memcpy(&feed_buff_[feed_buff_size_], &mapped_file_->addr<std::byte>()[file_pos_], total_msg_size);
            msg_buf_.push(mold_seq_num_, file_pos_);

            feed_buff_size_ += total_msg_size;
            file_pos_ += total_msg_size;
            ++header_.msg_count;
            ++mold_seq_num_;
        }
    }

    void send_feed_buffer()
    {
        header_.msg_count = htons(header_.msg_count);
        std::memcpy(feed_buff_.data(), &header_, mold_udp_64::downstream_header_size);
#ifndef DEBUG_NO_NETWORK
        if (const ssize_t bytes_sent{sendto(sock_.fd(),
                                            feed_buff_.data(),
                                            feed_buff_size_,
                                            0,
                                            reinterpret_cast<const sockaddr*>(&addr_),
                                            sizeof(addr_))};
            bytes_sent < 0)
        {
            std::println(std::cerr, "downstream sendto failed {}", std::strerror(errno));
        }
        else if (bytes_sent != static_cast<ssize_t>(feed_buff_size_))
        {
            std::println(std::cerr, "downstream sendto sent only {} of {} bytes", bytes_sent, feed_buff_size_);
        }
#endif
    }

    void handle_replay_timing()
    {
        if (!replay_ctx_.first_timestamp.count())
        {
            replay_ctx_.start_time = std::chrono::high_resolution_clock::now();
            replay_ctx_.first_timestamp = replay_ctx_.current_timestamp;
        }
        const auto since_last_send{replay_ctx_.current_timestamp - replay_ctx_.first_timestamp};
        const auto delay = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(since_last_send) / replay_ctx_.speed);
#ifndef DEBUG_NO_SLEEP
        std::this_thread::sleep_until(replay_ctx_.start_time + delay);
#endif
    }

    void end_of_session()
    {
#if !defined(DEBUG_NO_NETWORK) && !defined(DEBUG_NO_SLEEP)
        header_.msg_count = mold_udp_64::end_of_session_flag;
        header_.sequence_num = htobe64(mold_seq_num_);
        std::array<std::byte, mold_udp_64::downstream_header_size> end_session_buff{};
        std::memcpy(end_session_buff.data(), &header_, mold_udp_64::downstream_header_size);

        const auto end_session_start{std::chrono::high_resolution_clock::now()};
        auto next_send{end_session_start};
        for (std::size_t i = 0; i < mold_udp_64::end_of_session_time_sec; ++i)
        {
            next_send += std::chrono::seconds(1);
            std::this_thread::sleep_until(next_send);
            if (const ssize_t bytes_sent{sendto(sock_.fd(),
                                                end_session_buff.data(),
                                                mold_udp_64::downstream_header_size,
                                                0,
                                                reinterpret_cast<const sockaddr*>(&addr_),
                                                sizeof(addr_))};
                bytes_sent < 0)
            {
                std::println(std::cerr, "end of session sendto failed {}", std::strerror(errno));
            }
        }
#endif
    }

    std::shared_ptr<jam_utils::M_Map> mapped_file_;
    Message_Buffer<BufferSize>& msg_buf_;
    struct Replay_Context
    {
        double speed{};
        std::chrono::nanoseconds skip_before{};
        std::chrono::high_resolution_clock::time_point start_time;
        std::chrono::nanoseconds first_timestamp{};
        std::chrono::nanoseconds current_timestamp{};
    };
    Replay_Context replay_ctx_{};

    jam_utils::FD sock_;
    sockaddr_in addr_{};

    std::array<std::byte, mold_udp_64::packet_max_size> feed_buff_{};
    std::size_t feed_buff_size_{};

    mold_udp_64::Downstream_Header header_;
    std::uint64_t mold_seq_num_{1};

    std::size_t file_len_{mapped_file_->len()};
    std::size_t file_pos_{};
};
