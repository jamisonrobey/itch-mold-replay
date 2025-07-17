#pragma once

#include <chrono>
#include <map>
#include <string>

namespace nasdaq
{
enum class Market_Phase
{
    pre,
    open,
    close
};

constexpr std::chrono::nanoseconds market_phase_to_timestamp(const Market_Phase phase)
{
    using namespace std::chrono_literals;
    switch (phase)
    {
    case Market_Phase::pre:
        return 0ns;
    case Market_Phase::open:
        return 9h + 30min;
    case Market_Phase::close:
        return 16h;
    default:
        return 0ns;
    }
}

// for CLI11 (https://github.com/CLIUtils/CLI11/blob/main/examples/enum.cpp)
const std::map<std::string, Market_Phase> market_phase_map{{"pre", Market_Phase::pre},
                                                           {"open", Market_Phase::open},
                                                           {"close", Market_Phase::close}};
}