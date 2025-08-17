#include "downstream_server.h"
#include "itch.h"
#include "mold_udp_64.h"
#include "nasdaq.h"
#include <arpa/inet.h>
#include <chrono>
#include <stdexcept>
#include <sys/socket.h>
#include <iostream>
#include <thread>
#include <ranges>

Downstream_Server::Downstream_Server(std::string_view session,
                                     std::string_view group,
                                     std::uint16_t port,
                                     std::uint8_t ttl,
                                     bool loopback,
                                     double replay_speed,
                                     nasdaq::Market_Phase start_phase,
                                     jam_utils::M_Map& itch_file,
                                     Message_Buffer& msg_buffer)
    : res_ctx_{session},
      replay_ctx_{replay_speed, nasdaq::market_phase_to_timestamp(start_phase)},
      itch_file_{itch_file},
      msg_buffer_{msg_buffer},
      sock_{socket(AF_INET, SOCK_DGRAM, 0)}
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

void Downstream_Server::start()
{
    while (res_ctx_.file_pos < itch_file_.len())
    {
        fill_buffer();
        if (replay_ctx_.current_timestamp < replay_ctx_.start_replay_at)
        {
            continue;
        }
        handle_timing();
        send_buffer();
    }
    end_of_session();
}

void Downstream_Server::fill_buffer()
{
    res_ctx_.header.msg_count = 0;
    res_ctx_.header.sequence_num = htobe64(mold_seq_num_);

    res_ctx_.buff_len = sizeof(mold_udp_64::Downstream_Header);

    while (res_ctx_.file_pos < itch_file_.len())
    {
        if (res_ctx_.file_pos + itch::len_prefix_size > itch_file_.len())
        {
            throw std::runtime_error("unexpected trailing bytes at eof");
        }

        std::uint16_t len_prefix;
        std::memcpy(&len_prefix, itch_file_.at(res_ctx_.file_pos), itch::len_prefix_size);
        len_prefix = ntohs(len_prefix);

        const std::size_t total_msg_len{itch::len_prefix_size + len_prefix};

        if (res_ctx_.file_pos + total_msg_len > itch_file_.len())
        {
            throw std::runtime_error("ITCH message exceeds file size");
        }

        if (res_ctx_.buff_len + total_msg_len > res_ctx_.buff.size())
        {
            break;
        }

        if (res_ctx_.header.msg_count == 0)
        {
            replay_ctx_.current_timestamp = std::chrono::nanoseconds{
                itch::extract_timestamp(itch_file_.at(res_ctx_.file_pos))};
        }

        std::memcpy(&res_ctx_.buff[res_ctx_.buff_len], itch_file_.at(res_ctx_.file_pos), total_msg_len);

        msg_buffer_.push(mold_seq_num_, res_ctx_.file_pos);

        res_ctx_.buff_len += total_msg_len;
        res_ctx_.file_pos += total_msg_len;
        ++res_ctx_.header.msg_count;
        ++mold_seq_num_;
    }
}

void Downstream_Server::send_buffer()
{
    res_ctx_.header.msg_count = htons(res_ctx_.header.msg_count);
    std::memcpy(res_ctx_.buff.data(), &res_ctx_.header, sizeof(mold_udp_64::Downstream_Header));
#ifndef DEBUG_NO_NETWORK
    if (const ssize_t bytes_sent{sendto(sock_.fd(),
                                        res_ctx_.buff.data(),
                                        res_ctx_.buff_len,
                                        0,
                                        reinterpret_cast<const sockaddr*>(&addr_),
                                        sizeof(addr_))};
        bytes_sent < 0)
    {
        std::perror("sendto");
    }
    else if (bytes_sent != static_cast<ssize_t>(res_ctx_.buff_len))
    {
        // if this is happening try reducing MTU in constants/mold_udp_64.h
        std::println(std::cerr, "sendto sent only {} of {} bytes", bytes_sent, res_ctx_.buff_len);
    }
#endif
}

void Downstream_Server::handle_timing()
{
    if (replay_ctx_.first_timestamp.count() == 0)
    {
        replay_ctx_.first_timestamp = replay_ctx_.current_timestamp;
        return;
    }

    const std::chrono::nanoseconds elapsed{replay_ctx_.current_timestamp - replay_ctx_.first_timestamp};
    const auto delay{replay_ctx_.replay_start_time + (elapsed / replay_ctx_.speed)};

#ifndef DEBUG_NO_SLEEP
    std::this_thread::sleep_until(delay);
#endif
}

void Downstream_Server::end_of_session()
{
#if !(defined(DEBUG_NO_NETWORK) || defined(DEBUG_NO_SLEEP))
    res_ctx_.header.msg_count = mold_udp_64::end_of_session_flag;
    res_ctx_.header.sequence_num = htobe64(mold_seq_num_);

    std::array<std::byte, sizeof(mold_udp_64::Downstream_Header)> end_session_buff{};
    std::memcpy(end_session_buff.data(), &res_ctx_.header, sizeof(mold_udp_64::Downstream_Header));

    const auto start_time{std::chrono::high_resolution_clock::now()};
    for (auto second : std::views::iota(0z, mold_udp_64::end_of_session_transmission_duration.count()))
    {
        const auto send_time{start_time + std::chrono::seconds(second)};
        std::this_thread::sleep_until(send_time);
        if (const ssize_t bytes_sent{sendto(sock_.fd(),
                                            end_session_buff.data(),
                                            sizeof(mold_udp_64::Downstream_Header),
                                            0,
                                            reinterpret_cast<const sockaddr*>(&addr_),
                                            sizeof(addr_))};
            bytes_sent < 0)
        {
            std::perror("sendto");
        }
    }
#endif
}