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
          sock_{socket(AF_INET, SOCK_DGRAM, 0)},
          header_{session},
          start_time_{nasdaq::market_phase_to_timestamp(start_phase)}
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
        std::uint64_t mold_seq_num{1};

        std::array<std::byte, mold_udp_64::dgram_max_size> dgram{};
        std::size_t dgram_pos{0};

        std::chrono::steady_clock::time_point replay_start;
        std::uint64_t first_timestamp{0};

        const std::size_t file_len{mapped_file_->len()};
        std::size_t file_pos{0};
        while (file_pos < file_len)
        {
            header_.msg_count = 0;
            header_.sequence_num = htobe64(mold_seq_num);
            dgram_pos = mold_udp_64::downstream_header_size;
            std::uint64_t dgram_first_timestamp{0};
            while (file_pos < file_len)
            {
                if (file_pos + itch::len_prefix_size > file_len) [[unlikely]]
                {
                    throw std::runtime_error("unexpected trailing bytes at eof");
                }
                std::uint16_t msg_len;
                std::memcpy(&msg_len, &mapped_file_->addr<std::byte>()[file_pos], itch::len_prefix_size);
                msg_len = ntohs(msg_len);

                const std::size_t total_msg_size{itch::len_prefix_size + msg_len};
                if (file_pos + total_msg_size > file_len) [[unlikely]]
                {
                    throw std::runtime_error("ITCH message length exceeds file size");
                }
                if (dgram_pos + total_msg_size > mold_udp_64::dgram_max_size)
                {
                    break;
                }
                if (header_.msg_count == 0)
                {
                    dgram_first_timestamp = itch::extract_timestamp(&mapped_file_->addr<std::byte>()[file_pos]);
                }

                std::memcpy(&dgram[dgram_pos], &mapped_file_->addr<std::byte>()[file_pos], total_msg_size);
                msg_buf_.push(mold_seq_num, file_pos);

                dgram_pos += total_msg_size;
                file_pos += total_msg_size;
                ++header_.msg_count;
                ++mold_seq_num;
            }
            if (header_.msg_count == 0) [[unlikely]]
            {
                throw std::runtime_error(
                    "singular itch msg too big to fit into datagram; logic error or file issue");
            }
            header_.msg_count = htons(header_.msg_count);
            std::memcpy(dgram.data(), &header_, mold_udp_64::downstream_header_size);
            auto dgram_time{std::chrono::nanoseconds(dgram_first_timestamp)};
            if (dgram_time < start_time_)
            {
                continue;
            }
            if (first_timestamp == 0)
            {
                first_timestamp = dgram_first_timestamp;
                replay_start = std::chrono::steady_clock::now();
            }
            auto elapsed{dgram_time - std::chrono::nanoseconds(first_timestamp)};
            std::chrono::nanoseconds delay{static_cast<std::uint64_t>(static_cast<double>(elapsed.count())
                                                                      / replay_speed_)};
#ifndef DEBUG_NO_SLEEP
            std::this_thread::sleep_until(replay_start + delay);
#endif
#ifndef DEBUG_NO_NETWORK
            ssize_t bytes_sent{
                sendto(sock_.fd(), dgram.data(), dgram_pos, 0, reinterpret_cast<const sockaddr*>(&addr_),
                       sizeof(addr_))};
            if (bytes_sent < 0)
            {
                std::println(std::cerr, "sendto failed: {}", strerror(errno));
            }
            else if (bytes_sent != static_cast<ssize_t>(dgram_pos))
            {
                std::println(std::cerr,
                             "sendto sent only {} of {} bytes",
                             bytes_sent,
                             dgram_pos);
            }
#endif
        }
    }

private:
    std::shared_ptr<jam_utils::M_Map> mapped_file_;
    double replay_speed_{};
    Message_Buffer<BufferSize>& msg_buf_;

    jam_utils::FD sock_;
    sockaddr_in addr_{};
    mold_udp_64::Downstream_Header header_;
    std::chrono::nanoseconds start_time_{};
};