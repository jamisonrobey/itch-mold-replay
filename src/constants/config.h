#ifndef CONFIG_H
#define CONFIG_H

#include <cstddef>
#include <netinet/in.h>

namespace config
{
constexpr std::size_t msg_buffer_size{1U << 22U};
constexpr int epoll_max_events{1024};
} // namespace config

#endif
