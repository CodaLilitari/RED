#pragma once

#include "sed/assets/jkl.hpp"
#include "sed/text.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace sed::assets::detail
{

inline constexpr std::string_view kJklSectionPrefix = "SECTION:";

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
    bool trimPrevious{};
};

// The native test sees the entire buffer after inline comment removal. For an
// ordinary LF-terminated line the byte after the slash is its newline. For an
// inline comment or physical EOF it can be an authored character instead.
inline JklSoundLinePiece splitJklSoundPhysicalLine(
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

inline bool isJklSoundSection(std::string_view line) noexcept
{
    line = text::trimLeft(line.substr(0, line.find('#')));
    if ( !text::istartsWithAscii(line, kJklSectionPrefix) )
    {
        return false;
    }
    line = text::trimLeft(line.substr(kJklSectionPrefix.size()));
    return text::iequalsAscii(line.substr(0, line.find_first_of(" \t\r\n\v\f")), "SOUNDS");
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

// Only SOUNDS uses the native logical-line grammar here. Recognize its
// continued outer header without buffering or changing arbitrary other sections.
class ContinuedSoundSection
{
public:
    void append(const JklSoundLinePiece piece)
    {
        if ( piece.trimPrevious )
        {
            --trailingSlashes_;
        }
        const auto last = piece.text.find_last_not_of('\\');
        if ( last == std::string_view::npos )
        {
            trailingSlashes_ += piece.text.size();
            return;
        }
        if ( trailingSlashes_ != 0 )
        {
            accept('\\');
        }
        for ( const char c : piece.text.substr(0, last + 1) )
        {
            if ( state_ == kMismatch || state_ == kMatched )
            {
                break;
            }
            accept(c);
        }
        trailingSlashes_ = piece.text.size() - (last + 1);
    }

    bool matches() const noexcept
    {
        return state_ == kMatched || (state_ == kName.size() && trailingSlashes_ == 0);
    }

    bool endsInSlash() const noexcept { return trailingSlashes_ != 0; }

private:
    static constexpr std::string_view kName = "SECTION:SOUNDS";
    static constexpr std::size_t kMatched = kName.size() + 1;
    static constexpr std::size_t kMismatch = kName.size() + 2;
    std::size_t state_{};
    std::size_t trailingSlashes_{};

    void accept(const char c)
    {
        const bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
        if ( state_ < kName.size() )
        {
            if ( (state_ == 0 || state_ == kJklSectionPrefix.size()) && space )
            {
                return;
            }
            state_ = std::toupper(static_cast<unsigned char>(c)) == kName[state_] ? state_ + 1 : kMismatch;
        }
        else if ( state_ == kName.size() )
        {
            state_ = space ? kMatched : kMismatch;
        }
    }
};

enum class SoundRowKind { Unrelated, Consumed, Action, Stopped };

struct JklSoundRow
{
    SoundRowKind kind{SoundRowKind::Consumed};
    std::string_view text{};
    std::string_view name{};
};

// Owns only a continued SOUNDS row. Both consumers receive the same table
// framing and logical text. Views last until the next read.
class JklSoundTableReader
{
public:
    template <typename Work>
    JklSoundRow read(const std::string_view physical, const bool hadNewline, Work&& takeWork)
    {
        const bool wasContinued = continued_;
        const bool previousSlash = continued_ && (inSounds_
            ? joined_.ends_with('\\') : outerHeader_.endsInSlash());
        const auto piece = splitJklSoundPhysicalLine(physical, hadNewline, previousSlash);
        if ( piece.skipped )
        {
            return {inSounds_ ? SoundRowKind::Consumed : SoundRowKind::Unrelated};
        }
        continued_ = piece.continued;
        if ( !inSounds_ )
        {
            return readOuterLine(piece, wasContinued);
        }

        if ( wasContinued && text::istartsWithAscii(text::trimLeft(physical), kJklSectionPrefix) )
        {
            // The ordinary importer reads section boundaries on physical lines.
            // It cannot also preserve this header as part of a sound action.
            throw JklSoundTableError("JKL sound continuation crosses a physical section header");
        }
        if ( !wasContinued )
        {
            joined_.clear();
        }
        if ( piece.trimPrevious )
        {
            joined_.pop_back();
        }
        if ( piece.text.size() > kJklSoundPreloadMaxRowBytes - joined_.size() )
        {
            throw JklSoundTableError("JKL sound preload logical row exceeds the byte limit");
        }
        std::string_view row = piece.text;
        if ( wasContinued || continued_ )
        {
            joined_.append(piece.text);
            if ( continued_ )
            {
                return {};
            }
            row = joined_;
        }
        return readRow(row, wasContinued, takeWork);
    }

    void finish() const
    {
        if ( inSounds_ && (continued_ || !capacity_) )
        {
            throw JklSoundTableError("Truncated JKL sound preload section");
        }
    }

    const std::optional<std::uint32_t>& capacity() const noexcept { return capacity_; }

private:
    JklSoundRow readOuterLine(const JklSoundLinePiece piece, const bool wasContinued)
    {
        const bool physicalHeader = isJklSoundSection(piece.text);
        if ( wasContinued || continued_ )
        {
            outerHeader_.append(piece);
            if ( physicalHeader || (!continued_ && outerHeader_.matches()) )
            {
                throw JklSoundTableError("Continued JKL SOUNDS section headers are unsupported");
            }
            if ( !continued_ )
            {
                outerHeader_ = {};
            }
            return {SoundRowKind::Unrelated};
        }
        if ( !physicalHeader )
        {
            return {SoundRowKind::Unrelated};
        }
        if ( capacity_ )
        {
            throw JklSoundTableError("Duplicate JKL SOUNDS section");
        }
        inSounds_ = true;
        return {};
    }

    template <typename Work>
    JklSoundRow readRow(const std::string_view row, const bool wasContinued, Work& takeWork)
    {
        if ( row.find_first_not_of(", \t\r\n") == std::string_view::npos )
        {
            return {};
        }
        if ( !capacity_ )
        {
            if ( !takeWork() || !takeWork() || !takeWork() )
            {
                return {SoundRowKind::Stopped};
            }
            capacity_ = parseJklSoundCapacity(row);
            if ( !capacity_ )
            {
                throw JklSoundTableError("Invalid or missing WORLD SOUNDS capacity");
            }
            return {};
        }
        if ( !takeWork() )
        {
            return {SoundRowKind::Stopped};
        }
        auto remaining = row;
        const auto name = nextJklSoundArgumentValue(remaining);
        if ( text::iequalsAscii(name, "end") )
        {
            inSounds_ = false;
            return {};
        }
        if ( text::istartsWithAscii(text::trimLeft(row), kJklSectionPrefix) )
        {
            if ( *capacity_ )
            {
                throw JklSoundTableError("JKL SOUNDS section is missing its END record");
            }
            if ( isJklSoundSection(row) )
            {
                throw JklSoundTableError("Duplicate JKL SOUNDS section");
            }
            if ( wasContinued )
            {
                throw JklSoundTableError("Continued section boundary after dormant JKL SOUNDS is unsupported");
            }
            inSounds_ = false;
            return {SoundRowKind::Unrelated};
        }
        if ( rows_ >= kJklSoundPreloadMaxRows || row.size() > kJklSoundPreloadMaxTotalRowBytes - rowBytes_
            || row.find('\0') != std::string_view::npos )
        {
            throw JklSoundTableError("Invalid or over-budget JKL sound preload rows");
        }
        ++rows_;
        rowBytes_ += row.size();
        return {SoundRowKind::Action, row, name};
    }

    std::optional<std::uint32_t> capacity_{};
    bool inSounds_{};
    bool continued_{};
    std::string joined_{};
    ContinuedSoundSection outerHeader_{};
    std::size_t rows_{};
    std::size_t rowBytes_{};
};

} // namespace sed::assets::detail
