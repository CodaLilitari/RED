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
 * header for the hardware renderer, and carries the master's transparency
 * table when the master has one (flags 1), otherwise none (flags 0).
 * Throws std::invalid_argument for a master that is not a complete CMP or a
 * negative or non-finite tint.
 */
std::vector<std::byte> makeTintedCmp(std::span<const std::byte> master, const CmpTint& tint);
std::optional<CmpPalette> parseCmpPalette(std::span<const std::byte> data) noexcept;

/**
 * The palette entry nearest a colour under the distance the stock JK light
 * tables were built with: squared channel differences weighted 0.30, 0.59 and
 * 0.11 (red, green, blue). Measured on dflt.cmp, it reproduces 98.7% of the
 * stock light-table entries exactly, against 67% for plain RGB distance. Ties
 * go to the lowest index.
 */
std::uint8_t nearestCmpColor(const CmpPalette& palette, double r, double g, double b) noexcept;

/** One colour to place in a master palette. */
struct CmpPaletteAddition
{
    CmpColor color{};
    /** Drawn at full brightness at every light level, like the stock emissive entries. */
    bool fullbright{false};
};

struct CmpMasterBuild
{
    std::vector<std::byte> bytes;
    /** The slot each addition was given, in order. */
    std::vector<std::uint8_t> assignedSlots;
    /** Candidate slots passed over: used by a protected material, or fullbright in the master. */
    std::vector<std::uint8_t> skippedSlots;
    std::size_t lightEntriesComputed{};
    std::size_t transparencyEntriesComputed{};
};

/**
 * A master colormap with new colours, made the way the stock level masters
 * are: the stock masters share slots 0-191 and give each level its own colours
 * in 192-254, which dflt.cmp leaves as magenta placeholders.
 *
 * Each addition takes the next candidate slot that no protected material uses
 * and that the master does not keep fullbright (an entry that maps to itself
 * at every light level). Its light rows follow the stock rule: level l is the
 * colour scaled by l/63 mapped to nearestCmpColor, level 63 is the entry
 * itself; a fullbright addition maps to itself at every level. Its
 * transparency row and column blend 0.35 of the row colour with 0.65 of the
 * column colour (96.6% of dflt.cmp's table reproduced exactly). Any other light
 * or transparency entry that pointed at a replaced slot is recomputed by the
 * same rules. Every other byte is the master's, so textures that do not use
 * the replaced slots look exactly as they did.
 *
 * Throws std::invalid_argument for a master without its light tables, a
 * repeated candidate slot, or fewer usable slots than additions.
 */
CmpMasterBuild makeMasterCmp(std::span<const std::byte> master,
                             std::span<const CmpPaletteAddition> additions,
                             std::span<const std::uint8_t> candidateSlots,
                             const std::array<bool, 256>& protectedIndices);
CmpPalette applyCmpTranslation(
    const CmpPalette& masterPalette,
    const CmpTranslation& translation) noexcept;
CmpLightRamp makeCmpLightRamp(
    const CmpFile& cmp,
    std::uint8_t paletteIndex) noexcept;
CmpPalette makeGrayscalePalette() noexcept;

} // namespace sed::assets
