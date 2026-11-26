#pragma once

#include "sed/assets/raven_bsp.hpp"

#include <optional>
#include <functional>

namespace sed::assets
{

inline constexpr int kRavenLightmapGridSteps = 6;
inline constexpr int kRavenLightmapProbesPerTriangle =
    (kRavenLightmapGridSteps + 1) * (kRavenLightmapGridSteps + 2) / 2;

struct RavenLightmapWitness
{
    double maxChannelError{};
    std::size_t triangle{};
    std::array<double, 3> barycentric{}, sourceRgb{}, approximationRgb{}, sourcePosition{};
};

struct RavenLightmapFidelity
{
    std::size_t triangleCount{};
    std::optional<RavenLightmapWitness> rgbInterpolation, cornerScalar;
};

// The scalar sampler takes a valid first-atlas index and finite UVs from a
// validated RBSP. It retains the importer's luminance-before-interpolation
// convention and float precision.
float ravenLightmapLuminance(const RavenBsp& bsp, int atlas, double u, double v);
// Inspect an existing surface of a validated RBSP, with finite geometry and
// finite UVs on lightmapped surfaces. Use the recipe's patch subdivisions (1–16).
RavenLightmapFidelity inspectRavenLightmap(const RavenBsp& bsp, std::size_t surface, std::uint32_t patchSubdivisions);

struct RankedLightmapSurface
{
    std::size_t surface{};
    RavenLightmapWitness witness;
};

struct RavenLightmapRanking
{
    std::size_t sampledSurfaces{}, withoutAtlas{}, withoutTriangles{}, triangleCount{};
    std::vector<RankedLightmapSurface> rgbInterpolation, cornerScalar;
};

// Return the worst source faces for each metric. Equal losses retain source order.
// A scan beyond the map budget fails without returning a partial ranking.
inline constexpr std::size_t kRavenMapLightmapTriangleLimit = 250000;
RavenLightmapRanking rankRavenLightmaps(const RavenBsp& bsp, std::uint32_t patchSubdivisions,
                                       std::size_t top, const std::function<void()>& checkActive = {});

} // namespace sed::assets
