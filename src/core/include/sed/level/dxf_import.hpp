#pragma once

#include "sed/assets/dxf.hpp"
#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sed::level
{

/** Outcome of a DXF face import (E-13). */
struct DxfSectorImportResult
{
    /** New sector indices, in face order. */
    std::vector<std::int32_t> sectors{};
    /** Faces that were not compiled (non-horizontal, degenerate, or
     * failing sector validity), counted rather than guessed. */
    std::size_t skippedFaces{};
};

/**
 * Compiles DXF 3DFACE footprints into extruded sectors (E-13, the
 * scoped DXF-to-sector slice of the ConvJED/UltraEdit family).
 *
 * Every horizontal face (all corners at one Z) with a non-degenerate
 * triangle or quad footprint becomes one prism sector: floor on the
 * face plane, ceiling `height` above it, one wall per footprint edge,
 * default material, recomputed normals/UVs. Imported sectors that
 * share a wall are adjoined to each other (a failed adjoin keeps both
 * sectors, unadjoined). Non-horizontal faces, degenerate footprints
 * and sectors failing convexity are skipped and counted in
 * `skippedFaces`. Nothing is appended for a non-finite, non-positive
 * or over-1000 height: the call returns std::nullopt and the Level is
 * unchanged. An empty face list is valid and imports nothing. */
[[nodiscard]] std::optional<DxfSectorImportResult> levelImportDxfFaces(
    Level& level,
    std::span<const assets::DxfFace> faces,
    double height);

} // namespace sed::level
