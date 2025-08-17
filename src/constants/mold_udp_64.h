#ifndef MOLD_UDP_64_H
#define MOLD_UDP_64_H

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <array>
#include <cassert>
#include <chrono>

// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf

namespace mold_udp_64
{

constexpr std::size_t mtu_size{1200}; // this is left with a little headroom for example VPN
constexpr std::size_t udp_header_size{28};
constexpr std::size_t max_payload_size{mtu_size - udp_header_size};

constexpr std::size_t session_id_len{10};

struct __attribute__((__packed__)) Downstream_Header
{
    std::array<char, session_id_len> session{};
    std::uint64_t sequence_num{};
    std::uint16_t msg_count{};

    Downstream_Header() = default;

    explicit Downstream_Header(std::string_view session_)
    {
        if (session_.length() != session_id_len)
        {
            throw std::invalid_argument(std::format("session {} has length {} expected {} ", session_, session_.length(), session_id_len));
        }
        assert(session_.length() == session_id_len);
        std::memcpy(&session, session_.data(), session_id_len);
    }
};

struct Response_Context
{
    Downstream_Header header;
    std::array<std::byte, max_payload_size> buff{};
    std::size_t buff_len{};
    std::size_t file_pos{};

    explicit Response_Context(std::string_view session)
        : header{session}
    {
    }
};

using Retransmission_Request = Downstream_Header; // these are actually the same

constexpr std::uint16_t end_of_session_flag{0xFFFF};
constexpr std::chrono::seconds end_of_session_transmission_duration{30};
} // namespace mold_udp_64

#endif
