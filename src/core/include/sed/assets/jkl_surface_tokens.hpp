#pragma once

#include "sed/text.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace sed::assets
{

// JKL admits "vertex,uv", "vertex, uv" and "vertex uv". Advance only after
// both indices parse, leaving all following lighting tokens exactly as written.
inline std::optional<std::pair<std::int32_t, std::int32_t>> readJklSurfaceVertexPair(
    const std::span<const std::string_view> tokens, std::size_t& cursor)
{
    if ( cursor >= tokens.size() )
    {
        return std::nullopt;
    }
    std::size_t next = cursor;
    const auto token = tokens[next++];
    const auto comma = token.find(',');
    const auto vertex = text::parseInt<std::int32_t>(token.substr(0, comma));
    auto uvText = comma == std::string_view::npos ? std::string_view{} : token.substr(comma + 1);
    if ( uvText.empty() && next < tokens.size() )
    {
        uvText = tokens[next++];
    }
    if ( !vertex || uvText.empty() )
    {
        return std::nullopt;
    }
    const auto uv = text::parseInt<std::int32_t>(uvText);
    if ( !uv )
    {
        return std::nullopt;
    }
    cursor = next;
    return std::pair{ *vertex, *uv };
}

} // namespace sed::assets
