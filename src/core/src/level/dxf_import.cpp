#include "sed/level/dxf_import.hpp"

#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sed::level
{
namespace
{

// Builds one validated prism sector from a CCW footprint, or nothing.
std::optional<Sector> buildPrismSector(const Level& level,
    const std::vector<TVector>& footprint, const double floorZ, const double height)
{
    const std::size_t count = footprint.size();
    Sector sector{};
    for (const TVector& corner : footprint)
    {
        sector.vertices.push_back(Vertex{ corner.x, corner.y, floorZ });
    }
    for (const TVector& corner : footprint)
    {
        sector.vertices.push_back(Vertex{ corner.x, corner.y, floorZ + height });
    }
    const auto ringIndex = [count](const std::size_t i, const bool top)
    {
        return static_cast<std::int32_t>(i + (top ? count : 0u));
    };
    std::vector<std::vector<std::int32_t>> loops;
    std::vector<std::int32_t> floorLoop;
    std::vector<std::int32_t> ceilingLoop;
    for (std::size_t i = 0; i < count; ++i)
    {
        floorLoop.push_back(ringIndex(i, false));
        ceilingLoop.push_back(ringIndex(count - 1 - i, true));
    }
    loops.push_back(floorLoop);
    loops.push_back(ceilingLoop);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t next = (i + 1) % count;
        loops.push_back({ ringIndex(i, false), ringIndex(next, false),
            ringIndex(next, true), ringIndex(i, true) });
    }

    TVector centroid{};
    for (const Vertex& vertex : sector.vertices)
    {
        centroid.x += vertex.x;
        centroid.y += vertex.y;
        centroid.z += vertex.z;
    }
    const double invCount = 1.0 / static_cast<double>(sector.vertices.size());
    centroid.x *= invCount;
    centroid.y *= invCount;
    centroid.z *= invCount;

    const double ppu = levelPixelPerUnit(level);
    for (std::vector<std::int32_t>& loop : loops)
    {
        Surface surface{};
        surface.material = "dflt.mat";
        surface.vertices = loop;
        surface.texVertices.assign(loop.size(), TexVertex{});
        surfaceRecalcNewAll(ppu, sector, surface, 0);
        // Sector surface normals face into the room: flip the loop when
        // the recomputed normal points away from the centroid instead.
        const Vertex& any = sector.vertices[static_cast<std::size_t>(loop[0])];
        const TVector inward = TVector::Create(
            centroid.x - any.x, centroid.y - any.y, centroid.z - any.z);
        if (surface.normal.Dot(inward) < 0.0)
        {
            std::reverse(surface.vertices.begin(), surface.vertices.end());
            surfaceRecalcNewAll(ppu, sector, surface, 0);
        }
        surface.num = static_cast<std::int32_t>(sector.surfaces.size());
        sector.surfaces.push_back(std::move(surface));
    }

    if (!sectorIsConvex(sector))
    {
        return std::nullopt;
    }
    for (const Surface& surface : sector.surfaces)
    {
        if (!surfaceIsPlanar(sector, surface) || !surfaceIsConvex(sector, surface))
        {
            return std::nullopt;
        }
    }
    return sector;
}

} // namespace

std::optional<DxfSectorImportResult> levelImportDxfFaces(
    Level& level,
    const std::span<const assets::DxfFace> faces,
    const double height)
{
    if (!std::isfinite(height) || height <= 0.0 || height > 1000.0)
    {
        return std::nullopt;
    }
    Level candidate = level;
    DxfSectorImportResult result{};
    std::vector<std::int32_t> imported{};
    for (const assets::DxfFace& face : faces)
    {
        if (face.cornerCount < 3u || face.cornerCount > 4u)
        {
            ++result.skippedFaces;
            continue;
        }
        std::vector<TVector> footprint(face.corners.begin(),
            face.corners.begin() + static_cast<std::ptrdiff_t>(face.cornerCount));
        bool usable = true;
        for (const TVector& corner : footprint)
        {
            usable = usable && std::isfinite(corner.x) && std::isfinite(corner.y)
                && std::isfinite(corner.z)
                && std::abs(corner.z - footprint[0].z) <= 1e-9;
        }
        if (!usable)
        {
            ++result.skippedFaces;
            continue;
        }
        double twiceArea = 0.0;
        for (std::size_t i = 0; i < footprint.size(); ++i)
        {
            const TVector& a = footprint[i];
            const TVector& b = footprint[(i + 1) % footprint.size()];
            twiceArea += a.x * b.y - b.x * a.y;
        }
        if (std::abs(twiceArea) < 1e-12)
        {
            ++result.skippedFaces;
            continue;
        }
        if (twiceArea < 0.0)
        {
            std::reverse(footprint.begin(), footprint.end());
        }
        std::optional<Sector> sector = buildPrismSector(candidate, footprint, footprint[0].z, height);
        if (!sector)
        {
            ++result.skippedFaces;
            continue;
        }
        const auto index = static_cast<std::int32_t>(candidate.sectors.size());
        candidate.sectors.push_back(std::move(*sector));
        for (const std::int32_t previous : imported)
        {
            (void)levelConnectSectors(candidate, previous, index);
        }
        imported.push_back(index);
        result.sectors.push_back(index);
    }
    level = std::move(candidate);
    return result;
}

} // namespace sed::level
