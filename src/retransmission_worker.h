#pragma once
#include "message_bufffer.h"
#include "mold_udp_64.h"
#include "itch.h"
#include "jamutils/M_Map.h"

#include <print>
#include <iostream>
#include <cstddef>
#include <cerrno>
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
        if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0 ||
            setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            throw std::system_error(errno, std::system_category());
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (const auto ret = inet_pton(AF_INET, address.data(), &addr.sin_addr); ret == 0)
        {
            throw std::invalid_argument(std::format("invalid ip format for request address {}", address));
        }
        else if (ret < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        if (bind(sock_.fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
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
        while (true)
        {
            const int nfds{epoll_wait(epoll_fd_, events_.data(), MaxEpollEvents, -1)};
            if (nfds < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::system_error(errno, std::system_category());
            }
            for (int i = 0; i < nfds; ++i)
            {
                const epoll_event& ev{events_[i]};
                const int fd{ev.data.fd};
                if (fd == shutdown_fd_)
                {
                    return;
                }
                if (ev.events & EPOLLIN)
                {
                    while (try_parse_request(fd))
                    {
                        fill_response_buffer();
                        send_response_buffer();
                    }
                }
            }
        }
    }

  private:
    bool try_parse_request(const int fd)
    {
        const ssize_t bytes_recv{recvfrom(fd,
                                          &retrans_req_,
                                          mold_udp_64::retransmission_request_size,
                                          0,
                                          reinterpret_cast<sockaddr*>(&client_addr_),
                                          &addr_len_)};

        if (bytes_recv < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return false;
            }
            std::println(std::cerr, "recvfrom {}", std::strerror(errno));
            return false;
        }
        else if (static_cast<std::size_t>(bytes_recv) != mold_udp_64::retransmission_request_size)
        {
            std::println(std::cerr, "request packet had wrong size");
            return false;
        }

        if (retrans_req_.session != session_)
        {
            return false;
        }

        const auto file_pos{msg_buf_.get_file_pos(be64toh(retrans_req_.sequence_num))};
        if (!file_pos)
        {
            return false;
        }
        file_pos_ = *file_pos;

        retrans_req_.msg_count = ntohs(retrans_req_.msg_count);
        if (retrans_req_.msg_count <= 0)
        {
            return false;
        }

        return true;
    }

    void fill_response_buffer()
    {
        res_header_.msg_count = 0;
        res_header_.sequence_num = retrans_req_.sequence_num;

        while (res_buff_size_ < mold_udp_64::packet_max_size &&
               file_pos_ < mapped_file_->len() &&
               res_header_.msg_count < retrans_req_.msg_count)
        {
            if (file_pos_ + itch::len_prefix_size > mapped_file_->len())
            {
                break;
            }

            std::uint16_t msg_len;
            std::memcpy(&msg_len, &mapped_file_->addr<std::byte>()[file_pos_], itch::len_prefix_size);
            msg_len = ntohs(msg_len);

            const std::size_t total_msg_size{itch::len_prefix_size + msg_len};

            if (res_buff_size_ + total_msg_size > mold_udp_64::packet_max_size)
            {
                break;
            }

            std::memcpy(&res_buff_[res_buff_size_], &mapped_file_->addr<std::byte>()[file_pos_], total_msg_size);

            res_buff_size_ += total_msg_size;
            file_pos_ += total_msg_size;
            ++res_header_.msg_count;
        }
    }

    void send_response_buffer()
    {
        res_header_.msg_count = htons(res_header_.msg_count);
        std::memcpy(res_buff_.data(), &res_header_, mold_udp_64::downstream_header_size);

#ifndef DEBUG_NO_NETWORK
        if (const ssize_t bytes_sent{sendto(sock_.fd(),
                                            res_buff_.data(),
                                            res_buff_size_,
                                            0,
                                            reinterpret_cast<sockaddr*>(&client_addr_),
                                            addr_len_)};
            bytes_sent < 0)
        {
            std::println(std::cerr, "sendto failed: {}", std::strerror(errno));
        }
        else if (bytes_sent != static_cast<ssize_t>(res_buff_size_))
        {
            std::println(std::cerr,
                         "sendto sent only {} of {} bytes",
                         bytes_sent,
                         res_buff_size_);
        }
#endif
    }

    const int shutdown_fd_;
    std::string_view session_;

    std::shared_ptr<jam_utils::M_Map> mapped_file_;
    Message_Buffer<BufferSize>& msg_buf_;

    jam_utils::FD sock_;
    int epoll_fd_;
    epoll_event event_{};
    std::array<epoll_event, MaxEpollEvents> events_;

    struct sockaddr_in client_addr_{};
    socklen_t addr_len_{sizeof(client_addr_)};
    mold_udp_64::Retransmission_Request retrans_req_{};
    std::size_t file_pos_{};

    std::array<std::byte, mold_udp_64::packet_max_size> res_buff_{};
    std::size_t res_buff_size_{};
    mold_udp_64::Downstream_Header res_header_{};
};