#include <CLI/App.hpp>
#include <print>

#include "downstream_server.h"
#include "nasdaq.h"


int main(const int argc, char **argv)
{
    CLI::App cli{};

    std::filesystem::path itch_file;

    cli.add_option("itch_file",
                   itch_file,
                   "NASDAQ ITCH 5.0 binary message file")
       ->required()
       ->check(CLI::ExistingFile);

    std::string downstream_group{"239.0.0.1"};
    int downstream_port{30000};
    int downstream_ttl{1};
    bool loopback{false};

    cli.add_option("--downstream-group",
                   downstream_group,
                   "Downstream group")
       ->capture_default_str();

    cli.add_option("--downstream-port",
                   downstream_port,
                   "Downstream port")
       ->check(CLI::Range(1025, 65535))
       ->capture_default_str();

    cli.add_option("--ttl", downstream_ttl, "Downstream TTL")
       ->check(CLI::Range(0, 255))
       ->capture_default_str();

    cli.add_flag("--loopback", loopback, "Enable downstream multicast loopback");

    std::string request_address{"127.0.0.1"};
    int request_port{31000};

    cli.add_option("--request-address",
                   request_address,
                   "Request server address")
       ->capture_default_str();

    cli.add_option("--request-port",
                   request_port,
                   "Request server port")
       ->check(CLI::Range(1025, 65535))
       ->capture_default_str();

    double replay_speed{1.0};
    auto start_phase{nasdaq::Market_Phase::pre};

    cli.add_option("--replay-speed", replay_speed, "Downstream replay speed")
       ->check(CLI::PositiveNumber)
       ->capture_default_str();

    cli.add_option("--start-phase",
                   start_phase,
                   "Market phase to start replay (pre, open, close)")
       ->transform(CLI::CheckedTransformer(nasdaq::market_phase_map, CLI::ignore_case))
       ->capture_default_str();

    CLI11_PARSE(cli, argc, argv);

    try
    {
        const Downstream_Server downstream_server{itch_file,
                                                  downstream_group,
                                                  static_cast<std::uint16_t>(downstream_port),
                                                  static_cast<uint8_t>(downstream_ttl),
                                                  loopback,
                                                  start_phase,
                                                  replay_speed};

        downstream_server.start();
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::println(std::cerr, "{}\n", ex.what());
        return -1;
    }
}