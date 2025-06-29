#include <CLI/App.hpp>
#include <print>

#include "downstream_server.h"


int main(const int argc, char **argv)
{
    CLI::App cli{};

    std::filesystem::path itch_file;

    cli.add_option("itch_file", itch_file,
                   "NASDAQ ITCH 5.0 binary message file")
       ->required()
       ->check(CLI::ExistingFile);

    std::string downstream_group{"239.0.0.1"};
    int downstream_port{30000};
    int downstream_ttl{1};
    bool loopback{false};

    cli.add_option("--downstream-group", downstream_group,
                   "MoldUDP64 downstream multicast group")
       ->capture_default_str();

    cli.add_option("--downstream-port", downstream_port,
                   "MoldUDP64 downstream multicast port")
       ->check(CLI::Range(1025, 65535))
       ->capture_default_str();

    cli.add_option("--downstream-ttl", downstream_ttl, "Downstream multicast TTL")
       ->check(CLI::Range(0, 255))
       ->capture_default_str();

    cli.add_flag("--loopback", loopback, "Enable downstream multicast loopback");

    std::string request_address{"127.0.0.1"};
    int request_port{31000};

    cli.add_option("--request-address", request_address,
                   "MoldUDP64 request server bind address")
       ->capture_default_str();

    cli.add_option("--request-port", request_port,
                   "MoldUDP64 request server port")
       ->check(CLI::Range(1025, 65535))
       ->capture_default_str();

    double replay_speed{1.0};

    cli.add_option("--replay-speed", replay_speed, "Downstream replay speed")
       ->check(CLI::PositiveNumber)
       ->capture_default_str();

    CLI11_PARSE(cli, argc, argv);

    try
    {
        const Downstream_Server downstream_server{itch_file, downstream_group,
                                                  static_cast<std::uint16_t>(downstream_port),
                                                  static_cast<uint8_t>(downstream_ttl),
                                                  loopback,
                                                  replay_speed};

        downstream_server.start();
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::print(stderr, "{}\n", ex.what());
        return -1;
    }
}