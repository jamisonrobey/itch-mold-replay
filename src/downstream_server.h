#pragma once

#include <jamutils/FD.h>
#include <jamutils/M_Map.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <thread>
#include <print>   // For std::println
#include <iostream> // For std::cerr
#include <system_error> // For std::system_error
#include <stdexcept>    // For std::runtime_error etc.
#include <format>       // For std::format
#include <cstring>      // For strerror, memcpy
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
        : mapped_file_{std::move(mapped_file)},
          replay_speed_{replay_speed},
          msg_buf_{message_buffer},
          start_time_{nasdaq::market_phase_to_timestamp(start_phase)},
          sock_{socket(AF_INET, SOCK_DGRAM, 0)},
          header_{session}
    {
        constexpr int opt = 1;
        if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ||
            setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0 ||
            setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port);
        const auto ret = inet_pton(AF_INET, group.data(), &addr_.sin_addr);
        if (ret == 0)
        {
            throw std::invalid_argument(
                std::format("invalid ip format for downstream group {}", group));
        }
        if (ret < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
    }

    void start()
    {
        std::chrono::high_resolution_clock::time_point replay_start;
        std::uint64_t first_timestamp{};

        while (file_pos_ < file_len_)
        {
            header_.msg_count = 0;
            header_.sequence_num = htobe64(mold_seq_num_);
            dgram_pos_ = mold_udp_64::downstream_header_size;

            fill_datagram();

            const auto dgram_time{std::chrono::nanoseconds(dgram_first_timestamp_)};
            if (dgram_time < start_time_)
            {
                continue;
            }

            if (first_timestamp == 0)
            {
                first_timestamp = dgram_first_timestamp_;
                replay_start = std::chrono::high_resolution_clock::now();
            }

            auto elapsed{dgram_time - std::chrono::nanoseconds(first_timestamp)};
            std::chrono::nanoseconds delay{static_cast<uint64_t>(static_cast<double>(elapsed.count()) / replay_speed_)};

#ifndef DEBUG_NO_SLEEP
            std::this_thread::sleep_until(replay_start + delay);
#endif

#ifndef DEBUG_NO_NETWORK
            ssize_t bytes_sent{
                sendto(sock_.fd(), dgram_.data(), dgram_pos_, 0, reinterpret_cast<const sockaddr*>(&addr_),
                       sizeof(addr_))};
            if (bytes_sent < 0)
            {
                std::println(std::cerr, "sendto failed: {}", strerror(errno));
            }
            else if (bytes_sent != static_cast<ssize_t>(dgram_pos_))
            {
                std::println(std::cerr,
                             "sendto sent only {} of {} bytes",
                             bytes_sent,
                             dgram_pos_);
            }
#endif
        }
    }

    void fill_datagram()
    {
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

            if (dgram_pos_ + total_msg_size > mold_udp_64::dgram_max_size)
            {
                break; // done
            }

            if (header_.msg_count == 0)
            {
                dgram_first_timestamp_ = itch::extract_timestamp(&mapped_file_->addr<std::byte>()[file_pos_]);
            }

            std::memcpy(&dgram_[dgram_pos_], &mapped_file_->addr<std::byte>()[file_pos_], total_msg_size);
            msg_buf_.push(mold_seq_num_, file_pos_);

            dgram_pos_ += total_msg_size;
            file_pos_ += total_msg_size;
            ++header_.msg_count;
            ++mold_seq_num_;
        }

        header_.msg_count = htons(header_.msg_count);
        std::memcpy(dgram_.data(), &header_, mold_udp_64::downstream_header_size);
    }


    std::shared_ptr<jam_utils::M_Map> mapped_file_;
    double replay_speed_{};
    Message_Buffer<BufferSize>& msg_buf_;
    std::chrono::nanoseconds start_time_{};

    jam_utils::FD sock_;
    sockaddr_in addr_{};
    mold_udp_64::Downstream_Header header_;

    std::array<std::byte, mold_udp_64::dgram_max_size> dgram_{};
    std::uint64_t mold_seq_num_{1};
    std::size_t file_pos_{};
    std::size_t file_len_{mapped_file_->len()};
    std::size_t dgram_pos_{};
    std::uint64_t dgram_first_timestamp_{};
};