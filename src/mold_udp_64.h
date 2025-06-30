#pragma once

#include <cstdint>
#include <string_view>
#include <cstring>
#include <stdexcept>

// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf

namespace mold_udp_64 {
struct __attribute__((__packed__)) Downstream_Header
{
    char session[10]{};
    std::uint64_t sequence_num{};
    std::uint16_t msg_count{};

    explicit Downstream_Header(const std::string_view session_in)
    {
        if (session_in.length() != 10)
        {
            throw std::invalid_argument(
                "mold session must be exactly 10 bytes (excluding null terminator)");
        }
        std::memcpy(this->session, session_in.data(), sizeof(this->session));;
    }
};

constexpr std::size_t downstream_header_size = sizeof(Downstream_Header);
constexpr std::size_t mtu_max_size{1200};
constexpr std::size_t udp_ip_header_size{28};
constexpr std::size_t dgram_max_size{mtu_max_size - udp_ip_header_size};
}