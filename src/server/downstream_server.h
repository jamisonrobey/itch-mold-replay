#ifndef DOWNSTREAM_SERVER_H
#define DOWNSTREAM_SERVER_H

#include "message_buffer.h"
#include "mold_udp_64.h"
#include "nasdaq.h"

#include "jamutils/M_Map.h"

#include <chrono>

class Downstream_Server
{
  public:
    Downstream_Server(std::string_view session,
                      std::string_view group,
                      std::uint16_t port,
                      std::uint8_t ttl,
                      bool loopback,
                      double replay_speed,
                      nasdaq::Market_Phase start_phase,
                      jam_utils::M_Map& itch_file,
                      Message_Buffer& msg_buffer);
    void start();

  private:
    void fill_buffer();
    void send_buffer();
    void handle_timing();
    void end_of_session();
    struct Replay_Context
    {
        double speed;
        std::chrono::nanoseconds start_replay_at;
        std::chrono::high_resolution_clock::time_point replay_start_time;
        std::chrono::nanoseconds first_timestamp{};
        std::chrono::nanoseconds current_timestamp{};
        Replay_Context(double speed_, std::chrono::nanoseconds start_replay_at_)
            : speed{speed_},
              start_replay_at{start_replay_at_},
              replay_start_time{
                  std::chrono::high_resolution_clock::now()}
        {
        }
    };

    mold_udp_64::Response_Context res_ctx_;
    Replay_Context replay_ctx_;
    jam_utils::M_Map& itch_file_;
    Message_Buffer& msg_buffer_;
    jam_utils::FD sock_;
    sockaddr_in addr_{};

    std::uint64_t mold_seq_num_{1};
};

#endif