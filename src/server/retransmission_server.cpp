#include "retransmission_server.h"

#include "retransmission_worker.h"

Retransmission_Server::Retransmission_Server(std::string_view session,
                                             std::string_view address,
                                             std::uint16_t port,
                                             jam_utils::M_Map& itch_file,
                                             Message_Buffer& msg_buffer,
                                             std::size_t num_threads)
{
    for (std::size_t i = 0; i < num_threads; ++i)
    {
        worker_threads_.emplace_back([session, address, port, &itch_file, &msg_buffer, this] {
            Retransmission_Worker worker{session,
                                         address,
                                         port,
                                         shutdown_fd_,
                                         itch_file,
                                         msg_buffer};
            worker.start();
        });
    }
}

void Retransmission_Server::stop() const
{
    constexpr std::uint64_t val{1};
    if (write(shutdown_fd_, &val, sizeof(val)) < 0)
    {
        throw std::system_error(errno, std::system_category());
    }
}
