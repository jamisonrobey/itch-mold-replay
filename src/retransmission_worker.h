#pragma once
#include "message_bufffer.h"
#include "mold_udp_64.h"
#include "itch.h"
#include "jamutils/M_Map.h"

#include <print>
#include <iostream>
#include <cstddef>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

template <std::size_t BufferSize, std::size_t MaxEpollEvents>
class Retransmission_Worker
{
public:
    Retransmission_Worker(const std::string_view session,
                          const std::string_view address,
                          const std::uint16_t port,
                          const int shutdown_fd,
                          std::shared_ptr<jam_utils::M_Map> mapped_file,
                          Message_Buffer<BufferSize>& message_buffer)
        : shutdown_fd_{shutdown_fd},
          session_{session},
          mapped_file_{std::move(mapped_file)},
          msg_buf_{message_buffer},
          sock_{jam_utils::FD(socket(AF_INET, SOCK_DGRAM, 0))}
    {
        constexpr int opt{1};
        if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {

            throw std::system_error(errno, std::system_category());
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        const auto ret = inet_pton(AF_INET, address.data(), &addr.sin_addr);
        if (ret == 0)
        {
            throw std::invalid_argument(
                std::format("invalid ip format for request address {}", address));
        }
        if (ret < 0)
        {
            throw std::system_error(errno, std::system_category());
        }

        if (bind(sock_.fd(), reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr)) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        event_.events = EPOLLIN | EPOLLET;
        event_.data.fd = sock_.fd();

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_.fd(), &event_) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }

        event_.data.fd = shutdown_fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, shutdown_fd_, &event_) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }

    }

    void start()
    {
        std::array<std::byte, mold_udp_64::dgram_max_size> response_buff{};
        while (true)
        {
            const int nfds = epoll_wait(epoll_fd_, events_.data(), MaxEpollEvents, -1);
            if (nfds < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::system_error(errno, std::system_category());
            }
            for (std::size_t i = 0; i < static_cast<std::size_t>(nfds); ++i)
            {
                const epoll_event& ev = events_[i];
                const int fd = ev.data.fd;

                if (ev.data.fd == shutdown_fd_)
                {
                    return;
                }

                if (ev.events & EPOLLIN)
                {
                    while (true)
                    {
                        std::array<char, mold_udp_64::request_size> retrans_packet;
                        sockaddr_in client_addr{};
                        socklen_t addr_len{sizeof(client_addr)};

                        const ssize_t bytes_recv{
                            recvfrom(fd, retrans_packet.data(), retrans_packet.size(), 0,
                                     reinterpret_cast<sockaddr*>(&client_addr), &addr_len)};

                        if (bytes_recv < 0)
                        {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                            {
                                break;
                            }
#ifndef  NDEBUG
                            std::print(stderr, "recvfrom error: {}\n", std::strerror(errno));
#endif
                            break;
                        }
                        if (static_cast<std::size_t>(bytes_recv) != retrans_packet.size())
                        {
#ifndef NDEBUG
                            std::print(stderr, "request packet had wrong size\n");
#endif
                            continue;
                        }

                        const std::string_view req_session{retrans_packet.data() + mold_udp_64::request_session_offset,
                                                           mold_udp_64::session_string_size};

                        if (req_session != session_)
                        {
                            continue;
                        }

                        std::uint64_t req_seq_num;
                        std::memcpy(&req_seq_num, retrans_packet.data() + mold_udp_64::request_seq_num_offset,
                                    sizeof(std::uint64_t));
                        req_seq_num = be64toh(req_seq_num);

                        std::uint16_t req_msg_count;
                        std::memcpy(&req_msg_count, retrans_packet.data() + mold_udp_64::request_msg_count_offset,
                                    sizeof(std::uint16_t));
                        req_msg_count = ntohs(req_msg_count);

                        const auto file_pos_opt{msg_buf_.get_file_pos(req_seq_num)};
                        if (!file_pos_opt)
                        {
                            continue;
                        }

                        std::size_t file_pos{*file_pos_opt};

                        mold_udp_64::Downstream_Header header{session_};
                        header.msg_count = 0;
                        header.sequence_num = htobe64(req_seq_num);
                        std::size_t response_buff_pos{mold_udp_64::downstream_header_size};

                        while (response_buff_pos < response_buff.size() && file_pos < mapped_file_->len() &&
                               header.msg_count < req_msg_count)
                        {
                            if (file_pos + itch::len_prefix_size > mapped_file_->len())
                            {
                                break;
                            }
                            std::uint16_t msg_len;
                            std::memcpy(&msg_len, &mapped_file_->addr<std::byte>()[file_pos],
                                        itch::len_prefix_size);
                            msg_len = ntohs(msg_len);

                            const std::size_t total_msg_size{itch::len_prefix_size + msg_len};
                            if (file_pos + total_msg_size > mapped_file_->len())
                            {
                                break;
                            }

                            if (response_buff_pos + total_msg_size > response_buff.size())
                            {
                                break;
                            }

                            std::memcpy(&response_buff[response_buff_pos], &mapped_file_->addr<std::byte>()[file_pos],
                                        total_msg_size);

                            response_buff_pos += total_msg_size;
                            file_pos += total_msg_size;
                            ++header.msg_count;
                        }

                        if (header.msg_count == 0)
                        {
                            continue;
                        }

                        header.msg_count = htons(header.msg_count);
                        std::memcpy(response_buff.data(), &header, mold_udp_64::downstream_header_size);
#ifndef DEBUG_NO_NETWORK
                        ssize_t bytes_sent{sendto(sock_.fd(), response_buff.data(), response_buff_pos, 0,
                                                  reinterpret_cast<sockaddr*>(&client_addr), addr_len)};

                        if (bytes_sent < 0)
                        {
                            std::println(std::cerr, "sendto failed: {}", strerror(errno));
                        }
                        else if (bytes_sent != static_cast<ssize_t>(response_buff_pos))
                        {
                            std::println(std::cerr,
                                         "sendto sent only {} of {} bytes",
                                         bytes_sent,
                                         response_buff_pos);
                        }
#endif
                    }
                }
            }
        }
    }

private:
    const int shutdown_fd_;
    std::string session_;

    std::shared_ptr<jam_utils::M_Map> mapped_file_;
    Message_Buffer<BufferSize>& msg_buf_;

    jam_utils::FD sock_;
    int epoll_fd_;
    epoll_event event_{};
    std::array<epoll_event, MaxEpollEvents> events_;
};