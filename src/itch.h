#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf

namespace itch
{
using len_prefix_t = std::uint16_t;
constexpr std::size_t len_prefix_size{sizeof(len_prefix_t)};
constexpr std::size_t timestamp_offset{len_prefix_size +
                                       1 + // message type
                                       2 + // stock locate
                                       2}; // tracking number
constexpr std::size_t timestamp_size{6};
constexpr std::size_t max_msg_len{50};
}