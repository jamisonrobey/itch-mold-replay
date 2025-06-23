#include <CLI/CLI.hpp>
#include <print>

int main(const int argc, char **argv) {
  CLI::App cli{};

  std::filesystem::path itch_file;
  cli.add_option("--itch_file", itch_file,
                 "NASDAQ ITCH 5.0 binary message file")
      ->required()
      ->check(CLI::ExistingFile);

  std::string_view downstream_group{"239.0.0.1"};
  int downstream_port{3000};
  int downstream_ttl{1};

  cli.add_option("--downstream-group", downstream_group,
                 "MoldUDP64 downstream multicast group")
      ->capture_default_str();

  cli.add_option("--downstream-port", downstream_port,
                 "MoldUDP64 downstream multicast port")
      ->check(CLI::Range(1024, 65535))
      ->capture_default_str();

  cli.add_option("--downstream-ttl", downstream_ttl, "Downstream multicast TTL")
      ->check(CLI::Range(1, 255))
      ->capture_default_str();

  std::string_view request_address{"127.0.0.1"};
  int request_port{4000};

  cli.add_option("--request-address", request_address,
                 "MoldUDP64 request server bind address")
      ->capture_default_str();

  cli.add_option("--request-port", request_port,
                 "MoldUDP64 request server port")
      ->check(CLI::Range(1024, 65535))
      ->capture_default_str();

  double replay_speed{1.0};

  cli.add_option("--replay-speed", replay_speed, "Downstream replay speed")
      ->check(CLI::PositiveNumber)
      ->capture_default_str();

  CLI11_PARSE(cli, argc, argv);

  return 0;
}
