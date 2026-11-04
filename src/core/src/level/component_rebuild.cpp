#include "sed/level/component_rebuild.hpp"

#include "cog_reference_utils.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace sed::level
{
namespace
{

// Matches the defaults the shape builders give newly created surfaces.
void initNewSurfaceDefaults(const ProjectType projectType, Surface& surface)
{
    surface.adjoinFlags = SAF_Visible | SAF_Move;
    if ( projectType != ProjectType::IJIM )
    {
        surface.adjoinFlags |= SAF_AllowSoundPass;
    }
    surface.material = "dflt.mat";
    surface.surfaceFlags = SF_Collision;
    surface.faceFlags = 0;
    surface.geo = static_cast<std::int32_t>(TGeoMode::Texture);
    surface.light = static_cast<std::int32_t>(TLightMode::Gouraud);
    surface.tex = projectType == ProjectType::IJIM ? 3 : 1;
    surface.extraLight = ColorBlack;
    surface.uScale = 1.0f;
    surface.vScale = 1.0f;
    surface.adjoin.reset();
    surface.nAdj = -1;
    surface.nMat = -1;
}

void copySectorProperties(const Sector& source, Sector& destination)
{
    destination.flags = source.flags;
    destination.ambient = source.ambient;
    destination.extraLight = source.extraLight;
    destination.pointLight = source.pointLight;
    destination.colormap = source.colormap;
    destination.colormapIndex = source.colormapIndex;
    destination.tint = source.tint;
    destination.sound = source.sound;
    destination.soundVolume = source.soundVolume;
    destination.thrust = source.thrust;
    destination.collideBox = source.collideBox;
    destination.layer = source.layer;
}

bool validSector(const Level& level, const std::int32_t sector) noexcept
{
    return sector >= 0 && static_cast<std::size_t>(sector) < level.sectors.size();
}

TVector position(const Vertex& vertex) noexcept
{
    return TVector{ vertex.x, vertex.y, vertex.z };
}

// Orders an unordered corner set into a simple polygon around its centroid.
// Returns an empty list when the points do not span a plane.
std::vector<std::int32_t> orderCorners(const Sector& sector, std::vector<std::int32_t> corners)
{
    TVector centroid{};
    for ( const std::int32_t index : corners )
    {
        centroid = centroid + position(sector.vertices[static_cast<std::size_t>(index)]);
    }
    centroid = centroid * (1.0 / static_cast<double>(corners.size()));

    TVector axisU{};
    double radius = 0.0;
    for ( const std::int32_t index : corners )
    {
        const TVector offset = position(sector.vertices[static_cast<std::size_t>(index)]) - centroid;
        if ( offset.length() > radius )
        {
            radius = offset.length();
            axisU = offset;
        }
    }
    TVector normal{};
    for ( const std::int32_t index : corners )
    {
        const TVector candidate = axisU.cross(position(sector.vertices[static_cast<std::size_t>(index)]) - centroid);
        if ( candidate.length() > normal.length() )
        {
            normal = candidate;
        }
    }
    constexpr double kRelativeAreaEpsilon = 1e-9;
    if ( radius <= 0.0 || normal.length() <= kRelativeAreaEpsilon * radius * radius )
    {
        return {};
    }
    axisU.normalize();
    normal.normalize();
    const TVector axisV = normal.cross(axisU);

    std::vector<std::pair<double, std::int32_t>> angles{};
    angles.reserve(corners.size());
    for ( const std::int32_t index : corners )
    {
        const TVector offset = position(sector.vertices[static_cast<std::size_t>(index)]) - centroid;
        angles.emplace_back(std::atan2(offset.dot(axisV), offset.dot(axisU)), index);
    }
    std::sort(angles.begin(), angles.end());
    corners.clear();
    for ( const auto& [angle, index] : angles )
    {
        corners.push_back(index);
    }
    return corners;
}

} // namespace

BuildSurfaceResult levelBuildSurfaceFromVertices(
    Level& level,
    const std::int32_t sectorIndex,
    const std::span<const std::int32_t> vertices) noexcept
{
    try
    {
        if ( !validSector(level, sectorIndex) )
        {
            return { ComponentRebuildError::InvalidReference, {} };
        }
        Sector& sector = level.sectors[static_cast<std::size_t>(sectorIndex)];
        std::vector<std::int32_t> corners{};
        for ( const std::int32_t index : vertices )
        {
            if ( index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size() )
            {
                return { ComponentRebuildError::InvalidReference, {} };
            }
            if ( std::find(corners.begin(), corners.end(), index) == corners.end() )
            {
                corners.push_back(index);
            }
        }
        if ( corners.size() < 3u )
        {
            return { ComponentRebuildError::TooFewElements, {} };
        }

        std::vector<std::int32_t> sortedCorners = corners;
        std::sort(sortedCorners.begin(), sortedCorners.end());
        for ( const Surface& existing : sector.surfaces )
        {
            std::vector<std::int32_t> existingCorners = existing.vertices;
            std::sort(existingCorners.begin(), existingCorners.end());
            existingCorners.erase(std::unique(existingCorners.begin(), existingCorners.end()), existingCorners.end());
            if ( existingCorners == sortedCorners )
            {
                return { ComponentRebuildError::DuplicateSurface, {} };
            }
        }

        Surface surface{};
        initNewSurfaceDefaults(level.kind, surface);
        surface.vertices = orderCorners(sector, std::move(corners));
        if ( surface.vertices.empty() )
        {
            return { ComponentRebuildError::Degenerate, {} };
        }
        surface.texVertices.resize(surface.vertices.size());

        // SED face normals point into the sector volume.
        const TVector sectorCenter = sectorCalcCenter(sector);
        TVector surfaceCenter{};
        if ( surfaceCalcCenter(sector, surface, surfaceCenter)
            && surfaceCalcNormalNewell(sector, surface).dot(sectorCenter - surfaceCenter) < 0.0 )
        {
            std::reverse(surface.vertices.begin(), surface.vertices.end());
        }

        detail::GeometryCogReferences references(level);
        sector.surfaces.reserve(sector.surfaces.size() + 1u);
        surface.num = static_cast<std::int32_t>(sector.surfaces.size());
        const std::int32_t surfaceIndex = sector.addSurface(std::move(surface));
        surfaceRecalcNewAll(levelPixelPerUnit(level), sector,
            sector.surfaces[static_cast<std::size_t>(surfaceIndex)], 0);
        references.publish(level);
        return { ComponentRebuildError::None, SurfaceRef{ sectorIndex, surfaceIndex } };
    }
    catch (...)
    {
        return { ComponentRebuildError::AllocationFailure, {} };
    }
}

BuildSectorResult levelBuildSectorFromSurfaces(Level& level, const std::span<const SurfaceRef> surfaces) noexcept
{
    try
    {
        std::vector<SurfaceRef> selected{};
        for ( const SurfaceRef& ref : surfaces )
        {
            if ( !validSector(level, ref.sector) || ref.surface < 0
                || static_cast<std::size_t>(ref.surface)
                    >= level.sectors[static_cast<std::size_t>(ref.sector)].surfaces.size() )
            {
                return { ComponentRebuildError::InvalidReference, -1, false };
            }
            if ( std::find(selected.begin(), selected.end(), ref) == selected.end() )
            {
                selected.push_back(ref);
            }
        }
        if ( selected.size() < 4u )
        {
            return { ComponentRebuildError::TooFewElements, -1, false };
        }
        const std::int32_t sourceIndex = selected.front().sector;
        for ( const SurfaceRef& ref : selected )
        {
            if ( ref.sector != sourceIndex )
            {
                return { ComponentRebuildError::MixedSectors, -1, false };
            }
        }

        const Sector& source = level.sectors[static_cast<std::size_t>(sourceIndex)];
        std::vector<bool> moved(source.surfaces.size(), false);
        for ( const SurfaceRef& ref : selected )
        {
            moved[static_cast<std::size_t>(ref.surface)] = true;
        }

        // Stage both sectors' new contents before touching the level.
        const auto newSectorIndex = static_cast<std::int32_t>(level.sectors.size());
        std::vector<std::optional<SurfaceRef>> surfaceRemap(source.surfaces.size());
        std::vector<bool> keptVertexUse(source.vertices.size(), false);
        std::vector<bool> movedVertexUse(source.vertices.size(), false);
        for ( std::size_t i = 0; i < source.surfaces.size(); ++i )
        {
            for ( const std::int32_t index : source.surfaces[i].vertices )
            {
                if ( index >= 0 && static_cast<std::size_t>(index) < source.vertices.size() )
                {
                    (moved[i] ? movedVertexUse : keptVertexUse)[static_cast<std::size_t>(index)] = true;
                }
            }
        }

        Sector built{};
        copySectorProperties(source, built);
        std::vector<std::int32_t> builtVertexMap(source.vertices.size(), -1);
        std::vector<Surface> keptSurfaces{};
        for ( std::size_t i = 0; i < source.surfaces.size(); ++i )
        {
            Surface face = source.surfaces[i];
            if ( !moved[i] )
            {
                surfaceRemap[i] = SurfaceRef{ sourceIndex, static_cast<std::int32_t>(keptSurfaces.size()) };
                keptSurfaces.push_back(std::move(face));
                continue;
            }
            for ( std::int32_t& index : face.vertices )
            {
                const auto old = static_cast<std::size_t>(index);
                if ( builtVertexMap[old] < 0 )
                {
                    builtVertexMap[old] = static_cast<std::int32_t>(built.vertices.size());
                    built.vertices.push_back(source.vertices[old]);
                }
                index = builtVertexMap[old];
            }
            surfaceRemap[i] = SurfaceRef{ newSectorIndex, static_cast<std::int32_t>(built.surfaces.size()) };
            face.num = static_cast<std::int32_t>(built.surfaces.size());
            built.surfaces.push_back(std::move(face));
        }

        // Only vertices orphaned by this command leave the source sector.
        std::vector<std::int32_t> keptVertexMap(source.vertices.size(), -1);
        std::vector<Vertex> keptVertices{};
        for ( std::size_t i = 0; i < source.vertices.size(); ++i )
        {
            if ( movedVertexUse[i] && !keptVertexUse[i] )
            {
                continue;
            }
            keptVertexMap[i] = static_cast<std::int32_t>(keptVertices.size());
            keptVertices.push_back(source.vertices[i]);
        }
        for ( std::size_t i = 0; i < keptSurfaces.size(); ++i )
        {
            keptSurfaces[i].num = static_cast<std::int32_t>(i);
            for ( std::int32_t& index : keptSurfaces[i].vertices )
            {
                index = keptVertexMap[static_cast<std::size_t>(index)];
            }
        }

        detail::GeometryCogReferences references(level);
        references.splitSector(sourceIndex, surfaceRemap);
        level.sectors.reserve(level.sectors.size() + 1u);
        Sector& added = level.newSector();
        built.id = added.id;
        built.num = added.num;
        added = std::move(built);

        // Nothing below allocates, so the level cannot be left half-built.
        Sector& sourceSector = level.sectors[static_cast<std::size_t>(sourceIndex)];
        sourceSector.surfaces = std::move(keptSurfaces);
        sourceSector.vertices = std::move(keptVertices);
        for ( Sector& sector : level.sectors )
        {
            for ( Surface& face : sector.surfaces )
            {
                if ( face.adjoin && face.adjoin->sector == sourceIndex && face.adjoin->surface >= 0
                    && static_cast<std::size_t>(face.adjoin->surface) < surfaceRemap.size() )
                {
                    face.adjoin = surfaceRemap[static_cast<std::size_t>(face.adjoin->surface)];
                }
            }
        }
        references.publish(level);

        if ( sourceSector.surfaces.empty() && levelDeleteSector(level, sourceIndex) )
        {
            return { ComponentRebuildError::None, newSectorIndex - 1, true };
        }
        return { ComponentRebuildError::None, newSectorIndex, false };
    }
    catch (...)
    {
        return { ComponentRebuildError::AllocationFailure, -1, false };
    }
}

} // namespace sed::level
