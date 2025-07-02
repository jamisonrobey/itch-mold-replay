#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <format>
#include <iostream>
#include <endian.h>

#include "downstream_server.h"
#include "itch.h"
#include "mold_udp_64.h"

Downstream_Server::Downstream_Server(std::shared_ptr<jam_utils::M_Map> mapped_file,
                                     std::string_view group,
                                     const std::uint16_t port,
                                     const std::uint8_t ttl,
                                     const bool loopback,
                                     const double replay_speed,
                                     const nasdaq::Market_Phase start_phase)
  : sock_{socket(AF_INET, SOCK_DGRAM, 0)},
    mapped_file_{std::move(mapped_file)},
    replay_speed_{replay_speed},
    start_time_{nasdaq::market_phase_to_timestamp(start_phase)} {
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port);

  const auto ret = inet_pton(AF_INET, group.data(), &addr_.sin_addr);
  if (ret == 0) {
    throw std::invalid_argument(
        std::format("invalid ip format for downstream group {}", group));
  }
  if (ret < 0) {
    throw std::system_error(errno, std::system_category());
  }

  constexpr int opt = 1;
  if (setsockopt(sock_.fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ||
      setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0 ||
      setsockopt(sock_.fd(), IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0) {
    throw std::system_error(errno, std::system_category());
  }
}

void Downstream_Server::start() const {
  mold_udp_64::Downstream_Header header{"session001"};
  std::uint64_t mold_seq_num = 1;

  std::array<std::byte, mold_udp_64::dgram_max_size> dgram{};
  std::size_t dgram_pos = 0;

  std::chrono::steady_clock::time_point replay_start;
  std::uint64_t first_timestamp = 0;

  std::size_t file_pos = 0;

  while (file_pos < mapped_file_->len()) {
    header.msg_count = 0;
    header.sequence_num = htobe64(mold_seq_num);
    dgram_pos = mold_udp_64::downstream_header_size;

    std::uint64_t dgram_first_timestamp = 0;

    while (file_pos < mapped_file_->len()) {
      if (file_pos + itch::len_prefix_size > mapped_file_->len()) {
        throw std::runtime_error("unexpected trailing bytes at eof");
      }

      itch::len_prefix_t msg_len;
      std::memcpy(&msg_len, &mapped_file_->addr<std::byte>()[file_pos], itch::len_prefix_size);
      msg_len = ntohs(msg_len);

      std::size_t total_msg_size = itch::len_prefix_size + msg_len;
      if (file_pos + msg_len > mapped_file_->len()) {
        throw std::runtime_error("ITCH message length exceeds file size");
      }

      if (dgram_pos + total_msg_size > mold_udp_64::dgram_max_size) {
        break;
      }

      if (header.msg_count == 0) {
        dgram_first_timestamp =
            itch::extract_timestamp(&mapped_file_->addr<std::byte>()[file_pos]);
      }

      std::memcpy(&dgram[dgram_pos],
                  &mapped_file_->addr<std::byte>()[file_pos],
                  total_msg_size);

      dgram_pos += total_msg_size;
      file_pos += total_msg_size;
      ++header.msg_count;
      ++mold_seq_num;
    }

    if (header.msg_count == 0) {
      throw std::runtime_error(
          "singular itch msg too big to fit into datagram; logic error or file issue");
    }

    header.msg_count = htons(header.msg_count);
    std::memcpy(dgram.data(), &header, mold_udp_64::downstream_header_size);

    auto dgram_time = std::chrono::nanoseconds(dgram_first_timestamp);
    if (dgram_time < start_time_) {
      continue;
    }

    if (first_timestamp == 0) {
      first_timestamp = dgram_first_timestamp;
      replay_start = std::chrono::steady_clock::now();
    }

    auto first_time = std::chrono::nanoseconds(first_timestamp);
    auto elapsed = dgram_time - first_time;
    auto delay = std::chrono::nanoseconds(
        static_cast<std::uint64_t>(static_cast<double>(elapsed.count()) / replay_speed_));

    std::this_thread::sleep_until(replay_start + delay);

    ssize_t bytes_sent = sendto(sock_.fd(),
                                dgram.data(),
                                dgram_pos,
                                0,
                                reinterpret_cast<const sockaddr*>(&addr_),
                                sizeof(addr_));

    if (bytes_sent < 0) {
      std::println(std::cerr, "sendto failed: {}", strerror(errno));
    } else if (bytes_sent != static_cast<ssize_t>(dgram_pos)) {
      std::println(std::cerr,
                   "sendto sent only {} of {} bytes",
                   bytes_sent,
                   dgram_pos);
    }
  }
}