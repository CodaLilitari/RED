#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "sed/geometry.hpp"
#include "sed/image.hpp"
#include "sed/level.hpp"

namespace sed::render
{

/** One resolved material texture for the software snapshot renderer. */
struct SnapshotMaterial
{
    Rgba8Image image{};
    bool trueColor{};
    std::int32_t alphaBpp{};
};

/** Returns the material for a surface material name, or nullptr when
 * the caller could not resolve it (the surface is then flat-shaded). */
using SnapshotMaterialResolver =
    std::function<const SnapshotMaterial*(std::string_view materialName)>;

struct SnapshotCamera
{
    TVector position{};
    TVector target{};
    bool orthographic{};
    double fieldOfViewDegrees{ 90.0 };
    double orthographicHeight{};
    double nearPlane{ 0.1 };
    double farPlane{ 10000.0 };
};

struct SoftwareSnapshotOptions
{
    int width{};
    int height{};
    SnapshotCamera camera{};
    bool fullbright{};
    bool cullBackFaces{};
};

struct SoftwareSnapshotResult
{
    std::vector<std::uint8_t> png{};
    int surfacesDrawn{};
    int trianglesDrawn{};
    int materialsUsed{};
    int materialsMissing{};
};

/** Renders the level's world surfaces with a small CPU rasterizer
 * (z-buffered, perspective-correct UVs, near-plane clipping) and
 * returns a PNG. Scope, deliberately narrow and reported by callers:
 * world surfaces only (no things, models, sprites or dynamic
 * lights); static sector ambient is the only light unless
 * fullbright is set. Textures use mip 0 with nearest sampling.
 * Throws std::invalid_argument for bad dimensions/camera and
 * std::runtime_error for degenerate level geometry indices. */
SoftwareSnapshotResult renderSoftwareSnapshot(const Level& level,
                                              const SnapshotMaterialResolver& materials,
                                              const SoftwareSnapshotOptions& options);

/** Encodes an RGBA8 image (width*height*4 bytes) as a PNG with
 * stored (uncompressed) zlib blocks. */
std::vector<std::uint8_t> encodePngRgba8(int width, int height,
                                         const std::vector<std::uint8_t>& rgba);

} // namespace sed::render
