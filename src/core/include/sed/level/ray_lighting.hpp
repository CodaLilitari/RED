#pragma once

#include "sed/level/lighting.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace sed::level
{

struct RayLightVec
{
    float x{}, y{}, z{};
};

struct RayLightTriangle
{
    RayLightVec a{}, b{}, c{};
};

struct RayLightNode
{
    RayLightVec minimum{}, maximum{};
    std::int32_t left{-1}, right{-1}, first{}, count{};
};

struct RayLightRay
{
    RayLightVec origin{}, direction{};
    float minDistance{}, maxDistance{};
    std::int32_t root{-1};
};

struct RayLightHit
{
    std::int32_t triangle{-1};
    float distance{}, u{}, v{};
};

// One root per sector. Leaves have count > 0 and a contiguous triangle range.
// Test both sides. Nearest distance wins; exact ties choose the lower triangle
// index. Distance is strictly greater than minDistance and at most maxDistance.
// u and v are the barycentric weights of b and c. Write every hit, including misses.
using RayLightIntersector = std::function<bool(std::span<const RayLightTriangle>, std::span<const RayLightNode>,
                                               std::span<const RayLightRay>, std::span<RayLightHit>)>;

[[nodiscard]] bool intersectRayLightingCpu(std::span<const RayLightTriangle> triangles,
                                           std::span<const RayLightNode> nodes, std::span<const RayLightRay> rays,
                                           std::span<RayLightHit> hits);

struct RayLightingMaterial
{
    std::int32_t sector{-1}, surface{-1};
    ColorF albedo{1, 1, 1, 1};
    bool sky{};
    ColorF emission{0, 0, 0, 0}; // Explicit emitted RGB radiance at an opaque hit.
};

struct RayLightingBlocker
{
    std::int32_t sector{-1};
    RayLightTriangle triangle{};
};

struct RayLightingOptions
{
    std::uint32_t samples{32}, bounces{2};
    ColorF skyColor{1, 1, 1, 1};
    double skyIntensity{1.0}, reflectivity{0.65};
    LightingBakeOptions direct{};
};

// Sums of mean linear RGB at the receiving corners, before target normalization.
struct RayLightingContributions
{
    std::uint64_t corners{}, surfaces{};
    double direct{}, sky{}, emission{}, diffuse{}, longestEdge{};
};

struct RayLightingResult
{
    bool succeeded{}, cancelled{};
    std::string message{};
    std::uint64_t rays{}; // Intersection queries, including portal continuations.
};

inline constexpr std::uint32_t kRayLightingMaxSamples = 128;
inline constexpr std::uint32_t kRayLightingMaxBounces = 4;
inline constexpr std::uint64_t kRayLightingMaxQueries = 32'000'000;
inline constexpr std::uint64_t kRayLightingMaxTraversalWork = 512'000'000;
inline constexpr std::uint32_t kRayLightingMaxQueryWork = 65'536;
inline constexpr std::uint32_t kRayLightingMaxTriangles = 1'000'000;
inline constexpr std::uint32_t kRayLightingMaxCorners = 1'000'000;

// Recalculate unclamped direct light on a clean full-world snapshot, gather diffuse
// sky/emission and each indirect order, then publish only selected surface
// corners. Authored sector ambient is preserved; generated mirror floors are rejected.
// MotS/IJIM retain the authored white/alpha slot. JK outputs scalar light in
// all four channels. Stored RGB/JK scalar clamp to [0,1] only after transport.
// Optional model blockers absorb gather rays in their
// supplied sector. They do not modify the legacy direct light or model assets.
[[nodiscard]] RayLightingResult bakeRayLighting(Level& level, std::span<const std::int32_t> targets,
                                                const RayLightingOptions& options,
                                                std::span<const RayLightingMaterial> materials = {},
                                                std::span<const RayLightingBlocker> blockers = {},
                                                const RayLightIntersector& intersector = {},
                                                const LightingProgressCallback& progress = {});

[[nodiscard]] RayLightingResult bakeRayLightingWithReport(Level& level, std::span<const std::int32_t> targets,
    const RayLightingOptions& options, std::span<const RayLightingMaterial> materials,
    std::span<const RayLightingBlocker> blockers, const RayLightIntersector& intersector,
    const LightingProgressCallback& progress, RayLightingContributions& contributions);

} // namespace sed::level
