#pragma once

#include <optional>
#include <span>

#include "sed/assets/sed.hpp"
#include "sed/level.hpp"

namespace sed::assets
{

/** Optional SED section carrying per-light RGB fields that the
 * binary Lights section does not model (`rgbIntensity`,
 * `legacyRgbRange`). Presence is exact: a light with both fields
 * absent produces no section at all, so files written for levels
 * without RGB data stay byte-identical to older builds. Values are
 * stored as IEEE-754 bit patterns (signed zero survives). */
inline constexpr std::array<char, 4> SedLightRgbSectionType{ 'l', 'r', 'g', 'b' };

/** Optional SED section carrying ARCH authoring provenance
 * (`Level::motsArchAuthoring`). Imported entries keep
 * ImportedUnknown; no state is upgraded by persistence. */
inline constexpr std::array<char, 4> SedArchAuthoringSectionType{ 'a', 'r', 'c', 'a' };

/** Builds the light-RGB section, or nullopt when no light carries
 * either field. Throws std::invalid_argument on non-finite values. */
[[nodiscard]] std::optional<SedOptionalSection> makeSedLightRgbSection(
    const Level& level);

/** Applies the light-RGB section to a loaded level.
 * Returns false when no such section is present (fields stay
 * absent). Throws std::invalid_argument on a malformed payload,
 * a duplicate section, or a count mismatch - mismatched data is
 * rejected, never partially applied. */
bool applySedLightRgbSection(
    Level& level, std::span<const SedOptionalSection> sections);

/** Builds the ARCH-authoring section, or nullopt when the level
 * has no authoring entries. */
[[nodiscard]] std::optional<SedOptionalSection> makeSedArchAuthoringSection(
    const Level& level);

/** Applies the ARCH-authoring section. Same contract as
 * applySedLightRgbSection: false when absent, throw on malformed
 * or duplicate data. */
bool applySedArchAuthoringSection(
    Level& level, std::span<const SedOptionalSection> sections);

} // namespace sed::assets
