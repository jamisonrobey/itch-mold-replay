#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <expected>

template <std::size_t N>
class Message_Buffer
{
public:
    void push(const std::uint64_t seq, const std::size_t pos)
    {
        const auto idx = seq % N;
        buffer_[idx].seq_num = seq;
        buffer_[idx].file_pos = pos;
        write_seq_.store(seq, std::memory_order_release);
    }


    std::optional<std::size_t> get_file_pos(std::uint64_t seq)
    {
        const auto current_seq = write_seq_.load(std::memory_order_acquire);
        if (seq + N <= current_seq)
        {
            return std::nullopt;
        }
        if (seq > current_seq)
        {
            return std::nullopt;
        }
        const auto& entry = buffer_[seq % N];
        if (entry.seq_num != seq)
        {
            return std::nullopt;
        }
        return entry.file_pos;
    }

private:
    struct Message
    {
        std::uint64_t seq_num;
        std::size_t file_pos;
    };

    std::array<Message, N> buffer_{};
    std::atomic<std::uint64_t> write_seq_{0};
};