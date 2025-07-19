#pragma once

#include <cstdint>
#include <string_view>
#include <cstring>
#include <stdexcept>

// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf

namespace mold_udp_64
{
constexpr std::size_t mtu_size{1200}; // this is left with a little headroom if a VPN is present
constexpr std::size_t udp_header_size{28};
constexpr std::size_t packet_max_size{mtu_size - udp_header_size};

constexpr std::size_t session_string_size{10};

struct __attribute__((__packed__)) Downstream_Header
{
    char session[session_string_size]{};
    std::uint64_t sequence_num{};
    std::uint16_t msg_count{};

    explicit Downstream_Header(const std::string_view session_)
    {
        if (session_.length() != session_string_size)
        {
            throw std::invalid_argument("mold session must be exactly 10 bytes (excluding null terminator)");
        }
        std::memcpy(this->session, session_.data(), sizeof(this->session));
        ;
    }
};

constexpr std::size_t downstream_header_size{sizeof(Downstream_Header)};

constexpr std::size_t request_size{20};
constexpr std::size_t request_session_offset{0};
constexpr std::size_t request_seq_num_offset{session_string_size};
constexpr std::size_t request_msg_count_offset{request_seq_num_offset + sizeof(std::uint64_t)};

constexpr std::uint16_t end_of_session_flag{0xFFF};
constexpr std::size_t end_of_session_time_sec{30};
} // namespace mold_udp_64