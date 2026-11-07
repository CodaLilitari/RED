#pragma once

#include "sed/geometry.hpp"
#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

namespace sed::level
{

/** Aggregated level measurements (E-01, the Statter/Statman plug-ins).
 *
 * Counts and sums never retarget references: this is a read-only
 * aggregation over the level as authored. `cogs` counts the level's
 * script list and is level-wide by nature; every other field is scoped
 * by the sector filter of `levelComputeStatistics`. */
struct LevelStatistics
{
    std::size_t sectors{};
    std::size_t surfaces{};
    std::size_t vertices{};
    std::size_t texVertices{};
    std::size_t things{};
    std::size_t lights{};
    std::size_t cogs{};
    std::size_t adjoinedSurfaces{};
    std::size_t skySurfaces{};
    std::size_t fullyLitSurfaces{};
    /** Sum of polygon areas over the scoped surfaces. */
    double surfaceArea{};
    /** Bounds over the scoped sectors' vertices; `hasBounds` is false
     * when the scope holds no vertices. */
    TBox bounds{};
    bool hasBounds{};
    /** Scoped sector count per Sector::layer value. */
    std::map<std::int32_t, std::size_t> sectorsByLayer{};
};

/** Aggregates statistics over `sectorFilter` (empty = whole level).
 *
 * Unknown or duplicate filter entries are ignored. Things count when
 * their `Thing::sector` is in scope; editor lights count when
 * `levelFindSectorForXYZ` places their position in scope (lights in no
 * sector count only in the whole-level scope). Surface area uses the
 * fan-triangulated polygon area; surfaces with invalid vertex
 * references contribute nothing. Pure read: no level mutation. */
[[nodiscard]] LevelStatistics levelComputeStatistics(
    const Level& level,
    std::span<const std::int32_t> sectorFilter = {});

} // namespace sed::level
