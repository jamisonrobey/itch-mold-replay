#include "config.h"
#include "message_buffer.h"

void Message_Buffer::push(std::uint64_t seq, std::size_t pos)
{
    const auto idx{seq % config::msg_buffer_size};

    buffer_[idx].seq_num = seq;
    buffer_[idx].file_pos = pos;

    write_seq_.store(seq, std::memory_order_release);
}

std::optional<std::size_t> Message_Buffer::get_file_pos(std::uint64_t seq)
{
    const auto current_seq{write_seq_.load(std::memory_order_acquire)};

    if (seq + config::msg_buffer_size <= current_seq || seq > current_seq)
    {
        return std::nullopt;
    }

    const auto& entry{buffer_[seq % config::msg_buffer_size]};

    if (entry.seq_num != seq)
    {
        return std::nullopt;
    }

    return entry.file_pos;
}
