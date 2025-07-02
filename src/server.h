#pragma once

#include <memory>
#include <cstdint>

#include "downstream_server.h"
#include "nasdaq.h"
#include "jamutils/M_Map.h"


class Server {
public:
  Server(const std::filesystem::path& itch_file,
         std::string_view downstream_group,
         std::uint16_t downstream_port,
         std::uint8_t downstream_ttl,
         bool loopback,
         std::string_view request_address,
         std::uint16_t request_port,
         double replay_speed,
         nasdaq::Market_Phase start_phase);

  void start() const;

private:
  std::shared_ptr<jam_utils::M_Map> mapped_file_;
  Downstream_Server downstream_server_;
};