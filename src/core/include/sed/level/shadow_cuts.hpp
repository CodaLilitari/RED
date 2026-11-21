#pragma once

#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sed::level
{

/**
 * Shadow cuts (E-16): JK lights the corners of a surface and blends between
 * them, so a shadow edge can only fall where a surface has corners. These
 * cuts add those corners without baking light. Existing corner values are
 * retained and new values interpolate along the cut edges.
 *
 * - `shadowEdges` follows each light through the openings between sectors,
 *   the way the bake does, and cuts the surfaces it partly reaches along the
 *   planes through the light and the edges of each opening. Those planes are
 *   where the light stops.
 * - `sizeLimit` splits every surface the light reaches until no edge is
 *   longer than `maxEdge`, for softer shadows that don't depend on exact
 *   light positions.
 * - `selectedReceivers` refines explicit Gouraud world faces without light
 *   sources. Every resulting face has a vertex-to-vertex span <= `maxEdge`;
 *   this also adds interior samples when boundary edges are already short.
 *   It refuses the entire operation if a required cut cannot be made.
 *
 * Openings (adjoined surfaces), fully lit surfaces (light mode 0) and, in
 * light-guided modes, surfaces turned away from the light are never cut. A cut
 * that would give any surface more than the engine's 24 corners is skipped
 * in light-guided modes and refuses the entire selected-receiver operation
 * (each cut hands a corner to the surfaces beside it). Lights flagged
 * `LF_NoBlock` cast no shadows. `SAF_BlockLight` openings stop the light.
 */
enum class ShadowCutMode
{
    shadowEdges,
    sizeLimit,
    selectedReceivers,
};

inline constexpr std::size_t kMaxSelectedLightReceivers = 256;

struct ShadowCutOptions
{
    ShadowCutMode mode{ ShadowCutMode::shadowEdges };
    /** Light-guided modes: empty means every light. Must be empty for selectedReceivers. */
    std::vector<std::int32_t> lights{};
    /** sizeLimit: longest edge; selectedReceivers: longest vertex pair (JKU). */
    double maxEdge{ 0.5 };
    /** shadowEdges: a cut that would leave a piece thinner than this is skipped (JKU). */
    double minPiece{ 0.02 };
    /** Refuse, changing nothing, when the plan adds more surfaces than this. */
    std::int32_t maxNewSurfaces{ 2000 };
    /** How many openings deep a light is followed. */
    std::int32_t maxDepth{ 8 };
    /** selectedReceivers only: 1..256 distinct faces; never inferred from lights. */
    std::vector<SurfaceRef> surfaces{};
};

struct ShadowCutResult
{
    bool ok{ false };
    /** Set when nothing changed: overBudget, cornerLimit, badLight, badOptions,
     * badReceiver (invalid/unsupported selection), or receiverGeometry. */
    std::string refusal{};
    /** Cutting planes requested; a shadow-edge plane can split more than one piece. */
    std::int32_t plannedCuts{};
    std::int32_t surfacesCut{};
    std::int32_t newSurfaces{};
    /** Cuts refused by geometry checks; selectedReceivers refuses the whole operation. */
    std::int32_t refusedCuts{};
    /** Cuts skipped because a surface would pass the engine's 24-corner limit. */
    std::int32_t cornerLimitSkips{};
    /** Lights inside no sector cast nothing and are counted here. */
    std::int32_t lightsOutside{};
    /** True when a light's walk through the openings hit the visit cap. */
    bool walkTruncated{ false };
    /** selectedReceivers only: largest resulting vertex-pair distance (JKU). */
    double maxReceiverSpan{};
    /** Every surface that is a piece of a cut surface, after the cuts. */
    std::vector<SurfaceRef> pieces{};
    // One family per original face. No family crosses an authored split.
    std::vector<std::vector<SurfaceRef>> families{};
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

// Remove a complete recorded generation on a copy. A newly authored texture
// or lighting seam makes the operation refuse without removing any faces.
bool levelRemoveOwnedShadowCuts(Level& level, std::span<const std::vector<SurfaceRef>> families,
                                std::span<SurfaceRef> trackedSurfaces = {});

} // namespace sed::level
