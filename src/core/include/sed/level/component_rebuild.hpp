#pragma once

#include "sed/level.hpp"

#include <cstdint>
#include <span>

namespace sed::level
{

enum class ComponentRebuildError
{
    None,
    TooFewElements,
    InvalidReference,
    MixedSectors,
    Degenerate,
    DuplicateSurface,
    AllocationFailure,
};

struct BuildSurfaceResult
{
    ComponentRebuildError error{ ComponentRebuildError::None };
    SurfaceRef surface{ -1, -1 };
};

struct BuildSectorResult
{
    ComponentRebuildError error{ ComponentRebuildError::None };
    std::int32_t sector{ -1 };
    bool sourceSectorDeleted{};
};

/** Legacy SED Build Surface (Alt+B with vertices selected).
 *
 * Appends one surface to `sector` from at least three distinct sector vertex
 * indices. The editor's vertex selection is unordered, so the corners are
 * ordered by angle around their centroid in the best-fit plane and wound so
 * the normal points into the sector, the SED convention. The new surface uses
 * the same defaults as other newly created surfaces. COG surface references
 * keep pointing at the same faces. On error the level is unchanged.
 */
BuildSurfaceResult levelBuildSurfaceFromVertices(
    Level& level,
    std::int32_t sector,
    std::span<const std::int32_t> vertices) noexcept;

/** Legacy SED Build Sector (Alt+B with surfaces selected).
 *
 * Moves at least four surfaces of one sector into a new sector that copies the
 * source sector's properties. Moved surfaces keep their properties, UVs and
 * adjoins; reciprocal adjoins and COG references are redirected. Vertices that
 * only the moved surfaces used are removed from the source. When the source is
 * left with no surfaces it is deleted, as in the legacy command, and the
 * returned index accounts for that. On error the level is unchanged.
 */
BuildSectorResult levelBuildSectorFromSurfaces(Level& level, std::span<const SurfaceRef> surfaces) noexcept;

} // namespace sed::level
