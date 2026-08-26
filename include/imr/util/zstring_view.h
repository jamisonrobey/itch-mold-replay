#pragma once

#include <string_view>
#include <string>

namespace imr::util
{
    /// A wrapper around `std::string_view` that guarantees null terminated strings for passing to C APIs.
    class zstring_view
    {
      public:
        zstring_view() = default;
        constexpr zstring_view(const char* str) noexcept : sv_(str) {}
        zstring_view(const std::string& str) noexcept : sv_(str) {}

        zstring_view(std::string_view) = delete;
        zstring_view(std::string&&) = delete;

        constexpr const char* c_str() const noexcept { return sv_.data(); }
        constexpr std::string_view sv() const noexcept { return sv_; }
        constexpr const char* data() const noexcept { return sv_.data(); }
        constexpr std::size_t size() const noexcept { return sv_.size(); }
        constexpr bool empty() const noexcept { return sv_.empty(); }
        constexpr operator std::string_view() const noexcept { return sv_; }

      private:
        std::string_view sv_;
    };
}
