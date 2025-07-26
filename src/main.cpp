#include <CLI/App.hpp>

#include <filesystem>
#include <string>
#include <cstdint>
#include <print>

#include "server.h"
#include "nasdaq.h"

int main(const int argc, char** argv)
{
    CLI::App cli{};

    std::string session;
    cli.add_option("session", session, "MoldUDP64 Session")
        ->required()
        ->check([](const std::string& s) {
            if (s.length() != mold_udp_64::session_string_size)
            {
                return "session must be exactly 10 characters";
            }
            return "";
        });

    std::filesystem::path itch_file;
    cli.add_option("itch_file", itch_file, "NASDAQ ITCH 5.0 binary message file")
        ->required()
        ->check(CLI::ExistingFile);

    std::string downstream_group{"239.0.0.1"};
    int downstream_port{30000};
    int downstream_ttl{1};
    bool loopback{false};

    cli.add_option("--downstream-group", downstream_group, "Downstream group")
        ->capture_default_str();

    cli.add_option("--downstream-port", downstream_port, "Downstream port")
        ->check(CLI::Range(1025, 65535))
        ->capture_default_str();

    cli.add_option("--ttl", downstream_ttl, "Downstream TTL")
        ->check(CLI::Range(0, 255))
        ->capture_default_str();

    cli.add_flag("--loopback", loopback, "Enable downstream multicast loopback");

    std::string retrans_address{"127.0.0.1"};
    int retrans_port{31000};

    cli.add_option("--retrans-address", retrans_address, "Retransmission server address")
        ->capture_default_str();

    cli.add_option("--retrans-port", retrans_port, "Retransmission server port")
        ->check(CLI::Range(1025, 65535))
        ->capture_default_str();

    double replay_speed{1.0};
    auto start_phase{nasdaq::Market_Phase::pre};

    cli.add_option("--replay-speed,--speed", replay_speed, "Downstream replay speed")
        ->check(CLI::PositiveNumber)
        ->capture_default_str();

    cli.add_option("--start-phase,--phase,--start",
                   start_phase,
                   "Market phase to start replay (pre, open, close)")
        ->transform(
            CLI::CheckedTransformer(nasdaq::market_phase_map, CLI::ignore_case))
        ->capture_default_str();

    CLI11_PARSE(cli, argc, argv);

    try
    {


       const auto server{std::make_unique<Server<>>(session,
                                                     itch_file,
                                                     downstream_group,
                                                     static_cast<std::uint16_t>(downstream_port),
                                                     static_cast<std::uint8_t>(downstream_ttl),
                                                     loopback,
                                                     retrans_address,
                                                     static_cast<std::uint16_t>(retrans_port),
                                                     replay_speed,
                                                     start_phase)};

        server->start();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::println(std::cerr, "{}\n", ex.what());
        return -1;
    }
}