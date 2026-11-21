#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "sed/geometry.hpp"
#include "sed/assets/mat.hpp"
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
    std::optional<assets::IndexedMatCel> indexed{};
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

/** Transient preview geometry; never appended to the authored Level. */
struct SnapshotGeometry
{
    Sector sector{};
    UvCoordinateSpace uvCoordinateSpace{ UvCoordinateSpace::Normalized };
    bool doubleSided{};
};

/** How world surfaces are shaded. */
enum class SnapshotLighting : std::uint8_t
{
    /** Each surface by its sector's ambient light (the original contract). */
    sectorAmbient,
    /**
     * Each corner by its stored (baked) light, combined the way the game does:
     * light = max(sector extra light, min(1, corner light + face extra light)),
     * interpolated across the face, with light mode 0 drawn at full light and
     * a sector on a non-master colormap multiplied by that colormap's tint.
     * JK reads one value per corner from `a`; MotS and IJIM read RGB.
     */
    baked,
};

struct SoftwareSnapshotOptions
{
    int width{};
    int height{};
    SnapshotCamera camera{};
    /** Overrides `lighting`: every surface at full light. */
    bool fullbright{};
    SnapshotLighting lighting{ SnapshotLighting::sectorAmbient };
    /**
     * Baked shading only: the hardware-renderer tint of each colormap slot
     * (Level::colormaps order). Slot 0, the master colormap, is never
     * tinted; a sector whose slot has no entry here is drawn untinted.
     */
    std::vector<ColorF> colormapTints{};
    bool cullBackFaces{};
    // Candidate raster samples, charged before each triangle pixel loop.
    // Legacy callers retain their original unbounded behavior.
    std::size_t maxRasterSamples{ std::numeric_limits<std::size_t>::max() };
    bool indexed8bit{};
    // Slot zero supplies the display palette; each sector supplies the light-table slot.
    std::vector<assets::CmpFile> indexedColormaps{};
    // Authored CMP slot names (needed for JED/native sectors with name-only bindings).
    std::vector<std::string> indexedColormapNames{};
};

struct SoftwareSnapshotResult
{
    std::vector<std::uint8_t> png{};
    int surfacesDrawn{};
    int trianglesDrawn{};
    int materialsUsed{};
    int materialsMissing{};
    // Adjoins skipped for geo=0 or no material; retained material names
    // do not make hidden portals visible or count as missing materials.
    int portalsSkipped{};
    int thingSurfacesSubmitted{};
    int thingTrianglesSubmitted{};
    std::size_t rasterSamples{};
};

/** Renders the level's world surfaces with a small CPU rasterizer
 * (z-buffered, perspective-correct UVs, near-plane clipping) and
 * returns a PNG. Scope, deliberately narrow and reported by callers:
 * world surfaces only (no things, models, sprites or dynamic
 * lights); static sector ambient is the only light unless
 * fullbright is set. Textures use mip 0 with nearest sampling.
 * Geometry mode zero is hidden, including material-bearing adjoins.
 * Translucent world faces (face flag 0x2) are drawn last, farthest first,
 * blended over what lies behind at the engine's 90/255, testing depth
 * without writing it, so a mirror floor or a window shows what it covers.
 * Remaining nonzero geometry modes use this filled rasterization
 * approximation; dedicated vertex/wireframe styles and engine-specific
 * Solid-versus-Textured shading are not implemented. Orthographic
 * depth uses view-space Z; perspective depth uses reciprocal Z.
 * Raster bounds are intersected with the viewport before integer
 * conversion, including finite projections beyond the integer range.
 * Throws std::invalid_argument for bad dimensions/camera and
 * std::runtime_error for invalid geometry indices or non-finite
 * projected geometry/raster arithmetic. */
SoftwareSnapshotResult renderSoftwareSnapshot(const Level& level,
                                              const SnapshotMaterialResolver& materials,
                                              const SoftwareSnapshotOptions& options);

/** Adds transient static Thing geometry to the same depth buffer. Counters
 * are submitted polygons/triangles, not visible-Thing or gameplay claims.
 * Each geometry uses its own UV convention. Double-sided flags are honored
 * only for this opt-in geometry, preserving the legacy world pass. */
SoftwareSnapshotResult renderSoftwareSnapshot(const Level& level,
                                              const SnapshotMaterialResolver& materials,
                                              const SoftwareSnapshotOptions& options,
                                              std::span<const SnapshotGeometry> things);

/** Encodes an RGBA8 image (width*height*4 bytes) as a PNG with
 * stored (uncompressed) zlib blocks. */
std::vector<std::uint8_t> encodePngRgba8(int width, int height,
                                         const std::vector<std::uint8_t>& rgba);

} // namespace sed::render
