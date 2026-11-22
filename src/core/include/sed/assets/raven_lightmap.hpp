#pragma once

#include "sed/assets/raven_bsp.hpp"

#include <optional>

namespace sed::assets
{

inline constexpr int kRavenLightmapGridSteps = 6;
inline constexpr int kRavenLightmapProbesPerTriangle =
    (kRavenLightmapGridSteps + 1) * (kRavenLightmapGridSteps + 2) / 2;

struct RavenLightmapWitness
{
    double maxChannelError{};
    std::size_t triangle{};
    std::array<double, 3> barycentric{}, sourceRgb{}, approximationRgb{};
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

} // namespace sed::assets
