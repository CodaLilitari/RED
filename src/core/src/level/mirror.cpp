#include "sed/level/mirror.hpp"

#include "sed/geometry.hpp"
#include "sed/level/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace sed::level
{
namespace
{

constexpr double kPlaneEpsilon = 1e-6;

bool finiteVector(const TVector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

TVector vertexAt(const Sector& sector, const std::int32_t index) noexcept
{
    const Vertex& v = sector.vertices[static_cast<std::size_t>(index)];
    return TVector{ v.x, v.y, v.z };
}

bool sectorIndicesValid(const Sector& sector) noexcept
{
    for ( const Surface& surface : sector.surfaces )
    {
        if ( surface.texVertices.size() != surface.vertices.size() )
        {
            return false;
        }
        for ( const std::int32_t index : surface.vertices )
        {
            if ( index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size() )
            {
                return false;
            }
        }
    }
    return true;
}

TVector unitNormal(const Sector& sector, const Surface& surface) noexcept
{
    const TVector n = surfaceCalcNormalNewell(sector, surface);
    const double length = n.Length();
    return length > 1e-12 && std::isfinite(length) ? n * (1.0 / length) : TVector{};
}

double polygonArea(const Sector& sector, const Surface& surface) noexcept
{
    return surfaceCalcNormalNewell(sector, surface).Length() * 0.5;
}

// The height of a flat, horizontal face that looks up into its room.
std::optional<double> floorPlane(const Sector& room, const Surface& floor) noexcept
{
    if ( floor.vertices.size() < 3 )
    {
        return std::nullopt;
    }
    const double z0 = room.vertices[static_cast<std::size_t>(floor.vertices.front())].z;
    for ( const std::int32_t index : floor.vertices )
    {
        if ( std::abs(room.vertices[static_cast<std::size_t>(index)].z - z0) > kPlaneEpsilon )
        {
            return std::nullopt;
        }
    }
    if ( unitNormal(room, floor).z < 0.999 )
    {
        return std::nullopt;
    }
    return z0;
}

// An affine texture map read off a face: any point in the face's plane gets
// the coordinates the face's own mapping would give it there.
struct AffineUv
{
    TVector origin{};
    TVector e1{};
    TVector e2{};
    double g11{}, g12{}, g22{}, det{};
    double u0{}, v0{}, du1{}, dv1{}, du2{}, dv2{};

    std::pair<float, float> at(const TVector& p) const noexcept
    {
        const TVector d = p - origin;
        const double r1 = d.Dot(e1);
        const double r2 = d.Dot(e2);
        const double a = (r1 * g22 - r2 * g12) / det;
        const double b = (r2 * g11 - r1 * g12) / det;
        return { static_cast<float>(u0 + a * du1 + b * du2), static_cast<float>(v0 + a * dv1 + b * dv2) };
    }
};

std::optional<AffineUv> affineUvOf(const Sector& sector, const Surface& surface) noexcept
{
    const std::size_t n = surface.vertices.size();
    if ( n < 3 || surface.texVertices.size() != n )
    {
        return std::nullopt;
    }
    // The widest corner triangle from corner 0 conditions the solve best.
    const TVector p0 = vertexAt(sector, surface.vertices[0]);
    std::size_t bestJ = 0, bestK = 0;
    double best = 0.0;
    for ( std::size_t j = 1; j < n; ++j )
    {
        for ( std::size_t k = j + 1; k < n; ++k )
        {
            const double area = (vertexAt(sector, surface.vertices[j]) - p0)
                .Cross(vertexAt(sector, surface.vertices[k]) - p0).Length();
            if ( area > best )
            {
                best = area;
                bestJ = j;
                bestK = k;
            }
        }
    }
    if ( !(best > 1e-12) )
    {
        return std::nullopt;
    }
    AffineUv map{};
    map.origin = p0;
    map.e1 = vertexAt(sector, surface.vertices[bestJ]) - p0;
    map.e2 = vertexAt(sector, surface.vertices[bestK]) - p0;
    map.g11 = map.e1.Dot(map.e1);
    map.g12 = map.e1.Dot(map.e2);
    map.g22 = map.e2.Dot(map.e2);
    map.det = map.g11 * map.g22 - map.g12 * map.g12;
    if ( !(std::abs(map.det) > 1e-24) || !std::isfinite(map.det) )
    {
        return std::nullopt;
    }
    map.u0 = surface.texVertices[0].u;
    map.v0 = surface.texVertices[0].v;
    map.du1 = surface.texVertices[bestJ].u - map.u0;
    map.dv1 = surface.texVertices[bestJ].v - map.v0;
    map.du2 = surface.texVertices[bestK].u - map.u0;
    map.dv2 = surface.texVertices[bestK].v - map.v0;
    return map;
}

bool coplanar(const Sector& sector, const Surface& a, const Surface& b) noexcept
{
    const TVector na = unitNormal(sector, a);
    const TVector nb = unitNormal(sector, b);
    if ( na.Dot(nb) < 0.9999 )
    {
        return false;
    }
    const TVector pa = vertexAt(sector, a.vertices.front());
    for ( const std::int32_t index : b.vertices )
    {
        if ( std::abs(na.Dot(vertexAt(sector, index) - pa)) > kPlaneEpsilon )
        {
            return false;
        }
    }
    return true;
}

ColorF scaledRgb(ColorF color, const double k) noexcept
{
    color.r = static_cast<float>(color.r * k);
    color.g = static_cast<float>(color.g * k);
    color.b = static_cast<float>(color.b * k);
    return color;
}

ColorF scaledCorner(ColorF color, const double k) noexcept
{
    color = scaledRgb(color, k);
    color.a = static_cast<float>(color.a * k);
    return color;
}

// Whether `mirror` is, face for face and corner for corner, the reflection of
// `room` across z = z0 that levelAddMirrorFloor builds, and adjoins nothing
// but the floor portal. Corners are compared by position, not by vertex
// index: a saved and reloaded level shares and renumbers vertices.
bool isReflectionOf(const Sector& room, const std::int32_t roomIndex, const std::int32_t floorIndex,
    const Sector& mirror, const double z0) noexcept
{
    if ( mirror.surfaces.size() != room.surfaces.size() )
    {
        return false;
    }
    for ( std::size_t s = 0; s < room.surfaces.size(); ++s )
    {
        const Surface& a = room.surfaces[s];
        const Surface& b = mirror.surfaces[s];
        const std::size_t n = a.vertices.size();
        if ( b.vertices.size() != n || b.texVertices.size() != n || a.texVertices.size() != n )
        {
            return false;
        }
        for ( std::size_t k = 0; k < n; ++k )
        {
            const Vertex& p = room.vertices[static_cast<std::size_t>(a.vertices[n - 1 - k])];
            const Vertex& q = mirror.vertices[static_cast<std::size_t>(b.vertices[k])];
            if ( std::abs(p.x - q.x) > kPlaneEpsilon || std::abs(p.y - q.y) > kPlaneEpsilon
                || std::abs((2.0 * z0 - p.z) - q.z) > kPlaneEpsilon )
            {
                return false;
            }
        }
        if ( static_cast<std::int32_t>(s) == floorIndex )
        {
            if ( !b.adjoin || b.adjoin->sector != roomIndex || b.adjoin->surface != floorIndex )
            {
                return false;
            }
        }
        else if ( b.adjoin )
        {
            return false;
        }
    }
    return true;
}

void fail(MirrorFloorReport* report, const MirrorFloorError error) noexcept
{
    if ( report )
    {
        *report = MirrorFloorReport{};
        report->error = error;
    }
}

} // namespace

bool levelAddMirrorFloor(Level& level, const SurfaceRef floorRef, const MirrorFloorOptions& options,
    MirrorFloorReport* report)
{
    if ( report )
    {
        *report = MirrorFloorReport{};
    }
    if ( floorRef.sector < 0 || static_cast<std::size_t>(floorRef.sector) >= level.sectors.size() )
    {
        fail(report, MirrorFloorError::InvalidSurface);
        return false;
    }
    const Sector& room = level.sectors[static_cast<std::size_t>(floorRef.sector)];
    if ( floorRef.surface < 0 || static_cast<std::size_t>(floorRef.surface) >= room.surfaces.size()
        || !sectorIndicesValid(room)
        || room.surfaces[static_cast<std::size_t>(floorRef.surface)].vertices.size() < 3 )
    {
        fail(report, MirrorFloorError::InvalidSurface);
        return false;
    }
    if ( !std::isfinite(options.reflectivity) || options.reflectivity < 0.0 || options.reflectivity > 1.0 )
    {
        fail(report, MirrorFloorError::InvalidReflectivity);
        return false;
    }
    if ( options.openingMaterialIndex < -1
        || (options.openingMaterialIndex >= 0
            && static_cast<std::size_t>(options.openingMaterialIndex) >= level.materials.size()) )
    {
        fail(report, MirrorFloorError::InvalidMaterial);
        return false;
    }
    const Surface& floor = room.surfaces[static_cast<std::size_t>(floorRef.surface)];
    if ( floor.adjoin )
    {
        fail(report, MirrorFloorError::Adjoined);
        return false;
    }
    const std::optional<double> plane = floorPlane(room, floor);
    if ( !plane )
    {
        fail(report, MirrorFloorError::NotFloor);
        return false;
    }
    const double z0 = *plane;
    for ( const Vertex& v : room.vertices )
    {
        if ( v.z < z0 - kPlaneEpsilon )
        {
            fail(report, MirrorFloorError::RoomBelowFloor);
            return false;
        }
    }

    // The room's most used wall material, for doorways with no wall beside them.
    std::optional<std::size_t> commonWall;
    {
        std::map<std::string, std::pair<std::size_t, std::size_t>> uses; // material -> (count, first face)
        for ( std::size_t s = 0; s < room.surfaces.size(); ++s )
        {
            const Surface& surface = room.surfaces[s];
            if ( surface.adjoin || surface.material.empty() || surface.geo == 0 || surface.vertices.size() < 3
                || std::abs(unitNormal(room, surface).z) >= 0.5 )
            {
                continue;
            }
            auto [it, inserted] = uses.try_emplace(surface.material, std::pair<std::size_t, std::size_t>{ 0, s });
            ++it->second.first;
        }
        std::size_t bestCount = 0;
        for ( const auto& [name, use] : uses )
        {
            if ( use.first > bestCount )
            {
                bestCount = use.first;
                commonWall = use.second;
            }
        }
    }

    Sector mirror{};
    mirror.flags = room.flags & ~SECF_CollideBox;
    mirror.ambient = scaledRgb(room.ambient, options.reflectivity);
    mirror.extraLight = scaledRgb(room.extraLight, options.reflectivity);
    mirror.pointLight = room.pointLight;
    mirror.colormap = room.colormap;
    mirror.colormapIndex = room.colormapIndex;
    mirror.tint = room.tint;
    mirror.layer = room.layer;
    mirror.vertices.reserve(room.vertices.size());
    for ( const Vertex& v : room.vertices )
    {
        Vertex reflected{ v.x, v.y, 2.0 * z0 - v.z, 0 };
        if ( !std::isfinite(reflected.z) )
        {
            fail(report, MirrorFloorError::NonFinite);
            return false;
        }
        mirror.vertices.push_back(reflected);
    }

    std::vector<std::int32_t> sealed;
    std::size_t continued = 0;
    mirror.surfaces.reserve(room.surfaces.size());
    for ( std::size_t s = 0; s < room.surfaces.size(); ++s )
    {
        const Surface& source = room.surfaces[s];
        Surface copy = source;
        copy.num = static_cast<std::int32_t>(s);
        copy.mark = 0;
        copy.adjoin.reset();
        copy.nAdj = -1;
        copy.adjoinFlags = 0;
        copy.extraLight = scaledRgb(source.extraLight, options.reflectivity);
        for ( TexVertex& corner : copy.texVertices )
        {
            corner.color = scaledCorner(corner.color, options.reflectivity);
        }

        if ( static_cast<std::int32_t>(s) != floorRef.surface && source.adjoin )
        {
            // A doorway: the copy has nothing beyond it, so it becomes a wall,
            // continued from a wall in its plane when the room has one.
            const Surface* donor = nullptr;
            double donorArea = 0.0;
            for ( const Surface& other : room.surfaces )
            {
                if ( &other == &source || other.adjoin || other.material.empty() || other.geo == 0
                    || other.vertices.size() < 3 || !coplanar(room, source, other) )
                {
                    continue;
                }
                const double area = polygonArea(room, other);
                if ( area > donorArea )
                {
                    donorArea = area;
                    donor = &other;
                }
            }
            const Surface* style = donor ? donor
                : commonWall ? &room.surfaces[*commonWall] : nullptr;
            if ( options.openingMaterialIndex >= 0 )
            {
                copy.material = level.materials[static_cast<std::size_t>(options.openingMaterialIndex)];
                copy.nMat = options.openingMaterialIndex;
            }
            else if ( style )
            {
                copy.material = style->material;
                copy.nMat = style->nMat;
            }
            else
            {
                fail(report, MirrorFloorError::NoOpeningMaterial);
                return false;
            }
            if ( style )
            {
                copy.geo = style->geo;
                copy.light = style->light;
                copy.tex = style->tex;
                copy.surfaceFlags = style->surfaceFlags & ~SF_Floor;
                copy.faceFlags = style->faceFlags;
                copy.uScale = style->uScale;
                copy.vScale = style->vScale;
            }
            else
            {
                copy.geo = 4;
                copy.light = 3;
                copy.tex = 1;
                copy.surfaceFlags = SF_Collision;
                copy.faceFlags = 0;
            }
            // Texture coordinates, in room space; the reversal below carries them.
            const std::optional<AffineUv> map = donor ? affineUvOf(room, *donor) : std::nullopt;
            const TVector n = unitNormal(room, source);
            TVector uAxis = std::abs(n.z) < 0.999 ? TVector::up.Cross(n) : TVector{ 1, 0, 0 };
            uAxis = uAxis * (1.0 / std::max(1e-12, uAxis.Length()));
            TVector vAxis = n.Cross(uAxis);
            if ( vAxis.z > 0.0 )
            {
                vAxis = vAxis * -1.0;
            }
            const double scale = level.uvCoordinateSpace == UvCoordinateSpace::TexturePixels
                ? level.ppunit : level.ppunit / 64.0;
            for ( std::size_t k = 0; k < source.vertices.size(); ++k )
            {
                const TVector p = vertexAt(room, source.vertices[k]);
                if ( map )
                {
                    const auto [u, v] = map->at(p);
                    copy.texVertices[k].u = u;
                    copy.texVertices[k].v = v;
                }
                else
                {
                    copy.texVertices[k].u = static_cast<float>(p.Dot(uAxis) * scale);
                    copy.texVertices[k].v = static_cast<float>(p.Dot(vAxis) * scale);
                }
            }
            if ( map )
            {
                ++continued;
            }
            sealed.push_back(static_cast<std::int32_t>(s));
        }

        std::reverse(copy.vertices.begin(), copy.vertices.end());
        std::reverse(copy.texVertices.begin(), copy.texVertices.end());
        mirror.surfaces.push_back(std::move(copy));
    }
    for ( Surface& surface : mirror.surfaces )
    {
        surface.normal = surfaceCalcNormalNewell(mirror, surface);
        if ( !finiteVector(surface.normal) )
        {
            fail(report, MirrorFloorError::NonFinite);
            return false;
        }
    }
    for ( const Surface& surface : mirror.surfaces )
    {
        for ( const TexVertex& corner : surface.texVertices )
        {
            if ( !std::isfinite(corner.u) || !std::isfinite(corner.v) )
            {
                fail(report, MirrorFloorError::NonFinite);
                return false;
            }
        }
    }

    // Commit on a copy so a throw (ID exhaustion) leaves the level untouched.
    Level candidate = level;
    const auto mirrorIndex = static_cast<std::int32_t>(candidate.sectors.size());
    {
        Sector& allocated = candidate.newSector();
        const std::int32_t id = allocated.id;
        const std::int32_t num = allocated.num;
        allocated = std::move(mirror);
        allocated.id = id;
        allocated.num = num;
    }
    Surface& top = candidate.sectors[static_cast<std::size_t>(mirrorIndex)]
        .surfaces[static_cast<std::size_t>(floorRef.surface)];
    top.adjoin = SurfaceRef{ floorRef.sector, floorRef.surface };
    top.nAdj = -1;
    top.adjoinFlags = SAF_Visible;
    top.geo = 0;
    top.material.clear();
    top.nMat = -1;
    top.faceFlags = 0;
    top.surfaceFlags = 0;

    // The floor: a translucent portal that holds the player up.
    Surface& walkOn = candidate.sectors[static_cast<std::size_t>(floorRef.sector)]
        .surfaces[static_cast<std::size_t>(floorRef.surface)];
    walkOn.adjoin = SurfaceRef{ mirrorIndex, floorRef.surface };
    walkOn.nAdj = -1;
    walkOn.adjoinFlags = SAF_Visible;
    walkOn.faceFlags |= FF_Transluent;
    walkOn.surfaceFlags |= SF_Floor | SF_Collision;
    if ( walkOn.geo == 0 )
    {
        walkOn.geo = 4;
    }

    level = std::move(candidate);
    if ( report )
    {
        report->mirrorSector = mirrorIndex;
        report->mirrorSurface = floorRef.surface;
        report->sealedOpenings = std::move(sealed);
        report->openingsContinued = continued;
    }
    return true;
}

std::vector<MirrorFloorPortal> levelFindMirrorFloors(const Level& level)
{
    std::vector<MirrorFloorPortal> found;
    for ( std::size_t r = 0; r < level.sectors.size(); ++r )
    {
        const Sector& room = level.sectors[r];
        if ( !sectorIndicesValid(room) )
        {
            continue;
        }
        for ( std::size_t f = 0; f < room.surfaces.size(); ++f )
        {
            const Surface& floor = room.surfaces[f];
            if ( !floor.adjoin || (floor.faceFlags & FF_Transluent) == 0 || (floor.adjoinFlags & SAF_Move) != 0 )
            {
                continue;
            }
            const SurfaceRef far = *floor.adjoin;
            if ( far.sector < 0 || static_cast<std::size_t>(far.sector) >= level.sectors.size()
                || far.sector == static_cast<std::int32_t>(r) || far.surface != static_cast<std::int32_t>(f) )
            {
                continue;
            }
            const std::optional<double> plane = floorPlane(room, floor);
            const Sector& mirror = level.sectors[static_cast<std::size_t>(far.sector)];
            if ( !plane || !sectorIndicesValid(mirror)
                || !isReflectionOf(room, static_cast<std::int32_t>(r), static_cast<std::int32_t>(f), mirror, *plane) )
            {
                continue;
            }
            found.push_back(MirrorFloorPortal{ SurfaceRef{ static_cast<std::int32_t>(r), static_cast<std::int32_t>(f) }, far });
        }
    }
    return found;
}

std::size_t levelRefreshMirrorFloors(Level& level, const double reflectivity)
{
    if ( !std::isfinite(reflectivity) || reflectivity < 0.0 || reflectivity > 1.0 )
    {
        return 0;
    }
    const std::vector<MirrorFloorPortal> mirrors = levelFindMirrorFloors(level);
    for ( const MirrorFloorPortal& portal : mirrors )
    {
        const Sector& room = level.sectors[static_cast<std::size_t>(portal.floor.sector)];
        Sector& mirror = level.sectors[static_cast<std::size_t>(portal.mirror.sector)];
        mirror.ambient = scaledRgb(room.ambient, reflectivity);
        mirror.extraLight = scaledRgb(room.extraLight, reflectivity);
        for ( std::size_t s = 0; s < room.surfaces.size(); ++s )
        {
            const Surface& source = room.surfaces[s];
            Surface& copy = mirror.surfaces[s];
            const std::size_t n = source.texVertices.size();
            copy.extraLight = scaledRgb(source.extraLight, reflectivity);
            for ( std::size_t k = 0; k < n; ++k )
            {
                copy.texVertices[k].color = scaledCorner(source.texVertices[n - 1 - k].color, reflectivity);
            }
        }
    }
    return mirrors.size();
}

} // namespace sed::level
