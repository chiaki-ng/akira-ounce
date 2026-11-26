#include "util/text_format.hpp"

namespace akira::text {

auto vformat(std::string_view pattern, std::format_args args) -> std::string
{
    return std::vformat(pattern, args);
}

}
