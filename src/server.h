#pragma once

#include <memory>
#include <thread>

#include "downstream_server.h"
#include "message_bufffer.h"
#include "retransmission_server.h"
#include "nasdaq.h"
#include "jamutils/M_Map.h"

// note on BufferSize: 65536 elements ~= 1MB stack, increase this if you want; preferring power2 size.
// market open is ~20k msgs/sec, N=65536 ≈ 3s window. Window grows as traffic slows

template <std::size_t BufferSize = 65336, std::size_t MaxEpollEvents = 1024>
class Server
{
public:
    Server(const std::string_view session,
           const std::filesystem::path& itch_file,
           const std::string_view downstream_group,
           const std::uint16_t downstream_port,
           const std::uint8_t downstream_ttl,
           const bool loopback,
           std::string_view request_address,
           std::uint16_t request_port,
           const double replay_speed,
           const nasdaq::Market_Phase start_phase)
        : mapped_file_{
              std::make_shared<jam_utils::M_Map>(
                  std::filesystem::file_size(itch_file), PROT_READ, MAP_PRIVATE | MAP_POPULATE,
                  jam_utils::FD{itch_file}, 0)},
          request_server_{session, request_address, request_port, mapped_file_, msg_buf_},
          downstream_server_{session, downstream_group, downstream_port, downstream_ttl, loopback, replay_speed,
                             start_phase,
                             mapped_file_, msg_buf_} {}

    void start()
    {
        request_thread_ = std::jthread{[this] { request_server_.start(); }};
        downstream_thread_ = std::thread{[this] { downstream_server_.start(); }};
        downstream_thread_.join();
        request_server_.stop();
    }

private:
    std::shared_ptr<jam_utils::M_Map> mapped_file_;

    Message_Buffer<BufferSize> msg_buf_{};
    Retransmission_Server<BufferSize, MaxEpollEvents> request_server_;
    std::jthread request_thread_;
    Downstream_Server<BufferSize> downstream_server_;
    std::thread downstream_thread_;
};