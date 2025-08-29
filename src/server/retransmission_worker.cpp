#include "retransmission_worker.h"
#include "config.h"
#include "itch.h"
#include "mold_udp_64.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstdio>
#include <iostream>
#include <print>

Retransmission_Worker::Retransmission_Worker(std::string_view session,
                                             std::string_view address,
                                             std::uint16_t port,
                                             int shutdown_fd,
                                             jam_utils::M_Map& itch_file,
                                             Message_Buffer& msg_buffer)
    : res_ctx_{session},
      shutdown_fd_{shutdown_fd},
      itch_file_{itch_file},
      msg_buffer_{msg_buffer},
      sock_{socket(AF_INET, SOCK_DGRAM, 0)},
      epoll_fd_{epoll_create1(0)}
{
    constexpr auto opt{1};
    if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0 ||
        setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (const auto ret{inet_pton(AF_INET,
                                 address.data(),
                                 &addr.sin_addr)};
        ret == 0)
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

    event_.events = EPOLLIN | EPOLLET;
    event_.data.fd = sock_.fd();

    if (epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_ADD, sock_.fd(), &event_) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }

    event_.data.fd = shutdown_fd_;

    if (epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_ADD, shutdown_fd_, &event_) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }
}

void Retransmission_Worker::start()
{
    while (true)
    {
        const int nfds{epoll_wait(epoll_fd_.fd(), events_.data(), config::epoll_max_events, -1)};
        if (nfds < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            throw std::system_error(errno, std::system_category());
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(nfds); ++i)
        {
            const epoll_event& ev{events_[i]};
            const int client_fd{ev.data.fd};

            if (client_fd == shutdown_fd_)
            {
                return;
            }

            if ((ev.events & EPOLLIN) != 0)
            {
                while (try_parse_request(client_fd))
                {
                    fill_response_buffer();
                    send_response();
                }
            }
        }
    }
}

bool Retransmission_Worker::try_parse_request(int client_fd)
{
    const ssize_t bytes_recv{
        recvfrom(client_fd,
                 &req_ctx_.request,
                 sizeof(mold_udp_64::Retransmission_Request),
                 0,
                 reinterpret_cast<sockaddr*>(&req_ctx_.client_addr),
                 &addr_len_)};

    if (bytes_recv < 0)
    {
        if (errno == EWOULDBLOCK)
        {
            return false;
        }
        std::perror("recvfrom");
        return false;
    }
    else if (static_cast<std::size_t>(bytes_recv) != sizeof(mold_udp_64::Retransmission_Request))
    {
        return false;
    }

    if (req_ctx_.request.session != res_ctx_.header.session)
    {
        return false;
    }

    req_ctx_.request.msg_count = ntohs(req_ctx_.request.msg_count);

    if (req_ctx_.request.msg_count <= 0)
    {
        return false;
    }

    const auto file_pos{msg_buffer_.get_file_pos(be64toh(req_ctx_.request.sequence_num))};

    if (!file_pos)
    {
        return false;
    }

    res_ctx_.file_pos = *file_pos;
    return true;
}

void Retransmission_Worker::fill_response_buffer()
{
    res_ctx_.header.msg_count = 0;
    res_ctx_.header.sequence_num = req_ctx_.request.sequence_num;

    res_ctx_.buff_len = sizeof(mold_udp_64::Downstream_Header);

    while (res_ctx_.buff_len < mold_udp_64::max_payload_size &&
           res_ctx_.file_pos < itch_file_.len() &&
           res_ctx_.header.msg_count < req_ctx_.request.msg_count)
    {
        if (res_ctx_.file_pos + itch::len_prefix_size > itch_file_.len())
        {
            break;
        }

        std::uint16_t len_prefix;
        std::memcpy(&len_prefix,
                    itch_file_.at(res_ctx_.file_pos),
                    itch::len_prefix_size);
        len_prefix = ntohs(len_prefix);

        const std::size_t total_msg_len{itch::len_prefix_size + len_prefix};

        if (res_ctx_.buff_len + total_msg_len > mold_udp_64::max_payload_size)
        {
            break;
        }

        std::memcpy(&res_ctx_.buff[res_ctx_.buff_len],
                    itch_file_.at(res_ctx_.file_pos),
                    total_msg_len);

        res_ctx_.buff_len += total_msg_len;
        res_ctx_.file_pos += total_msg_len;
        ++res_ctx_.header.msg_count;
    }
}

void Retransmission_Worker::send_response()
{
    res_ctx_.header.msg_count = htons(res_ctx_.header.msg_count);
    std::memcpy(res_ctx_.buff.data(),
                &res_ctx_.header,
                sizeof(mold_udp_64::Downstream_Header));

#ifndef DEBUG_NO_NETWORK
    if (const ssize_t bytes_sent{
            sendto(sock_.fd(),
                   res_ctx_.buff.data(),
                   res_ctx_.buff_len,
                   0,
                   reinterpret_cast<sockaddr*>(&req_ctx_.client_addr),
                   sizeof(req_ctx_.client_addr))};
        bytes_sent < 0)
    {
        std::perror("sendto");
    }
    else if (bytes_sent != static_cast<ssize_t>(res_ctx_.buff_len))
    {
        std::println(std::cerr,
                     "sendto sent only {} of {} bytes",
                     bytes_sent,
                     res_ctx_.buff_len);
    }
#endif
}
