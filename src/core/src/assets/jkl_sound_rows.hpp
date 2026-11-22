#pragma once

#include "sed/text.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace sed::assets::detail
{

// The JK/MotS config reader splits on commas as well as horizontal whitespace
// and interprets a key=value token by its value. Keep the source row separately:
// the presence of a second argument is significant even when its value is 0.
inline std::string_view nextJklSoundArgumentValue(std::string_view& remaining) noexcept
{
    constexpr std::string_view separators = ", \t\r\n";
    const std::size_t begin = remaining.find_first_not_of(separators);
    if ( begin == std::string_view::npos )
    {
        remaining = {};
        return {};
    }
    remaining.remove_prefix(begin);
    const std::size_t end = remaining.find_first_of(separators);
    std::string_view token = remaining.substr(0, end);
    remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end);
    const std::size_t equals = token.find('=');
    if ( equals != std::string_view::npos )
    {
        token.remove_prefix(equals + 1u);
    }
    return token;
}

struct JklSoundLinePiece
{
    std::string_view text{};
    bool skipped{};
    bool continued{};
};

// Native comments are column-one ';'/'#' or an inline '#'. A semicolon inside
// a record is an ordinary argument, and a continuation joins without inserting
// whitespace. Normalize the physical CRLF terminator, not authored row text.
inline JklSoundLinePiece splitJklSoundPhysicalLine(
    std::string_view rawLine, const bool hadNewline = true) noexcept
{
    if ( !rawLine.empty() && rawLine.back() == '\r' )
    {
        rawLine.remove_suffix(1u);
    }
    if ( rawLine.empty() || rawLine.front() == ';' || rawLine.front() == '#' )
    {
        return {.skipped = true};
    }
    const std::size_t comment = rawLine.find('#');
    rawLine = rawLine.substr(0, comment);
    const bool continued = comment == std::string_view::npos && hadNewline
        && !rawLine.empty() && rawLine.back() == '\\';
    if ( continued )
    {
        rawLine.remove_suffix(1u);
    }
    return {.text = rawLine, .continued = continued};
}

inline std::optional<std::uint32_t> parseJklSoundCapacity(std::string_view row) noexcept
{
    if ( !sed::text::iequalsAscii(nextJklSoundArgumentValue(row), "world")
        || !sed::text::iequalsAscii(nextJklSoundArgumentValue(row), "sounds") )
    {
        return std::nullopt;
    }
    std::string_view token = nextJklSoundArgumentValue(row);
    if ( !token.empty() && token.front() == '+' )
    {
        token.remove_prefix(1u);
    }
    if ( token.empty() )
    {
        return std::nullopt;
    }
    std::uint32_t capacity{};
    const auto result = std::from_chars(token.data(), token.data() + token.size(), capacity);
    if ( result.ec != std::errc{} || result.ptr != token.data() + token.size()
        || capacity > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) )
    {
        return std::nullopt;
    }
    return capacity;
}

} // namespace sed::assets::detail
