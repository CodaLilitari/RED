#include "sed/level/surface_flag_repair.hpp"

#include "sed/level/geometry.hpp"

namespace sed::level
{
namespace
{

Surface* surfaceAt(Level& level, const SurfaceRef ref) noexcept
{
    if ( ref.sector < 0 || ref.surface < 0
        || static_cast<std::size_t>(ref.sector) >= level.sectors.size() )
    {
        return nullptr;
    }
    Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    if ( static_cast<std::size_t>(ref.surface) >= sector.surfaces.size() )
    {
        return nullptr;
    }
    return &sector.surfaces[static_cast<std::size_t>(ref.surface)];
}

} // namespace

std::size_t levelFixSolidSurfaces(Level& level, const std::span<const SurfaceRef> surfaces) noexcept
{
    std::size_t changed = 0;
    for ( const SurfaceRef ref : surfaces )
    {
        Surface* const surface = surfaceAt(level, ref);
        if ( !surface || surface->adjoin )
        {
            continue;
        }
        const std::int32_t oldGeo = surface->geo;
        const std::int32_t oldFlags = surface->surfaceFlags;
        surface->geo = static_cast<std::int32_t>(TGeoMode::Texture);
        surface->surfaceFlags |= SF_Collision;
        surfaceCheckIfFloor(*surface);
        if ( surface->geo != oldGeo || surface->surfaceFlags != oldFlags )
        {
            ++changed;
        }
    }
    return changed;
}

std::size_t levelFixPassableAdjoins(Level& level, const std::span<const SurfaceRef> surfaces) noexcept
{
    std::size_t changed = 0;
    for ( const SurfaceRef ref : surfaces )
    {
        Surface* const surface = surfaceAt(level, ref);
        if ( !surface || !surface->adjoin )
        {
            continue;
        }
        const std::int32_t oldGeo = surface->geo;
        const std::int32_t oldFlags = surface->adjoinFlags;
        surface->geo = static_cast<std::int32_t>(TGeoMode::NotDrawn);
        surface->adjoinFlags = (SAF_Visible | SAF_Move)
            | (surface->adjoinFlags & ~(SAF_NoAiMove | SAF_NoPlayerMove));
        if ( surface->geo != oldGeo || surface->adjoinFlags != oldFlags )
        {
            ++changed;
        }
    }
    return changed;
}

} // namespace sed::level
