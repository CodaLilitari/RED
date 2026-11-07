#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

#include "sed/geometry.hpp"
#include "sed/level.hpp"

namespace sed::assets
{

/** One DXF 3DFACE in the RED interchange profile (E-13).
 *
 * Corners are world-space in the Level's own axes (X, Y, Z as
 * authored; no axis conversion is applied or promised). A triangle
 * repeats its third corner as the fourth, per DXF convention, and
 * reports `cornerCount` 3; a true quad reports 4. `layer` is the
 * DXF layer name, `SECTOR_<index>` for faces exported from a Level. */
struct DxfFace
{
    std::array<TVector, 4> corners{};
    std::size_t cornerCount{ 3 };
    std::string layer{};
};

/** Exports every Level surface as DXF 3DFACE entities.
 *
 * Surfaces are fan-triangulated in corner order, so an n-corner
 * surface yields n-2 faces on layer `SECTOR_<sector index>`.
 * Invalid surfaces (too few corners, out-of-range vertices) are
 * skipped. The output is an ASCII DXF with a single ENTITIES
 * section; UVs, materials, and adjoins are outside this profile. */
std::string saveLevelToDxfText(const Level& level);

/** Reads the 3DFACE entities of a DXF stream.
 *
 * Other entity types are skipped. Group codes and values are read
 * as code/value line pairs; a face missing any corner coordinate is
 * skipped rather than guessed. Throws std::runtime_error when the
 * stream cannot be read. */
std::vector<DxfFace> loadDxfFaces(std::istream& in);
std::vector<DxfFace> loadDxfFaces(const std::filesystem::path& path);

} // namespace sed::assets
