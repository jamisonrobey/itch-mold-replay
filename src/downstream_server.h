#pragma once

#include <jamutils/FD.h>
#include <jamutils/M_Map.h>
#include <filesystem>
#include <netinet/in.h>

#include "nasdaq.h"

class Downstream_Server {
public:
  Downstream_Server(std::shared_ptr<jam_utils::M_Map> mapped_file,
                    std::string_view group,
                    std::uint16_t port,
                    std::uint8_t ttl,
                    bool loopback,
                    double replay_speed,
                    nasdaq::Market_Phase start_phase);

  void start() const;

private:
  jam_utils::FD sock_;
  sockaddr_in addr_{};
  std::shared_ptr<jam_utils::M_Map> mapped_file_;
  double replay_speed_{};
  std::chrono::nanoseconds start_time_{};
};