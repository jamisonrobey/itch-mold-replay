#pragma once

#include <jamutils/FD.h>
#include <jamutils/M_Map.h>
#include <filesystem>
#include <netinet/in.h>

class Downstream_Server
{
public:
    Downstream_Server(const std::filesystem::path &itch_file,
                      const std::string &group, std::uint16_t port,
                      std::uint8_t ttl,
                      bool loopback,
                      double replay_speed);

    void start() const;

private:
    jam_utils::FD sock_;
    sockaddr_in addr_{};
    jam_utils::M_Map mapped_file_;
    double replay_speed_{};

};