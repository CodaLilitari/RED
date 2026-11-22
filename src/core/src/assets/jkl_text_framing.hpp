#pragma once

#include "jkl_dialect.hpp"

#include <optional>
#include <stdexcept>
#include <string_view>

namespace sed::assets::detail
{

inline constexpr std::string_view kJklSectionPrefix = "SECTION:";

struct JklLinePiece
{
    std::string_view text{};
    bool skipped{};
    bool continued{};
    bool trimPrevious{};
};

// The native test sees the entire buffer after inline comment removal. For an
// ordinary LF-terminated line the byte after the slash is its newline. For an
// inline comment or physical EOF it can be an authored character instead.
inline JklLinePiece splitJklPhysicalLine(
    std::string_view line, const bool hadNewline, const bool previousSlash) noexcept
{
    if ( line.empty() || line.front() == '\r' || line.front() == ';' || line.front() == '#' )
    {
        return {.skipped = true};
    }
    if ( hadNewline && line.back() == '\r' )
    {
        line.remove_suffix(1);
    }
    const auto comment = line.find('#');
    if ( comment == std::string_view::npos && hadNewline )
    {
        if ( line.ends_with('\\') )
        {
            return {.text = line.substr(0, line.size() - 1), .continued = true};
        }
    }
    else
    {
        line = line.substr(0, comment);
        if ( line.size() >= 2 && line[line.size() - 2] == '\\' )
        {
            return {.text = line.substr(0, line.size() - 2), .continued = true};
        }
        if ( line.size() == 1 && previousSlash )
        {
            return {.continued = true, .trimPrevious = true};
        }
    }
    if ( line.ends_with('\r') )
    {
        line.remove_suffix(1);
    }
    return {.text = line};
}

// The world dispatcher scans " section: %s"; the section name ends at
// C whitespace. Commas separate arguments inside tables, not section names.
inline std::optional<std::string_view> nativeJklSectionName(std::string_view line) noexcept
{
    line = text::trimLeft(line);
    if ( !text::istartsWithAscii(line, kJklSectionPrefix) ) return {};
    line = text::trimLeft(line.substr(kJklSectionPrefix.size()));
    return line.substr(0, line.find_first_of(" \t\r\n\v\f"));
}

inline bool isJklSoundSection(std::string_view line) noexcept
{
    const auto name = nativeJklSectionName(line.substr(0, line.find('#')));
    return name && text::iequalsAscii(*name, "SOUNDS");
}

// Only the JK sound handler accepts a compact SECTION:SOUNDS. Other modeled
// sections retain the level importer's second whitespace-delimited field.
inline std::string_view jklSectionName(std::string_view line, const ProjectType dialect,
    const bool requireCompleteCollections = false)
{
    const bool nativeSounds = usesJklSoundPreloads(dialect);
    if ( nativeSounds )
    {
        line = line.substr(0, line.find('#'));
        if ( isJklSoundSection(line) )
        {
            return "SOUNDS";
        }
    }
    nextJklField(line);
    const auto name = nextJklField(line);
    if ( requireCompleteCollections && !text::trimLeft(line).empty() )
    {
        throw std::runtime_error("Duplicate or ambiguous JKL/NDY section: " + text::upperAscii(name));
    }
    return nativeSounds && text::iequalsAscii(name, "SOUNDS") ? std::string_view{} : name;
}

} // namespace sed::assets::detail
