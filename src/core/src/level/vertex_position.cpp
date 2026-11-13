#include "sed/level/vertex_position.hpp"

#include "sed/level/geometry.hpp"
#include "sed/level/transform.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <new>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace sed::level
{
namespace
{
struct Failure
{
    VertexPositionStatus status;
    std::string_view message;
};

class VertexEditWorkBudget
{
public:
    explicit VertexEditWorkBudget(const std::size_t limit) : remaining_(limit)
    {
    }

    void reserve(const std::size_t count)
    {
        reserveProduct(count, 1);
    }

    void reserveProduct(const std::size_t count, const std::size_t perItem)
    {
        if ( count != 0 && perItem > remaining_ / count )
        {
            throw Failure{ VertexPositionStatus::WorkLimitExceeded,
                "Vertex positioning exceeds the cumulative geometry/weld work limit." };
        }
        remaining_ -= count * perItem;
    }

    void reserveFaceChecks(const std::size_t corners)
    {
        // Planarity's precision allowance and the simple-boundary predicate
        // each traverse corner pairs; the remaining face checks are linear.
        reserveProduct(corners, corners);
        reserveProduct(corners, corners);
        reserveProduct(corners, 6);
    }

private:
    std::size_t remaining_{};
};

void reserveSectorVertexEditWork(const sed::Sector& sector, VertexEditWorkBudget& work,
    const VertexPositionOptions& options)
{
    work.reserveProduct(sector.vertices.size(), 6);
    work.reserveProduct(sector.surfaces.size(), 4);
    std::size_t corners{};
    for ( const auto& face : sector.surfaces )
    {
        const auto count = face.vertices.size();
        if ( count < 3 || count > options.maxFaceCorners || face.texVertices.size() != count )
        {
            throw Failure{ VertexPositionStatus::GeometryUnsupported,
                "Editing requires admitted face corner counts and matching UV/light records." };
        }
        // Reserve both geometry passes before copying or validating the
        // sector. Successful face reservations also bound this corner sum
        // by the fixed work limit, so the addition cannot overflow.
        work.reserveFaceChecks(count);
        work.reserveFaceChecks(count);
        corners += count;
    }
    // Each convexity vertex/face query computes a precision allowance by
    // traversing that face's corners. Charge the nested corner work too.
    work.reserveProduct(sector.vertices.size(), corners);
    work.reserveProduct(sector.vertices.size(), corners);
}

sed::TVector position(const sed::Vertex& vertex)
{
    return { vertex.x, vertex.y, vertex.z };
}

bool bounded(const double value, const VertexPositionOptions& options)
{
    return std::isfinite(value) && std::abs(value) <= options.coordinateLimit;
}

bool samePosition(const sed::Vertex& first, const sed::Vertex& second)
{
    return first.x == second.x && first.y == second.y && first.z == second.z;
}

sed::Surface checkedFace(const sed::Sector& sector, const sed::Surface& source,
    const VertexPositionStatus code, const VertexPositionOptions& options)
{
    if ( source.vertices.size() < 3 || source.vertices.size() > options.maxFaceCorners
        || source.texVertices.size() != source.vertices.size() )
    {
        throw Failure{ code, "Editing requires admitted face corner counts and matching UV/light records" };
    }
    std::set<std::int32_t> corners;
    for ( std::size_t c = 0; c < source.vertices.size(); ++c )
    {
        const auto vertex = source.vertices[c];
        if ( vertex < 0 || static_cast<std::size_t>(vertex) >= sector.vertices.size()
            || !corners.insert(vertex).second )
        {
            throw Failure{ code, "A face has invalid or repeated vertex references" };
        }
        const auto& point = sector.vertices[vertex];
        if ( !bounded(point.x, options) || !bounded(point.y, options) || !bounded(point.z, options) )
        {
            throw Failure{ code, "Edited geometry must be finite and within the caller coordinate limit" };
        }
        if ( !bounded(source.texVertices[c].u, options) || !bounded(source.texVertices[c].v, options) )
        {
            throw Failure{ code, "Edited texture coordinates must be finite and within the caller coordinate limit" };
        }
    }
    for ( std::size_t c = 0; c < source.vertices.size(); ++c )
    {
        const auto& first = sector.vertices[source.vertices[c]];
        const auto& second = sector.vertices[source.vertices[(c + 1) % source.vertices.size()]];
        if ( (position(first) - position(second)).lengthSqr() <= 1e-18 )
        {
            throw Failure{ code, "Editing refuses zero-length polygon edges" };
        }
    }
    auto face = source;
    face.normal = sed::level::surfaceCalcNormalNewell(sector, face);
    if ( face.normal.lengthSqr() < 0.99 || !sed::level::surfaceIsPlanar(sector, face)
        || !sed::level::surfaceIsConvex(sector, face)
        || !sed::level::surfaceHasSimpleBoundary(sector, face) )
    {
        throw Failure{ code, "Editing requires nonzero planar convex faces" };
    }
    return face;
}

void checkSectorGeometry(sed::Sector& sector, const VertexPositionOptions& options)
{
    if ( sector.vertices.size() < 4 || sector.surfaces.size() < 4 )
    {
        throw Failure{ VertexPositionStatus::GeometryUnsupported, "Vertex positioning requires a closed convex sector" };
    }
    sed::TVector center{};
    for ( const auto& vertex : sector.vertices )
    {
        if ( !bounded(vertex.x, options) || !bounded(vertex.y, options) || !bounded(vertex.z, options) )
        {
            throw Failure{ VertexPositionStatus::GeometryUnsupported,
                "Vertex positioning requires finite coordinates within the caller coordinate limit" };
        }
        center = center + position(vertex) * (1.0 / static_cast<double>(sector.vertices.size()));
    }
    std::map<std::pair<std::int32_t, std::int32_t>, std::size_t> edges;
    for ( auto& surface : sector.surfaces )
    {
        surface = checkedFace(sector, surface, VertexPositionStatus::GeometryUnsupported, options);
        const auto origin = position(sector.vertices[surface.vertices[0]]);
        if ( surface.normal.dot(center - origin) <= 1e-8 )
        {
            throw Failure{ VertexPositionStatus::GeometryUnsupported, "Vertex positioning requires inward faces around nonzero volume" };
        }
        for ( std::size_t c = 0; c < surface.vertices.size(); ++c )
        {
            ++edges[{ surface.vertices[c], surface.vertices[(c + 1) % surface.vertices.size()] }];
        }
    }
    for ( const auto& [edge, count] : edges )
    {
        const auto reverse = edges.find({ edge.second, edge.first });
        if ( count != 1 || reverse == edges.end() || reverse->second != 1 )
        {
            throw Failure{ VertexPositionStatus::GeometryUnsupported, "Vertex positioning requires a closed manifold boundary" };
        }
    }
    if ( !sed::level::sectorIsConvex(sector) )
    {
        throw Failure{ VertexPositionStatus::GeometryUnsupported, "Vertex positioning would create a nonconvex sector" };
    }
}

std::vector<std::int32_t> portalVertices(const sed::Level& level, const sed::SurfaceRef sourceRef,
    VertexEditWorkBudget& work, const VertexPositionOptions& options)
{
    const auto& sourceSector = level.sectors[sourceRef.sector];
    const auto& source = sourceSector.surfaces[sourceRef.surface];
    const auto peerRef = *source.adjoin;
    if ( peerRef.sector < 0 || static_cast<std::size_t>(peerRef.sector) >= level.sectors.size()
        || peerRef.sector == sourceRef.sector || peerRef.surface < 0
        || static_cast<std::size_t>(peerRef.surface) >= level.sectors[peerRef.sector].surfaces.size() )
    {
        throw Failure{ VertexPositionStatus::AdjoinUnsupported, "Vertex positioning requires valid reciprocal adjoin references" };
    }
    const auto& peerSector = level.sectors[peerRef.sector];
    const auto& peer = peerSector.surfaces[peerRef.surface];
    if ( peer.adjoin != sourceRef || source.vertices.size() != peer.vertices.size() )
    {
        throw Failure{ VertexPositionStatus::AdjoinUnsupported, "Vertex positioning requires reciprocal adjoins with matching boundaries" };
    }
    // The source sector has passed its face admission; an equal peer count
    // therefore also fits the caller policy. Charge before checking either
    // boundary or trying every reverse-cyclic offset, including correspondence copies.
    const auto count = source.vertices.size();
    work.reserveFaceChecks(count);
    work.reserveFaceChecks(count);
    work.reserveProduct(count, count);
    work.reserveProduct(count, count);
    work.reserve(count);
    (void)checkedFace(sourceSector, source, VertexPositionStatus::AdjoinUnsupported, options);
    (void)checkedFace(peerSector, peer, VertexPositionStatus::AdjoinUnsupported, options);
    std::size_t matches{};
    std::vector<std::int32_t> correspondence(count);
    for ( std::size_t offset = 0; offset < count; ++offset )
    {
        bool matchesBoundary = true;
        for ( std::size_t c = 0; c < count; ++c )
        {
            if ( !samePosition(sourceSector.vertices[source.vertices[c]],
                    peerSector.vertices[peer.vertices[(offset + count - c) % count]]) )
            {
                matchesBoundary = false;
                break;
            }
        }
        if ( matchesBoundary )
        {
            ++matches;
            for ( std::size_t c = 0; c < count; ++c )
            {
                correspondence[c] = peer.vertices[(offset + count - c) % count];
            }
        }
    }
    if ( matches != 1 )
    {
        throw Failure{ VertexPositionStatus::AdjoinUnsupported, "Vertex positioning requires one exact reverse-cyclic portal correspondence" };
    }
    return correspondence;
}

VertexPositionResult preparePosition(Level& level, const VertexRef vertex,
    const TVector& desired, const VertexPositionOptions& options)
{
    const auto sectorIndex = vertex.sector;
    const auto vertexIndex = vertex.vertex;
    if ( sectorIndex < 0 || static_cast<std::size_t>(sectorIndex) >= level.sectors.size()
        || vertexIndex < 0 || static_cast<std::size_t>(vertexIndex) >= level.sectors[sectorIndex].vertices.size() )
    {
        return { VertexPositionStatus::InvalidTarget, "Vertex positioning requires an existing vertex." };
    }
    if ( std::isnan(options.coordinateLimit) || options.coordinateLimit < 0
        || !bounded(desired.x, options) || !bounded(desired.y, options) || !bounded(desired.z, options) )
    {
        return { VertexPositionStatus::InvalidPosition,
            "Vertex position must be finite and within the caller coordinate limit." };
    }
    // A native Level has separate vertex pools per Sector. Follow only exact
    // reciprocal portal correspondences; nearby or merely coincident geometry
    // does not acquire a weld. The queue also covers portals meeting at a corner.
    std::vector<sed::VertexRef> queue{ { sectorIndex, vertexIndex } };
    std::set<std::pair<std::int32_t, std::int32_t>> selected{ { sectorIndex, vertexIndex } };
    std::map<std::int32_t, sed::Sector> staged;
    std::map<std::int32_t, std::vector<sed::VertexRef>> localVertices;
    VertexEditWorkBudget work(options.workLimit);
    for ( std::size_t next = 0; next < queue.size(); ++next )
    {
        const auto ref = queue[next];
        work.reserve(1);
        if ( !staged.contains(ref.sector) )
        {
            work.reserve(options.readOnlySectors.size());
            if ( std::find(options.readOnlySectors.begin(), options.readOnlySectors.end(), ref.sector)
                != options.readOnlySectors.end() )
            {
                throw Failure{ VertexPositionStatus::ReadOnlySector,
                    "The selected vertex or a connected portal belongs to a noneditable sector." };
            }
            reserveSectorVertexEditWork(level.sectors[ref.sector], work, options);
        }
        auto [entry, inserted] = staged.try_emplace(ref.sector, level.sectors[ref.sector]);
        if ( inserted )
        {
            checkSectorGeometry(entry->second, options);
        }
        const auto& sector = level.sectors[ref.sector];
        // One face scan during weld discovery, plus the checked core's
        // complete normal refresh for every selected local vertex. Faces
        // use a bounded maximum corner count; normal calculation has two scans.
        std::size_t maximumCorners = 64;
        for ( const auto& face : sector.surfaces )
        {
            maximumCorners = std::max(maximumCorners, face.vertices.size());
        }
        work.reserveProduct(sector.surfaces.size(), maximumCorners);
        work.reserveProduct(sector.surfaces.size(), maximumCorners);
        work.reserve(sector.surfaces.size());
        localVertices[ref.sector].push_back({ 0, ref.vertex });
        for ( std::size_t f = 0; f < sector.surfaces.size(); ++f )
        {
            const auto& surface = sector.surfaces[f];
            if ( !surface.adjoin )
            {
                continue;
            }
            const auto correspondence = portalVertices(level,
                { ref.sector, static_cast<std::int32_t>(f) }, work, options);
            const auto corner = std::find(surface.vertices.begin(), surface.vertices.end(), ref.vertex);
            if ( corner == surface.vertices.end() )
            {
                continue;
            }
            const auto peerVertex = correspondence[static_cast<std::size_t>(corner - surface.vertices.begin())];
            if ( selected.emplace(surface.adjoin->sector, peerVertex).second )
            {
                queue.push_back({ surface.adjoin->sector, peerVertex });
            }
        }
    }

    const auto delta = desired - position(level.sectors[sectorIndex].vertices[vertexIndex]);
    for ( auto& [sector, replacement] : staged )
    {
        sed::Level candidate{};
        candidate.sectors.push_back(std::move(replacement));
        if ( !sed::level::levelTranslateVerticesChecked(candidate, localVertices[sector], delta.x, delta.y, delta.z) )
        {
            throw Failure{ VertexPositionStatus::GeometryUnsupported,
                "The shared vertex transform could not prepare the edit." };
        }
        auto& moved = candidate.sectors[0];
        for ( std::size_t v = 0; v < moved.vertices.size(); ++v )
        {
            moved.vertices[v].mark = level.sectors[sector].vertices[v].mark;
        }
        for ( const auto ref : localVertices[sector] )
        {
            // Absolute positioning must retain even coordinates much smaller
            // than the old point's ULP; subtraction/addition alone can lose them.
            moved.vertices[ref.vertex].x = desired.x;
            moved.vertices[ref.vertex].y = desired.y;
            moved.vertices[ref.vertex].z = desired.z;
        }
        checkSectorGeometry(moved, options);
        replacement = std::move(moved);
    }
    bool changed = false;
    for ( const auto& [sector, replacement] : staged )
    {
        const auto& original = level.sectors[sector];
        for ( std::size_t v = 0; v < replacement.vertices.size(); ++v )
        {
            changed = changed || !samePosition(original.vertices[v], replacement.vertices[v]);
        }
        for ( std::size_t f = 0; f < replacement.surfaces.size(); ++f )
        {
            const auto& before = original.surfaces[f].normal;
            const auto& after = replacement.surfaces[f].normal;
            changed = changed || before.x != after.x || before.y != after.y || before.z != after.z;
        }
    }
    if ( !changed )
    {
        return { VertexPositionStatus::Unchanged, {} };
    }
    // No corner order, reference or entity ID changes. Every portal endpoint
    // in the collected correspondence is assigned the same absolute position.
    static_assert(std::is_nothrow_move_assignable_v<sed::Sector>);
    for ( auto& [sector, replacement] : staged )
    {
        level.sectors[sector] = std::move(replacement);
    }
    return { VertexPositionStatus::Changed, {} };
}
} // namespace

VertexPositionResult levelSetVertexPositionChecked(Level& level, const VertexRef vertex,
    const TVector& desired, const VertexPositionOptions& options)
{
    try
    {
        return preparePosition(level, vertex, desired, options);
    }
    catch ( const Failure& error )
    {
        return { error.status, error.message };
    }
    catch ( const std::bad_alloc& )
    {
        return { VertexPositionStatus::AllocationFailed,
            "The vertex edit could not allocate its bounded preparation state." };
    }
}

} // namespace sed::level
