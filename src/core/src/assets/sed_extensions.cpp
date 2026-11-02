#include "sed/assets/sed_extensions.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace sed::assets
{
namespace
{

constexpr std::uint8_t kPayloadVersion = 1;

void appendU8(std::vector<std::byte>& out, const std::uint8_t value)
{
    out.push_back(static_cast<std::byte>(value));
}

void appendU32(std::vector<std::byte>& out, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void appendI32(std::vector<std::byte>& out, const std::int32_t value)
{
    appendU32(out, static_cast<std::uint32_t>(value));
}

void appendF64(std::vector<std::byte>& out, const double value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        out.push_back(static_cast<std::byte>((bits >> shift) & 0xffu));
    }
}

void appendString(std::vector<std::byte>& out, const std::string& value)
{
    appendU32(out, static_cast<std::uint32_t>(value.size()));
    for (const char ch : value)
    {
        out.push_back(static_cast<std::byte>(ch));
    }
}

struct Reader
{
    std::span<const std::byte> bytes{};
    std::size_t offset{};

    [[nodiscard]] std::uint8_t u8(const char* what)
    {
        if (offset + 1 > bytes.size())
        {
            throw std::invalid_argument(
                std::string("Truncated SED extension payload at ") + what + ".");
        }
        return static_cast<std::uint8_t>(bytes[offset++]);
    }

    [[nodiscard]] std::uint32_t u32(const char* what)
    {
        std::uint32_t value{};
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            value |= static_cast<std::uint32_t>(u8(what)) << shift;
        }
        return value;
    }

    [[nodiscard]] std::int32_t i32(const char* what)
    {
        return static_cast<std::int32_t>(u32(what));
    }

    [[nodiscard]] double f64(const char* what)
    {
        std::uint64_t bits{};
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            bits |= static_cast<std::uint64_t>(u8(what)) << shift;
        }
        return std::bit_cast<double>(bits);
    }

    [[nodiscard]] std::string string(const char* what)
    {
        const std::uint32_t size = u32(what);
        if (offset + size > bytes.size())
        {
            throw std::invalid_argument(
                std::string("Truncated SED extension payload at ") + what + ".");
        }
        std::string value{};
        value.resize(size);
        for (std::uint32_t i = 0; i < size; ++i)
        {
            value[i] = static_cast<char>(bytes[offset++]);
        }
        return value;
    }

    void expectEnd(const char* what) const
    {
        if (offset != bytes.size())
        {
            throw std::invalid_argument(
                std::string("Trailing bytes in SED extension payload at ") + what + ".");
        }
    }
};

const SedOptionalSection* findSection(
    const std::span<const SedOptionalSection> sections,
    const std::array<char, 4>& type, const char* label)
{
    const SedOptionalSection* found{};
    for (const SedOptionalSection& section : sections)
    {
        if (section.type == type)
        {
            if (found != nullptr)
            {
                throw std::invalid_argument(
                    std::string("Duplicate SED extension section: ") + label + ".");
            }
            found = &section;
        }
    }
    return found;
}

} // namespace

std::optional<SedOptionalSection> makeSedLightRgbSection(const Level& level)
{
    bool any{};
    for (const SedLight& light : level.lights)
    {
        if (light.rgbIntensity.has_value() || light.legacyRgbRange.has_value())
        {
            any = true;
        }
        if ((light.rgbIntensity.has_value() && !std::isfinite(*light.rgbIntensity))
            || (light.legacyRgbRange.has_value()
                && !std::isfinite(*light.legacyRgbRange)))
        {
            throw std::invalid_argument(
                "Non-finite source-light RGB value cannot be persisted.");
        }
    }
    if (!any)
    {
        return std::nullopt;
    }
    SedOptionalSection section{};
    section.type = SedLightRgbSectionType;
    appendU8(section.payload, kPayloadVersion);
    appendU32(section.payload,
        static_cast<std::uint32_t>(level.lights.size()));
    for (const SedLight& light : level.lights)
    {
        std::uint8_t presence{};
        if (light.rgbIntensity.has_value())
        {
            presence |= 0x1u;
        }
        if (light.legacyRgbRange.has_value())
        {
            presence |= 0x2u;
        }
        appendU8(section.payload, presence);
        if (light.rgbIntensity.has_value())
        {
            appendF64(section.payload, *light.rgbIntensity);
        }
        if (light.legacyRgbRange.has_value())
        {
            appendF64(section.payload, *light.legacyRgbRange);
        }
    }
    return section;
}

