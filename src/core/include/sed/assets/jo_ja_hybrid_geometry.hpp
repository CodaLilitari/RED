#pragma once

#include "sed/assets/raven_bsp.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sed::assets
{

struct JoJaHybridTriangle
{
    RavenBspTriangle triangle;
    std::size_t material{};
    std::size_t sector{};
};

struct JoJaHybridModelFace
{
    // One clipped piece of an original source triangle. Its UV and light
    // interpolation remain affine, so the convex polygon need not be fanned
    // into separate model faces. Positions are in target units.
    std::vector<RavenBspVertex> corners;
    std::size_t material{};
    std::size_t sector{};
    std::size_t sourceSurface{};
};

struct JoJaHybridFace
{
    // Target positions; original normalized UVs and interpolated sampled light.
    std::vector<RavenBspVertex> corners;
    TVector normal{};
    std::optional<std::size_t> material;
    std::size_t sourceSurface{};
    std::int32_t neighbor{-1};
    std::int32_t mirror{-1};
};

struct JoJaHybridCell
{
    std::vector<JoJaHybridFace> faces;
    // Outward unit normals: inside iff dot(normal, p) <= distance.
    std::vector<RavenBspPlane> planes;
    TVector interior{};
    std::vector<std::size_t> sourceLeaves;
};

struct JoJaHybridOptions
{
    double scale{1.0 / 320.0};
    std::size_t maxSectors{64};
    std::size_t maxFragments{500000};
};

struct JoJaHybridGeometry
{
    std::vector<JoJaHybridCell> cells;
    std::vector<JoJaHybridTriangle> residual;
    std::vector<JoJaHybridModelFace> residualPolygons;
    std::size_t nativeFaces{};
    std::size_t portalPairs{};
    std::size_t splitSourceTriangles{};
    std::size_t untrimmedBoundaryPlanes{};
    std::size_t nativePromotionFallbacks{};
    std::size_t targetCollapsedFragments{};
    double targetCollapsedArea{};
    double sourceArea{};
    double outputArea{};
    double maximumRelativeCoverageError{};
};

// Source triangles are in Raven units. Their corner colors must already contain
// sampled light; clipping interpolates it instead of re-sampling an atlas.
// The result is entirely in target units. Throws before returning an incomplete
// partition, uncovered source geometry or an exceeded work/fragment budget.
JoJaHybridGeometry buildJoJaHybridGeometry(const RavenBsp& bsp, std::span<const JoJaHybridTriangle> triangles,
                                           const JoJaHybridOptions& options = {});

bool joJaHybridContains(const JoJaHybridCell& cell, const TVector& point, double tolerance = 0.0) noexcept;

} // namespace sed::assets
