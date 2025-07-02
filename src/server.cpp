#include "server.h"
#include <memory>

Server::Server(const std::filesystem::path& itch_file,
               const std::string_view downstream_group,
               const std::uint16_t downstream_port,
               const std::uint8_t downstream_ttl,
               const bool loopback,
               [[maybe_unused]]
               std::string_view request_address,
               [[maybe_unused]]
               std::uint16_t request_port,
               const double replay_speed,
               const nasdaq::Market_Phase start_phase)
  : mapped_file_{std::make_shared<jam_utils::M_Map>(
        std::filesystem::file_size(itch_file),
        PROT_READ,
        MAP_PRIVATE,
        jam_utils::FD{itch_file},
        0)},
    downstream_server_{mapped_file_,
                       downstream_group,
                       downstream_port,
                       downstream_ttl,
                       loopback,
                       replay_speed,
                       start_phase} {}

void Server::start() const {
  downstream_server_.start();
}