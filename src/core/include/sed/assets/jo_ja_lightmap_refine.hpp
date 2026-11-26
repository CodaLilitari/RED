#pragma once

#include "sed/assets/raven_bsp.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace sed::assets
{

inline constexpr double kJoJaLightmapRefinementDefaultMinimumEdge = 16.0;
inline constexpr double kJoJaLightmapRefinementMaximumMinimumEdge = 4096.0;

/** Opt-in model-scene setting that splits triangles where the first lightmap varies between their corners. */
struct JoJaLightmapRefinement
{
    // Largest admitted luminance difference, on a 0-1 scale, between the bilinear first-atlas sample and the
    // linear interpolation of corner luminance at any fixed grid probe. Admitted range (0, 1).
    double tolerance{};
    // Source units. A triangle whose longest edge is shorter than this does not start a split. Admitted
    // range [1, 4096]. Raven compiles default to 16 units per luxel.
    double minimumEdge{kJoJaLightmapRefinementDefaultMinimumEdge};
};

struct JoJaLightmapRefinementResult
{
    std::vector<RavenBspTriangle> triangles;
    // Input index of each output triangle. Nondecreasing, so children keep their parent's place in order.
    std::vector<std::size_t> parents;
    // Input triangles replaced by two or more children, and the bisection passes that ran.
    std::size_t splitSourceTriangles{}, passes{};
    // Largest interpolation error over lightmapped input and output triangles.
    double largestErrorBefore{}, largestErrorAfter{};
};

// Throws std::invalid_argument unless both settings are finite and inside their admitted ranges.
void validateJoJaLightmapRefinement(const JoJaLightmapRefinement& settings);

// Largest absolute difference over the kRavenLightmapProbesPerTriangle grid probes between the first-atlas
// luminance at the probe's interpolated lightmap UV and the same barycentric interpolation of the three
// corner luminances the importer stores. Zero for a surface without a first atlas.
double joJaLightmapInterpolationError(const RavenBsp& bsp, const RavenBspTriangle& triangle);

/**
 * Conforming longest-edge bisection driven by first-lightmap interpolation error.
 *
 * Each pass marks the longest edge of every lightmapped triangle whose error exceeds the tolerance and
 * whose longest edge is at least minimumEdge. Any triangle with a marked edge then has its own longest
 * edge marked too, until no mark is added, so every triangle sharing a split edge (matched by exact
 * source positions, across surfaces and including vertex-lit ones) splits it at the same midpoint and no
 * T-junction is created. A triangle is bisected across its longest edge and each half across its marked
 * original edge. Winding, surface, texture and lightmap coordinates, vertex color and normal direction
 * are interpolated along the split edge, so unmarked triangles and vertex-lit shading are unchanged.
 *
 * Throws std::runtime_error, without partial output, when the result would exceed maxTriangles.
 */
JoJaLightmapRefinementResult refineJoJaLightmapTriangles(const RavenBsp& bsp,
                                                         std::span<const RavenBspTriangle> triangles,
                                                         const JoJaLightmapRefinement& settings,
                                                         std::size_t maxTriangles);

} // namespace sed::assets
