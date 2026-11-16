#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sed::assets
{

struct CmpColor
{
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
};

using CmpPalette = std::array<CmpColor, 256>;
using CmpTranslation = std::array<std::uint8_t, 256>;
using CmpLightLevels = std::array<CmpTranslation, 64>;
using CmpLightRamp = std::array<CmpColor, 64>;

/** Classic CMP color data used by indexed MAT rendering. */
struct CmpFile
{
    CmpPalette palette{};
    /** Palette-index translation for every classic scalar light level. */
    CmpLightLevels lightLevels{};
    /**
     * The final classic light-level translation table. Original SED applies
     * this table to the level's master palette when a Sector CMP is active.
     */
    CmpTranslation translation{};
};

/** The header tint: three floats at offset 12, each channel's multiplier. */
struct CmpTint
{
    float r{ 1.0f };
    float g{ 1.0f };
    float b{ 1.0f };
};

std::optional<CmpFile> parseCmp(std::span<const std::byte> data) noexcept;
/**
 * Reads the header tint, which the hardware renderer multiplies a sector by
 * when the sector uses this colormap and it is not the level's master one.
 * Only the first 24 bytes are needed. Refuses a file that is not a CMP and a
 * negative or non-finite channel.
 */
std::optional<CmpTint> parseCmpTint(std::span<const std::byte> data) noexcept;
/**
 * A tinted colormap made from a master, the way the stock red maps are: each
 * light level's colour from the master is multiplied by the tint and mapped
 * to the nearest master palette colour (lowest index on a tie). The result
 * keeps the master's magic, version and palette, stores the tint in the
 * header for the hardware renderer, and has no transparency table (flags 0).
 * Throws std::invalid_argument for a master that is not a complete CMP or a
 * negative or non-finite tint.
 */
std::vector<std::byte> makeTintedCmp(std::span<const std::byte> master, const CmpTint& tint);
std::optional<CmpPalette> parseCmpPalette(std::span<const std::byte> data) noexcept;
CmpPalette applyCmpTranslation(
    const CmpPalette& masterPalette,
    const CmpTranslation& translation) noexcept;
CmpLightRamp makeCmpLightRamp(
    const CmpFile& cmp,
    std::uint8_t paletteIndex) noexcept;
CmpPalette makeGrayscalePalette() noexcept;

} // namespace sed::assets
