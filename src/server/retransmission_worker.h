#ifndef RETRANSMISSION_WORKER_H
#define RETRANSMISSION_WORKER_H

#include "message_buffer.h"

#include "config.h"
#include "mold_udp_64.h"

#include <jamutils/M_Map.h>

#include <cstdint>
#include <sys/epoll.h>
#include <netinet/in.h>

class Retransmission_Worker
{
  public:
    Retransmission_Worker(std::string_view session,
                          std::string_view address,
                          std::uint16_t port,
                          int shutdown_fd,
                          jam_utils::M_Map& itch_file,
                          Message_Buffer& msg_buffer);

    void start();

  private:
    bool try_parse_request(int client_fd);

    void fill_response_buffer();

    void send_response();

    mold_udp_64::Response_Context res_ctx_;
    const int shutdown_fd_;
    jam_utils::M_Map& itch_file_;
    Message_Buffer& msg_buffer_;

    jam_utils::FD sock_;
    jam_utils::FD epoll_fd_;
    epoll_event event_{};
    std::array<epoll_event, config::epoll_max_events> events_{};
    sockaddr_in addr_{};
    socklen_t addr_len_{sizeof(addr_)};

    struct Request_Context
    {
        mold_udp_64::Retransmission_Request request;
        sockaddr_in client_addr;
    };
    Request_Context req_ctx_{};
    std::array<std::byte, sizeof(mold_udp_64::Retransmission_Request)> req_buff_{};
};

#endif
