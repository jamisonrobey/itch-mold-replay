#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include "config.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <optional>

class Message_Buffer
{
  public:
    void push(std::uint64_t seq, std::size_t pos);

    std::optional<std::size_t> get_file_pos(uint64_t seq);

  private:
    struct Message
    {
        std::uint64_t seq_num;
        std::size_t file_pos;
    };

    std::array<Message, config::msg_buffer_size> buffer_{};
    std::atomic<std::uint64_t> write_seq_{0};
};

#endif
