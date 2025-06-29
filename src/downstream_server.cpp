#include "downstream_server.h"
#include "itch.h"
#include "mold_udp_64.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <ranges>
#include <map>
#include <unordered_map>
#include <vector>
#include <bits/this_thread_sleep.h>
#include <fstream>
#include <print>

Downstream_Server::Downstream_Server(const std::filesystem::path &itch_file, const std::string &group,
                                     const std::uint16_t port, const std::uint8_t ttl, bool loopback,
                                     const double replay_speed) : sock_{socket(AF_INET, SOCK_DGRAM, 0)},
                                                                  mapped_file_{
                                                                      std::filesystem::file_size(itch_file), PROT_READ,
                                                                      MAP_PRIVATE, jam_utils::FD{itch_file}, 0},
                                                                  replay_speed_{replay_speed}
{
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);

    if (const auto ret{inet_pton(AF_INET, group.c_str(), &addr_.sin_addr)}; ret == 0)
    {
        throw std::invalid_argument(std::format("invalid ip format for downstream group {}", group));
    }
    else if (ret < 0)
    {
        throw std::system_error(errno, std::system_category());
    }

    constexpr int opt{1};
    if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }

    if (setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }

    if (setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }
}

void Downstream_Server::start() const
{
    mold_udp_64::Downstream_Header header{"session001"};
    std::uint64_t mold_seq_num{1};

    std::array<std::byte, mold_udp_64::dgram_max_size> dgram{};
    std::size_t dgram_pos{0};

    std::size_t file_pos{0};

    const auto replay_start = std::chrono::steady_clock::now();
    std::uint64_t first_timestamp{0};

    while (file_pos < mapped_file_.len())
    {
        header.msg_count = 0;
        header.sequence_num = htobe64(mold_seq_num);
        dgram_pos = mold_udp_64::downstream_header_size;

        std::uint64_t dgram_first_timestamp{0};

        while (file_pos < mapped_file_.len())
        {
            if (file_pos + itch::len_prefix_size > mapped_file_.len())
            {
                throw std::runtime_error("unexpected trailing bytes at eof");
            }

            itch::len_prefix_t msg_len;
            std::memcpy(&msg_len, &mapped_file_.addr<std::byte>()[file_pos], itch::len_prefix_size);
            msg_len = ntohs(msg_len);

            const std::size_t total_msg_size{itch::len_prefix_size + msg_len};

            if (file_pos + msg_len > mapped_file_.len())
            {
                throw std::runtime_error("ITCH message has length prefix which exceeds file size");
            }

            if (dgram_pos + total_msg_size > mold_udp_64::dgram_max_size)
            {
                break;
            }

            if (header.msg_count == 0)
            {
                std::memcpy(reinterpret_cast<std::byte *>(&dgram_first_timestamp) + 2,
                            &mapped_file_.addr<std::byte>()[file_pos] + itch::timestamp_offset,
                            itch::timestamp_size);

                dgram_first_timestamp = be64toh(dgram_first_timestamp);

                if (!first_timestamp)
                {
                    first_timestamp = dgram_first_timestamp;
                }
            }

            std::memcpy(&dgram[dgram_pos], &mapped_file_.addr<std::byte>()[file_pos], total_msg_size);

            // increment
            dgram_pos += total_msg_size;
            file_pos += total_msg_size;
            ++header.msg_count;
            ++mold_seq_num;
        }

        if (header.msg_count == 0)
        {
            throw std::runtime_error(
                "singular itch msg too big to fit into datagram; logic error or file issue");
        }

        header.msg_count = htons(header.msg_count);
        std::memcpy(dgram.data(), &header, mold_udp_64::downstream_header_size);

        if (dgram_first_timestamp > first_timestamp)
        {
            const auto delay_ns = static_cast<std::uint64_t>(
                static_cast<double>(dgram_first_timestamp - first_timestamp) / replay_speed_
            );

            const auto target_time = replay_start + std::chrono::nanoseconds(delay_ns);
            std::this_thread::sleep_until(target_time);
        }

        const ssize_t bytes_sent{sendto(sock_.fd(), dgram.data(), dgram_pos, 0,
                                        reinterpret_cast<const sockaddr *>(&addr_), sizeof(addr_))};

        if (bytes_sent < 0)
        {
            std::println(std::cerr, "sendto failed: {}", strerror(errno));
        }
        else if (bytes_sent != static_cast<ssize_t>(dgram_pos))
        {
            std::println(std::cerr, "sendto sent only {} of {} bytes", bytes_sent, dgram_pos);
        }

    }
}