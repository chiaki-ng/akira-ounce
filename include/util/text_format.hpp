#pragma once

#include <format>
#include <string>
#include <string_view>

namespace akira::text {

auto vformat(std::string_view pattern, std::format_args args) -> std::string;

template <typename... Args>
auto format(std::format_string<Args...> pattern, Args&&... args) -> std::string
{
    return akira::text::vformat(pattern.get(), std::make_format_args(args...));
}

}
