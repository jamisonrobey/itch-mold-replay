#pragma once

#include <sys/eventfd.h>

#include "retransmission_worker.h"

template <std::size_t BufferSize, std::size_t MaxEpollEvents>
class Retransmission_Server
{
public:
    Retransmission_Server(const std::string_view session,
                          const std::string_view address,
                          const std::uint16_t port,
                          std::shared_ptr<jam_utils::M_Map> mapped_file,
                          Message_Buffer<BufferSize>& message_buffer,
                          const std::size_t num_threads = std::thread::hardware_concurrency() - 1)
        : session_{session},
          address_{address},
          port_{port},
          mapped_file_{std::move(mapped_file)},
          msg_buf_{message_buffer},
          num_threads_{num_threads} {}

    void start()
    {
        for (unsigned int i = 0; i < num_threads_; ++i)
        {
            worker_threads_.emplace_back([this] {
                Retransmission_Worker<BufferSize, MaxEpollEvents> worker{
                    session_, address_, port_, shutdown_fd_.fd(), mapped_file_, msg_buf_};
                worker.start();
            });
        }
    }

    void stop() const
    {
        constexpr std::uint64_t val{1};
        if (write(shutdown_fd_.fd(), &val, sizeof(val)) < 0)
        {
            std::println(std::cerr, "retransmission server write to shutdown_fd_ failed {}", strerror(errno));
        }
    }

private:
    std::string session_;
    std::string address_;
    std::uint16_t port_;
    std::shared_ptr<jam_utils::M_Map> mapped_file_;
    Message_Buffer<BufferSize>& msg_buf_;
    std::size_t num_threads_;

    jam_utils::FD shutdown_fd_{eventfd(0, EFD_CLOEXEC)};
    std::vector<std::jthread> worker_threads_;
};