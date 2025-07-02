#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <endian.h>

// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf

namespace itch {
using len_prefix_t = std::uint16_t;

constexpr std::size_t len_prefix_size{sizeof(len_prefix_t)};
constexpr std::size_t timestamp_offset{len_prefix_size +
                                       1 + // message type
                                       2 + // stock locate
                                       2}; // tracking number
constexpr std::size_t timestamp_size{6};


// pad 6-byte timestamp to uint64_t
inline std::uint64_t extract_timestamp(const std::byte* msg_start) {
  std::uint64_t timestamp{0};
  std::memcpy(reinterpret_cast<std::byte*>(&timestamp) + 2,
              msg_start + timestamp_offset,
              timestamp_size);
  return be64toh(timestamp);
}

constexpr std::size_t max_msg_len{50};
}