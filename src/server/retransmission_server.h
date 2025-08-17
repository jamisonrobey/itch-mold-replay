#ifndef RETRANSMISSION_SERVER_H
#define RETRANSMISSION_SERVER_H

#include "message_buffer.h"

#include "jamutils/M_Map.h"

#include <sys/eventfd.h>
#include <vector>
#include <thread>

class Retransmission_Server
{
  public:
    Retransmission_Server(std::string_view session,
                          std::string_view address,
                          std::uint16_t port,
                          jam_utils::M_Map& itch_file,
                          Message_Buffer& msg_buffer,
                          std::size_t num_threads = std::thread::hardware_concurrency() - 1);

    void stop() const;

  private:
    const int shutdown_fd_{eventfd(0, EFD_CLOEXEC)};
    std::vector<std::jthread> worker_threads_;
};

#endif
