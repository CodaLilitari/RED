#pragma once

#include "sed/level.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sed::level
{

/**
 * Shadow cuts (E-16): JK lights the corners of a surface and blends between
 * them, so a shadow edge can only fall where a surface has corners. These
 * cuts add those corners before a bake; they never change lighting values.
 *
 * - `shadowEdges` follows each light through the openings between sectors,
 *   the way the bake does, and cuts the surfaces it partly reaches along the
 *   planes through the light and the edges of each opening. Those planes are
 *   where the light stops.
 * - `sizeLimit` splits every surface the light reaches until no edge is
 *   longer than `maxEdge`, for softer shadows that don't depend on exact
 *   light positions.
 *
 * Openings (adjoined surfaces), fully lit surfaces (light mode 0) and
 * surfaces turned away from the light are never cut, and a cut that would
 * give any surface more than the engine's 24 corners is skipped (each cut
 * hands a corner to the surfaces beside it). Lights flagged
 * `LF_NoBlock` cast no shadows. `SAF_BlockLight` openings stop the light.
 */
enum class ShadowCutMode
{
    shadowEdges,
    sizeLimit,
};

struct ShadowCutOptions
{
    ShadowCutMode mode{ ShadowCutMode::shadowEdges };
    /** Light indices; empty means every light. */
    std::vector<std::int32_t> lights{};
    /** sizeLimit: the longest edge a piece may keep (JKU). */
    double maxEdge{ 0.5 };
    /** shadowEdges: a cut that would leave a piece thinner than this is skipped (JKU). */
    double minPiece{ 0.02 };
    /** Refuse, changing nothing, when the plan adds more surfaces than this. */
    std::int32_t maxNewSurfaces{ 2000 };
    /** How many openings deep a light is followed. */
    std::int32_t maxDepth{ 8 };
};

struct ShadowCutResult
{
    bool ok{ false };
    /** Set when nothing changed: "overBudget", "cornerLimit", "badLight" or "badOptions". */
    std::string refusal{};
    /** Cuts planned before cutting starts; a plane can split more than one piece. */
    std::int32_t plannedCuts{};
    std::int32_t surfacesCut{};
    std::int32_t newSurfaces{};
    /** Cuts the geometry check refused; those surfaces stay as they were. */
    std::int32_t refusedCuts{};
    /** Cuts skipped because a surface would pass the engine's 24-corner limit. */
    std::int32_t cornerLimitSkips{};
    /** Lights inside no sector cast nothing and are counted here. */
    std::int32_t lightsOutside{};
    /** True when a light's walk through the openings hit the visit cap. */
    bool walkTruncated{ false };
    /** Every surface that is a piece of a cut surface, after the cuts. */
    std::vector<SurfaceRef> pieces{};
};

/** Plans the cuts and applies them unless the plan is over budget. */
ShadowCutResult levelShadowCuts(Level& level, const ShadowCutOptions& options);

struct ShadowCutRemoval
{
    std::int32_t merges{};
    std::int32_t sectorsChanged{};
};

/**
 * Merges coplanar neighbouring solid surfaces back together when they share
 * material, flags, modes, extra light, shared corner colors and the same UV
 * projection. This structural cleanup has no cut provenance. Undo restores
 * the exact previous geometry. Empty `sectors` means every sector. Optional
 * tracked face references follow the surviving face through each merge.
 */
ShadowCutRemoval levelRemoveShadowCuts(Level& level, std::span<const std::int32_t> sectors,
    std::span<SurfaceRef> trackedSurfaces = {});

} // namespace sed::level