bool applySedLightRgbSection(
    Level& level, const std::span<const SedOptionalSection> sections)
{
    const SedOptionalSection* section = findSection(
        sections, SedLightRgbSectionType, "light RGB");
    if (section == nullptr)
    {
        return false;
    }
    Reader reader{ section->payload };
    if (reader.u8("version") != kPayloadVersion)
    {
        throw std::invalid_argument("Unsupported light-RGB SED extension version.");
    }
    const std::uint32_t count = reader.u32("light count");
    if (count != level.lights.size())
    {
        throw std::invalid_argument(
            "Light-RGB SED extension count does not match the level lights; "
            "the section was rejected, not applied.");
    }
    std::vector<SedLight> updated = level.lights;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint8_t presence = reader.u8("presence");
        if ((presence & ~0x3u) != 0)
        {
            throw std::invalid_argument("Unknown light-RGB presence bits.");
        }
        if ((presence & 0x1u) != 0)
        {
            updated[i].rgbIntensity = reader.f64("rgbIntensity");
        }
        if ((presence & 0x2u) != 0)
        {
            updated[i].legacyRgbRange = reader.f64("legacyRgbRange");
        }
    }
    reader.expectEnd("light RGB");
    level.lights = std::move(updated);
    return true;
}

std::optional<SedOptionalSection> makeSedArchAuthoringSection(const Level& level)
{
    if (level.motsArchAuthoring.empty())
    {
        return std::nullopt;
    }
    SedOptionalSection section{};
    section.type = SedArchAuthoringSectionType;
    appendU8(section.payload, kPayloadVersion);
    appendU32(section.payload,
        static_cast<std::uint32_t>(level.motsArchAuthoring.size()));
    for (const MotsArchAuthoringEntry& entry : level.motsArchAuthoring)
    {
        appendI32(section.payload, entry.thingIndex);
        appendU8(section.payload, static_cast<std::uint8_t>(entry.origin));
        appendString(section.payload, entry.modelName);
        appendString(section.payload, entry.modelEntrySha256Hex);
        appendString(section.payload, entry.bakeInputSha256Hex);
        appendString(section.payload, entry.sampleSha256Hex);
        appendString(section.payload, entry.bakeProfile);
    }
    return section;
}

bool applySedArchAuthoringSection(
    Level& level, const std::span<const SedOptionalSection> sections)
{
    const SedOptionalSection* section = findSection(
        sections, SedArchAuthoringSectionType, "ARCH authoring");
    if (section == nullptr)
    {
        return false;
    }
    Reader reader{ section->payload };
    if (reader.u8("version") != kPayloadVersion)
    {
        throw std::invalid_argument("Unsupported ARCH-authoring SED extension version.");
    }
    const std::uint32_t count = reader.u32("entry count");
    std::vector<MotsArchAuthoringEntry> entries{};
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        MotsArchAuthoringEntry entry{};
        entry.thingIndex = reader.i32("thingIndex");
        const std::uint8_t origin = reader.u8("origin");
        if (origin > static_cast<std::uint8_t>(MotsArchSampleOrigin::BakedWorldOccludedV1))
        {
            throw std::invalid_argument("Unknown ARCH sample origin in SED extension.");
        }
        entry.origin = static_cast<MotsArchSampleOrigin>(origin);
        entry.modelName = reader.string("modelName");
        entry.modelEntrySha256Hex = reader.string("modelEntrySha256Hex");
        entry.bakeInputSha256Hex = reader.string("bakeInputSha256Hex");
        entry.sampleSha256Hex = reader.string("sampleSha256Hex");
        entry.bakeProfile = reader.string("bakeProfile");
        entries.push_back(std::move(entry));
    }
    reader.expectEnd("ARCH authoring");
    level.motsArchAuthoring = std::move(entries);
    return true;
}

} // namespace sed::assets
