#pragma once

#include "sed/level.hpp"
#include "sed/text.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sed::assets::detail
{

// A legacy section name is the second whitespace-delimited word, even when
// the first word has additional characters attached to SECTION:.
inline std::string_view nextJklField(std::string_view& line) noexcept
{
    line = text::trimLeft(line);
    std::size_t size = 0;
    while (size < line.size() && !std::isspace(static_cast<unsigned char>(line[size])))
    {
        ++size;
    }
    const auto word = line.substr(0, size);
    line.remove_prefix(size);
    return word;
}

inline bool usesJklSoundPreloads(const ProjectType kind) noexcept
{
    return kind == ProjectType::JKDF2 || kind == ProjectType::MOTS;
}

// Identify the source dialect with its original section grammar before
// selecting the JK/MotS-specific SOUNDS grammar. No payload rows are retained.
class JklDialectReader
{
public:
    void observe(std::string_view physical) noexcept
    {
        auto line = text::trimView(physical.substr(0, physical.find('#')));
        if (text::istartsWithAscii(line, "SECTION:"))
        {
            nextJklField(line);
            const auto name = nextJklField(line);
            header_ = text::iequalsAscii(name, "HEADER");
            copyright_ = text::iequalsAscii(name, "COPYRIGHT");
            hasArchLighting_ |= text::iequalsAscii(name, "ARCHLIGHTING");
        }
        else if (text::iequalsAscii(line, "END"))
        {
            header_ = false;
            copyright_ = false;
        }
        else if (header_ && text::iequalsAscii(nextJklField(line), "VERSION"))
        {
            const auto spelling = nextJklField(line);
            if (!spelling.empty())
            {
                const auto version = text::parseInt<std::int32_t>(spelling).value_or(0);
                if (!sawVersion_)
                {
                    firstVersion_ = version;
                    sawVersion_ = true;
                }
                hasIjimVersion_ |= version == 2 || version == 3;
            }
        }
        else if (copyright_ && !hasDroidCopyright_)
        {
            // Retail decorates the notice with '@' and '.'. A containing
            // sentence is not a DroidWorks signature.
            constexpr std::string_view letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            const auto first = line.find_first_of(letters);
            if (first != std::string_view::npos)
            {
                const auto last = line.find_last_of(letters);
                hasDroidCopyright_ = text::iequalsAscii(line.substr(first, last - first + 1), "Lucas Learning Limited");
            }
        }
    }

    bool hasLevelVersion() const noexcept { return firstVersion_ >= 1 && firstVersion_ <= 3; }

    ProjectType initialProjectType(ProjectType hint) const noexcept
    {
        // Imported UV space and defaults historically use the first VERSION.
        if (firstVersion_ == 2 || firstVersion_ == 3)
        {
            return ProjectType::IJIM;
        }
        if (firstVersion_ == 1 && hint != ProjectType::MOTS && hint != ProjectType::DroidWorks)
        {
            hint = ProjectType::JKDF2;
        }
        if (hint != ProjectType::MOTS && hint != ProjectType::IJIM && hasDroidCopyright_)
        {
            hint = ProjectType::DroidWorks;
        }
        return hasArchLighting_ && hint != ProjectType::IJIM ? ProjectType::MOTS : hint;
    }

    ProjectType projectType(const ProjectType hint = ProjectType::JKDF2) const noexcept
    {
        // The ordinary HEADER decoder sees all VERSION rows. A later IJIM
        // version updates the Level kind without reinitializing its defaults.
        return hasIjimVersion_ ? ProjectType::IJIM : initialProjectType(hint);
    }

private:
    std::int32_t firstVersion_{};
    bool sawVersion_{};
    bool hasIjimVersion_{};
    bool header_{};
    bool copyright_{};
    bool hasDroidCopyright_{};
    bool hasArchLighting_{};
};

} // namespace sed::assets::detail
