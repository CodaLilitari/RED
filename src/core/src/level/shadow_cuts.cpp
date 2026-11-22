#include "sed/level/shadow_cuts.hpp"

#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/merge.hpp"
#include "sed/text.hpp"

#include "cleave_index_utils.hpp"
#include "cog_reference_utils.hpp"
#include "geometry_precision.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace sed::level
{
namespace
{

struct V3
{
    double x{};
    double y{};
    double z{};
};

V3 operator-(const V3& a, const V3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator+(const V3& a, const V3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 operator*(const V3& a, const double s) { return { a.x * s, a.y * s, a.z * s }; }
double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3& a, const V3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double length(const V3& a) { return std::sqrt(dot(a, a)); }

/** Points x with dot(n, x) + d >= 0 are inside. */
struct Plane
{
    V3 n{};
    double d{};
    [[nodiscard]] double at(const V3& p) const { return dot(n, p) + d; }
};

V3 vertexAt(const Sector& sector, const std::int32_t index)
{
    const Vertex& v = sector.vertices[static_cast<std::size_t>(index)];
    return { v.x, v.y, v.z };
}

std::vector<V3> polygonOf(const Sector& sector, const Surface& surface)
{
    std::vector<V3> out;
    out.reserve(surface.vertices.size());
    for ( const std::int32_t index : surface.vertices )
    {
        if ( index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size() ) return {};
        out.push_back(vertexAt(sector, index));
    }
    return out;
}

V3 normalOf(const Surface& surface) { return { surface.normal.x, surface.normal.y, surface.normal.z }; }

std::vector<V3> clip(const std::vector<V3>& polygon, const Plane& plane)
{
    std::vector<V3> out;
    if ( polygon.empty() ) return out;
    for ( std::size_t i = 0; i < polygon.size(); ++i )
    {
        const V3& a = polygon[i];
        const V3& b = polygon[(i + 1) % polygon.size()];
        const double da = plane.at(a);
        const double db = plane.at(b);
        if ( da >= 0.0 ) out.push_back(a);
        if ( (da >= 0.0) != (db >= 0.0) )
        {
            const double t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

double area(const std::vector<V3>& polygon)
{
    if ( polygon.size() < 3 ) return 0.0;
    V3 sum{};
    for ( std::size_t i = 1; i + 1 < polygon.size(); ++i )
    {
        sum = sum + cross(polygon[i] - polygon[0], polygon[i + 1] - polygon[0]);
    }
    return 0.5 * length(sum);
}

bool samePlane(const Plane& a, const Plane& b)
{
    const double c = dot(a.n, b.n);
    if ( c > 1.0 - 1e-9 ) return std::abs(a.d - b.d) < 1e-7;
    if ( c < -1.0 + 1e-9 ) return std::abs(a.d + b.d) < 1e-7;
    return false;
}

bool receives(const Sector& sector, const Surface& surface, const V3& light, const double range)
{
    if ( surface.adjoin || surface.light == 0 || surface.geo == 0 || surface.vertices.size() < 3 ) return false;
    const V3 n = normalOf(surface);
    if ( length(n) < 0.5 ) return false;
    const double side = dot(n, light - vertexAt(sector, surface.vertices[0]));
    return side > 0.0 && side <= range;
}

// For a convex, clipped receiver, the closest point is either on its
// perimeter or the perpendicular projection onto its interior.
bool withinLightRange(const std::vector<V3>& polygon, const V3& normal, const V3& light, const double range)
{
    if ( polygon.size() < 3 || area(polygon) < 1e-10 )
    {
        return false;
    }
    const double normalSquared = dot(normal, normal);
    if ( normalSquared <= 0.0 )
    {
        return false;
    }
    const double height = dot(normal, light - polygon.front());
    const double rangeSquared = range * range;
    if ( height * height >= rangeSquared * normalSquared )
    {
        return false;
    }
    bool positive = false;
    bool negative = false;
    for ( std::size_t i = 0; i < polygon.size(); ++i )
    {
        const V3 edge = polygon[(i + 1) % polygon.size()] - polygon[i];
        const double edgeSquared = dot(edge, edge);
        if ( edgeSquared == 0.0 )
        {
            continue;
        }
        const V3 offset = light - polygon[i];
        const double fraction = std::clamp(dot(offset, edge) / edgeSquared, 0.0, 1.0);
        const V3 delta = offset - edge * fraction;
        if ( dot(delta, delta) < rangeSquared )
        {
            return true;
        }
        const double side = dot(normal, cross(edge, offset));
        positive = positive || side > 0.0;
        negative = negative || side < 0.0;
    }
    return !(positive && negative);
}

using CutPlan = std::map<std::pair<std::int32_t, std::int32_t>, std::vector<Plane>>;

struct Walker
{
    const Level& level;
    const ShadowCutOptions& options;
    V3 light{};
    double range{};
    CutPlan& plan;
    std::set<std::pair<std::int32_t, std::int32_t>>& reached;
    std::int32_t visits{};

    void addCut(const std::int32_t sector, const std::int32_t surface, const Plane& plane)
    {
        auto& planes = plan[{ sector, surface }];
        for ( const Plane& existing : planes )
        {
            if ( samePlane(existing, plane) ) return;
        }
        planes.push_back(plane);
    }

    void walk(const std::int32_t sectorIndex, const std::vector<Plane>& frustum, const std::int32_t depth)
    {
        if ( ++visits > 200000 ) return;
        const Sector& sector = level.sectors[static_cast<std::size_t>(sectorIndex)];
        for ( std::size_t s = 0; s < sector.surfaces.size(); ++s )
        {
            const Surface& surface = sector.surfaces[s];
            const std::vector<V3> polygon = polygonOf(sector, surface);
            if ( polygon.size() < 3 ) continue;
            if ( surface.adjoin )
            {
                if ( depth >= options.maxDepth || (surface.adjoinFlags & SAF_BlockLight) != 0 ) continue;
                const SurfaceRef target = *surface.adjoin;
                if ( target.sector < 0 || static_cast<std::size_t>(target.sector) >= level.sectors.size() ) continue;
                const double side = dot(normalOf(surface), light - polygon[0]);
                if ( side <= 1e-9 || side > range ) continue;
                std::vector<V3> window = polygon;
                for ( const Plane& plane : frustum ) window = clip(window, plane);
                if ( area(window) < 1e-10 ) continue;
                V3 centre{};
                for ( const V3& p : window ) centre = centre + p;
                centre = centre * (1.0 / static_cast<double>(window.size()));
                std::vector<Plane> next = frustum;
                for ( std::size_t i = 0; i < window.size(); ++i )
                {
                    V3 n = cross(window[i] - light, window[(i + 1) % window.size()] - light);
                    const double len = length(n);
                    if ( len < 1e-12 ) continue;
                    n = n * (1.0 / len);
                    Plane plane{ n, -dot(n, light) };
                    if ( plane.at(centre) < 0.0 ) plane = Plane{ n * -1.0, -plane.d };
                    next.push_back(plane);
                }
                walk(target.sector, next, depth + 1);
                continue;
            }
            if ( !receives(sector, surface, light, range) ) continue;
            if ( options.mode == ShadowCutMode::sizeLimit )
            {
                std::vector<V3> visible = polygon;
                for ( const Plane& plane : frustum )
                {
                    visible = clip(visible, plane);
                }
                if ( withinLightRange(visible, normalOf(surface), light, range) )
                {
                    reached.insert({ sectorIndex, static_cast<std::int32_t>(s) });
                }
                continue;
            }
            if ( frustum.empty() ) continue;
            bool lit = true;
            for ( const Plane& plane : frustum )
            {
                for ( const V3& p : polygon ) lit = lit && plane.at(p) >= -1e-9;
            }
            if ( lit ) continue;
            std::vector<V3> litPart = polygon;
            for ( const Plane& plane : frustum ) litPart = clip(litPart, plane);
            if ( area(litPart) < 1e-10 ) continue;
            for ( const Plane& plane : frustum )
            {
                double low = 0.0;
                double high = 0.0;
                for ( const V3& p : polygon )
                {
                    low = std::min(low, plane.at(p));
                    high = std::max(high, plane.at(p));
                }
                if ( low < -options.minPiece && high > options.minPiece )
                {
                    addCut(sectorIndex, static_cast<std::int32_t>(s), plane);
                }
            }
        }
    }
};

bool crosses(const Level& level, const SurfaceRef ref, const Plane& plane, const double margin)
{
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const std::vector<V3> polygon = polygonOf(sector, sector.surfaces[static_cast<std::size_t>(ref.surface)]);
    double low = 0.0;
    double high = 0.0;
    for ( const V3& p : polygon )
    {
        low = std::min(low, plane.at(p));
        high = std::max(high, plane.at(p));
    }
    return low < -margin && high > margin;
}

// The engine refuses a level with a surface of more than 24 corners.
constexpr std::size_t kCornerLimit = 24;

/**
 * Whether a cut stays within the corner limit: each piece of the cut surface
 * keeps at most one more corner than it had, and every surface sharing an
 * edge the plane crosses between corners gains one (and, for an opening, so
 * does its other side).
 */
bool cutFitsCornerLimit(const Level& level, const SurfaceRef ref, const Plane& plane)
{
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const Surface& surface = sector.surfaces[static_cast<std::size_t>(ref.surface)];
    if ( surface.vertices.size() + 1 > kCornerLimit ) return false;
    constexpr double kOn = 1e-9;
    for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
    {
        const V3 a = vertexAt(sector, surface.vertices[i]);
        const V3 b = vertexAt(sector, surface.vertices[(i + 1) % surface.vertices.size()]);
        const double da = plane.at(a);
        const double db = plane.at(b);
        if ( !((da > kOn && db < -kOn) || (da < -kOn && db > kOn)) ) continue;
        // The new corner lands on every edge it lies inside, the neighbours'
        // longer or shorter collinear edges included.
        const V3 corner = a + (b - a) * (da / (da - db));
        for ( std::size_t s = 0; s < sector.surfaces.size(); ++s )
        {
            if ( static_cast<std::int32_t>(s) == ref.surface ) continue;
            const Surface& other = sector.surfaces[s];
            for ( std::size_t k = 0; k < other.vertices.size(); ++k )
            {
                const V3 c = vertexAt(sector, other.vertices[k]);
                const V3 d = vertexAt(sector, other.vertices[(k + 1) % other.vertices.size()]);
                const V3 edge = d - c;
                const double len2 = dot(edge, edge);
                if ( len2 <= 0.0 ) continue;
                const double u = dot(corner - c, edge) / len2;
                if ( u <= 1e-9 || u >= 1.0 - 1e-9 ) continue;
                if ( length(corner - (c + edge * u)) > 1e-7 ) continue;
                if ( other.vertices.size() + 1 > kCornerLimit ) return false;
                if ( other.adjoin && other.adjoin->sector >= 0
                    && static_cast<std::size_t>(other.adjoin->sector) < level.sectors.size() )
                {
                    const Sector& far = level.sectors[static_cast<std::size_t>(other.adjoin->sector)];
                    if ( other.adjoin->surface >= 0 && static_cast<std::size_t>(other.adjoin->surface) < far.surfaces.size()
                        && far.surfaces[static_cast<std::size_t>(other.adjoin->surface)].vertices.size() + 1 > kCornerLimit )
                    {
                        return false;
                    }
                }
                break;
            }
        }
    }
    return true;
}

std::size_t mostCorners(const Level& level)
{
    std::size_t most = 0;
    for ( const Sector& sector : level.sectors )
    {
        for ( const Surface& surface : sector.surfaces ) most = std::max(most, surface.vertices.size());
    }
    return most;
}

/**
 * Cuts one surface by a plane; returns the new piece's index, -1 when the
 * cleave refuses, or -2 when the cut would leave a surface of the sector or
 * a neighbour past the corner limit (the cut is then undone). A cut corner
 * reaches the neighbours through the openings, so they are checked too.
 */
std::int32_t cutOnce(Level& level, const SurfaceRef ref, const Plane& plane, const V3& point)
{
    const std::size_t before = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces.size();
    const auto result = levelCleaveSurface(level, ref, { plane.n.x, plane.n.y, plane.n.z },
        point.x, point.y, point.z, kCornerLimit);
    if ( result == SurfaceCleaveResult::cornerLimit )
    {
        return -2;
    }
    if ( result != SurfaceCleaveResult::applied )
    {
        return -1;
    }
    const std::size_t after = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces.size();
    return after > before ? static_cast<std::int32_t>(after - 1) : -1;
}

double longestEdge(const Level& level, const SurfaceRef ref, V3& from, V3& to)
{
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const std::vector<V3> polygon = polygonOf(sector, sector.surfaces[static_cast<std::size_t>(ref.surface)]);
    double best = 0.0;
    for ( std::size_t i = 0; i < polygon.size(); ++i )
    {
        const double len = length(polygon[(i + 1) % polygon.size()] - polygon[i]);
        if ( len > best )
        {
            best = len;
            from = polygon[i];
            to = polygon[(i + 1) % polygon.size()];
        }
    }
    return best;
}

// A short subdivided perimeter does not imply interior light samples. Bound
// the whole convex polygon's diameter, not just its consecutive edges.
double longestSpan(const Level& level, const SurfaceRef ref, V3& from, V3& to)
{
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const auto polygon = polygonOf(sector, sector.surfaces[static_cast<std::size_t>(ref.surface)]);
    double best = 0.0;
    for ( std::size_t i = 0; i < polygon.size(); ++i )
    {
        for ( std::size_t j = i + 1; j < polygon.size(); ++j )
        {
            const double distance = length(polygon[j] - polygon[i]);
            if ( distance > best )
            {
                best = distance;
                from = polygon[i];
                to = polygon[j];
            }
        }
    }
    return best;
}

V3 receiverCutPoint(const Level& level, const SurfaceRef ref, const V3& from, const V3& to,
                    const V3& direction, const double span)
{
    const V3 middle = (from + to) * 0.5;
    const TVector normal{ direction.x, direction.y, direction.z };
    const TVector anchor{ middle.x, middle.y, middle.z };
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const auto& surface = sector.surfaces[static_cast<std::size_t>(ref.surface)];

    // A bisector can miss a mathematically coincident corner by roundoff,
    // cutting a microscopic edge there. Exact corner hits are useful.
    bool grazesCorner = false;
    std::vector<double> fractions{ 0.3, 0.7 };
    for ( const auto index : surface.vertices )
    {
        const V3 corner = vertexAt(sector, index);
        const double distance = dot(direction, corner - middle);
        const double error = precision::planeError(normal, { corner.x, corner.y, corner.z }, anchor)
            + precision::kRoundoff * span;
        if ( distance != 0.0 && std::abs(distance) <= error ) grazesCorner = true;
        const double fraction = 0.5 + distance / span;
        if ( fraction > 0.3 && fraction < 0.7 ) fractions.push_back(fraction);
    }
    if ( !grazesCorner ) return middle;

    // Stay in the central 40% of the chord and bisect the widest gap
    // between projected corners. Each side retains at least 30% of the
    // chord, with one deterministic plane and no repeated cleave attempts.
    std::sort(fractions.begin(), fractions.end());
    double largestGap = 0.0;
    double cutFraction = 0.5;
    for ( std::size_t i = 1; i < fractions.size(); ++i )
    {
        const double gap = fractions[i] - fractions[i - 1];
        if ( gap > largestGap )
        {
            largestGap = gap;
            cutFraction = (fractions[i] + fractions[i - 1]) * 0.5;
        }
    }
    return from * (1.0 - cutFraction) + to * cutFraction;
}

bool validReceiver(const Level& level, const SurfaceRef ref)
{
    if ( ref.sector < 0 || static_cast<std::size_t>(ref.sector) >= level.sectors.size() ) return false;
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    if ( ref.surface < 0 || static_cast<std::size_t>(ref.surface) >= sector.surfaces.size() ) return false;
    const Surface& surface = sector.surfaces[static_cast<std::size_t>(ref.surface)];
    const auto material = text::trimView(surface.material);
    if ( surface.adjoin || surface.light != 3 || (surface.geo != 3 && surface.geo != 4)
        || material.empty() || text::iequalsAscii(material, "none")
        || (surface.surfaceFlags & (SF_SkyHorizon | SF_SkyCeiling)) != 0
        || (surface.faceFlags & 0x2u) != 0 || surface.vertices.size() < 3
        || surface.vertices.size() > kCornerLimit || surface.texVertices.size() != surface.vertices.size() ) return false;
    for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
    {
        const auto index = surface.vertices[i];
        if ( index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size() ) return false;
        const Vertex& vertex = sector.vertices[static_cast<std::size_t>(index)];
        for ( const double coordinate : { vertex.x, vertex.y, vertex.z } )
        {
            if ( !std::isfinite(coordinate) || std::abs(coordinate) > 1'000'000.0 ) return false;
        }
        const TexVertex& corner = surface.texVertices[i];
        for ( const auto value : { corner.u, corner.v, corner.color.r, corner.color.g, corner.color.b, corner.color.a } )
        {
            if ( !std::isfinite(value) ) return false;
        }
    }
    const V3 normal = normalOf(surface);
    return std::isfinite(length(normal)) && length(normal) > 0.5
        && surfaceIsPlanar(sector, surface) && surfaceIsConvex(sector, surface)
        && surfaceHasSimpleBoundary(sector, surface)
        && area(polygonOf(sector, surface)) > 1e-10;
}

} // namespace

std::optional<double> levelReceiverSpan(const Level& level, const SurfaceRef ref)
{
    if ( !validReceiver(level, ref) ) return std::nullopt;
    V3 from{}, to{};
    return longestSpan(level, ref, from, to);
}

bool ReceiverWorkBudget::charge(const Sector& sector) noexcept
{
    const auto take = [&](const std::size_t count)
    {
        if ( count > remaining_ ) return false;
        remaining_ -= count;
        return true;
    };
    // Each started 64-byte block of variable-length names also travels
    // with a copied Sector, including repeated rollback snapshots.
    const auto takeText = [&](const std::string& value)
    {
        return take(value.size() / 64 + (value.size() % 64 != 0));
    };
    if ( !take(1) || !take(sector.vertices.size()) || !take(sector.surfaces.size())
        || !takeText(sector.colormap) || !takeText(sector.sound) ) return false;
    for ( const auto& surface : sector.surfaces )
    {
        if ( !take(surface.vertices.size()) || !take(surface.texVertices.size())
            || !takeText(surface.material) ) return false;
    }
    return true;
}

ShadowCutResult levelShadowCuts(Level& level, const ShadowCutOptions& options)
{
    ReceiverWorkBudget budget;
    return levelShadowCuts(level, options, budget);
}

ShadowCutResult levelShadowCuts(Level& level, const ShadowCutOptions& options, ReceiverWorkBudget& budget)
{
    ShadowCutResult result;
    const bool selected = options.mode == ShadowCutMode::selectedReceivers;
    if ( !(options.maxEdge > 0.0) || !(options.minPiece >= 0.0) || options.maxNewSurfaces < 0 || options.maxDepth < 0
        || (options.mode != ShadowCutMode::shadowEdges && options.mode != ShadowCutMode::sizeLimit && !selected)
        || (!selected && !options.surfaces.empty())
        || (selected && (!options.lights.empty() || !std::isfinite(options.maxEdge)
            || options.maxEdge < 0.001 || options.maxEdge > 1000.0 || options.maxNewSurfaces > 100000)) )
    {
        result.refusal = "badOptions";
        return result;
    }
    std::vector<std::int32_t> lights = options.lights;
    if ( lights.empty() && !selected )
    {
        for ( std::size_t i = 0; i < level.lights.size(); ++i ) lights.push_back(static_cast<std::int32_t>(i));
    }
    for ( const std::int32_t index : lights )
    {
        if ( index < 0 || static_cast<std::size_t>(index) >= level.lights.size() )
        {
            result.refusal = "badLight";
            return result;
        }
    }

    CutPlan plan;
    std::set<std::pair<std::int32_t, std::int32_t>> reached;
    if ( selected )
    {
        if ( options.surfaces.empty() || options.surfaces.size() > kMaxSelectedLightReceivers )
        {
            result.refusal = "badReceiver";
            return result;
        }
        // Charge the isolated candidate copy before allocating it.
        for ( const auto& sector : level.sectors )
        {
            if ( !budget.charge(sector) )
            {
                result.refusal = "receiverWorkLimit";
                return result;
            }
        }
        for ( const SurfaceRef ref : options.surfaces )
        {
            if ( !validReceiver(level, ref) || !reached.emplace(ref.sector, ref.surface).second )
            {
                result.refusal = "badReceiver";
                return result;
            }
        }
    }
    for ( const std::int32_t index : lights )
    {
        const SedLight& light = level.lights[static_cast<std::size_t>(index)];
        if ( (light.flags & LF_NoBlock) != 0 || !(light.range > 0.0) ) continue;
        const V3 position{ light.position.x, light.position.y, light.position.z };
        bool inside = false;
        for ( std::size_t s = 0; s < level.sectors.size(); ++s )
        {
            if ( !sectorContainsPoint(level.sectors[s], position.x, position.y, position.z) ) continue;
            Walker walker{ level, options, position, light.range, plan, reached };
            walker.walk(static_cast<std::int32_t>(s), {}, 0);
            result.walkTruncated = result.walkTruncated || walker.visits > 200000;
            inside = true;
            break;
        }
        if ( !inside ) ++result.lightsOutside;
    }

    Level work;
    std::map<std::int32_t, std::vector<std::int32_t>> rollbackSectors;
    if ( selected )
    {
        // A nonportal cleave snapshots its owner and every sector adjoined
        // to that owner. Receiver cuts append faces and keep these links.
        for ( const auto& [owner, face] : reached )
        {
            auto [it, inserted] = rollbackSectors.try_emplace(owner);
            if ( !inserted ) continue;
            it->second = detail::surfaceCleaveRollbackSectors(level, {owner, face});
        }
        // Receiver cleaves only modify sector geometry. Keep retained source
        // bytes, import tables, templates and other document data in place.
        work.kind = level.kind;
        work.uvCoordinateSpace = level.uvCoordinateSpace;
        work.sectors = level.sectors;
    }
    else
    {
        work = level;
    }
    std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::int32_t>> pieces;
    if ( options.mode == ShadowCutMode::shadowEdges )
    {
        for ( const auto& [key, planes] : plan ) result.plannedCuts += static_cast<std::int32_t>(planes.size());
        for ( const auto& [key, planes] : plan )
        {
            std::vector<std::int32_t> current{ key.second };
            for ( const Plane& plane : planes )
            {
                const V3 point = plane.n * -plane.d;
                std::vector<std::int32_t> next;
                for ( const std::int32_t piece : current )
                {
                    next.push_back(piece);
                    const SurfaceRef ref{ key.first, piece };
                    if ( !crosses(work, ref, plane, options.minPiece) ) continue;
                    if ( !cutFitsCornerLimit(work, ref, plane) )
                    {
                        ++result.cornerLimitSkips;
                        continue;
                    }
                    const std::int32_t added = cutOnce(work, ref, plane, point);
                    if ( added < 0 )
                    {
                        ++(added == -2 ? result.cornerLimitSkips : result.refusedCuts);
                        continue;
                    }
                    next.push_back(added);
                    ++result.newSurfaces;
                }
                current = std::move(next);
                if ( result.newSurfaces > options.maxNewSurfaces ) break;
            }
            if ( current.size() > 1 ) pieces[key] = current;
            if ( result.newSurfaces > options.maxNewSurfaces ) break;
        }
    }
    else
    {
        for ( const auto& key : reached )
        {
            std::vector<std::int32_t> todo{ key.second };
            std::vector<std::int32_t> done;
            while ( !todo.empty() && result.newSurfaces <= options.maxNewSurfaces )
            {
                const std::int32_t piece = todo.back();
                todo.pop_back();
                V3 from{};
                V3 to{};
                const SurfaceRef ref{ key.first, piece };
                const double span = selected ? longestSpan(work, ref, from, to) : longestEdge(work, ref, from, to);
                if ( span <= options.maxEdge )
                {
                    done.push_back(piece);
                    continue;
                }
                if ( selected )
                {
                    if ( result.newSurfaces >= options.maxNewSurfaces )
                    {
                        result.refusal = "overBudget";
                        return result;
                    }
                    for ( const auto owner : rollbackSectors.at(ref.sector) )
                    {
                        if ( !budget.charge(work.sectors[owner]) )
                        {
                            result.refusal = "receiverWorkLimit";
                            return result;
                        }
                    }
                }
                ++result.plannedCuts;
                V3 n = to - from;
                n = n * (1.0 / length(n));
                const V3 middle = selected ? receiverCutPoint(work, ref, from, to, n, span) : (from + to) * 0.5;
                const Plane plane{ n, -dot(n, middle) };
                // The guarded cleave validates affected neighbour loops before
                // reading them. Selected input must not reach the older fast
                // corner estimate's unchecked neighbour indices.
                if ( !selected && !cutFitsCornerLimit(work, ref, plane) )
                {
                    ++result.cornerLimitSkips;
                    done.push_back(piece);
                    continue;
                }
                const std::int32_t added = cutOnce(work, ref, plane, middle);
                if ( added < 0 )
                {
                    ++(added == -2 ? result.cornerLimitSkips : result.refusedCuts);
                    if ( selected )
                    {
                        result.refusal = added == -2 ? "cornerLimit" : "receiverGeometry";
                        return result;
                    }
                    done.push_back(piece);
                    continue;
                }
                ++result.newSurfaces;
                todo.push_back(piece);
                todo.push_back(added);
            }
            done.insert(done.end(), todo.begin(), todo.end());
            if ( selected || done.size() > 1 ) pieces[key] = std::move(done);
        }
    }
    if ( result.newSurfaces > options.maxNewSurfaces )
    {
        result.refusal = "overBudget";
        return result;
    }
    if ( mostCorners(work) > std::max(kCornerLimit, mostCorners(level)) )
    {
        result.refusal = "cornerLimit";
        return result;
    }
    for ( const auto& [key, list] : pieces )
    {
        if ( selected )
        {
            // Later cuts can insert seam corners into an already completed
            // piece. Certify the final candidate, including unsplit targets.
            for ( const auto piece : list )
            {
                const SurfaceRef ref{ key.first, piece };
                const auto span = levelReceiverSpan(work, ref);
                if ( !span || *span > options.maxEdge )
                {
                    result.refusal = "receiverGeometry";
                    return result;
                }
                result.maxReceiverSpan = std::max(result.maxReceiverSpan, *span);
                result.receivers.push_back(ref);
            }
        }
        if ( list.size() < 2 ) continue;
        ++result.surfacesCut;
        auto& family = result.families.emplace_back();
        for ( const std::int32_t piece : list ) family.push_back({ key.first, piece });
        result.pieces.insert(result.pieces.end(), family.begin(), family.end());
    }
    if ( selected )
    {
        if ( result.newSurfaces > 0 )
        {
            // Appending pieces preserves sector-local face indices. Capture
            // references on the original numbering and publish once after
            // the complete geometry transaction succeeds.
            const detail::GeometryCogReferences references(level);
            level.sectors.swap(work.sectors);
            references.publish(level);
        }
    }
    else
    {
        level = std::move(work);
    }
    result.ok = true;
    return result;
}

ShadowCutRemoval levelRemoveShadowCuts(Level& level, std::span<const std::int32_t> sectors,
    std::span<SurfaceRef> trackedSurfaces)
{
    ShadowCutRemoval removal;
    std::vector<std::int32_t> targets(sectors.begin(), sectors.end());
    if ( targets.empty() )
    {
        for ( std::size_t i = 0; i < level.sectors.size(); ++i ) targets.push_back(static_cast<std::int32_t>(i));
    }
    for ( const std::int32_t sectorIndex : targets )
    {
        if ( sectorIndex < 0 || static_cast<std::size_t>(sectorIndex) >= level.sectors.size() ) continue;
        bool changed = false;
        for ( int pass = 0; pass < 10000; ++pass )
        {
            const Sector& sector = level.sectors[static_cast<std::size_t>(sectorIndex)];
            using Key = std::tuple<std::string, std::int32_t, std::uint32_t, std::int32_t, std::int32_t, std::int32_t,
                float, float, long long, long long, long long, long long>;
            std::map<Key, std::vector<SurfaceRef>> groups;
            for ( std::size_t s = 0; s < sector.surfaces.size(); ++s )
            {
                const Surface& surface = sector.surfaces[s];
                if ( surface.adjoin || surface.vertices.empty() ) continue;
                const V3 n = normalOf(surface);
                const double d = -dot(n, vertexAt(sector, surface.vertices[0]));
                const auto q = [](const double v) { return static_cast<long long>(std::llround(v * 1e6)); };
                groups[Key{ surface.material, surface.surfaceFlags, surface.faceFlags, surface.geo, surface.light,
                    surface.tex, surface.uScale, surface.vScale, q(n.x), q(n.y), q(n.z), q(d) }]
                    .push_back(SurfaceRef{ sectorIndex, static_cast<std::int32_t>(s) });
            }
            bool merged = false;
            for ( const auto& [key, refs] : groups )
            {
                if ( refs.size() < 2 ) continue;
                const auto batch = levelMergeSelectedSurfaces(level, refs, SurfaceMergePolicy::preserveAuthoredSeams, trackedSurfaces);
                if ( batch && batch->mergeCount > 0 )
                {
                    removal.merges += static_cast<std::int32_t>(batch->mergeCount);
                    merged = true;
                    break;
                }
            }
            if ( !merged ) break;
            changed = true;
        }
        if ( changed ) ++removal.sectorsChanged;
    }
    return removal;
}

bool levelRemoveOwnedShadowCuts(Level& level, std::span<const std::vector<SurfaceRef>> families,
                                std::span<SurfaceRef> trackedSurfaces)
{
    if ( families.empty() ) return false;
    std::vector<SurfaceRef> tracked(trackedSurfaces.begin(), trackedSurfaces.end());
    std::set<std::pair<int, int>> unique;
    for ( const auto& family : families )
    {
        if ( family.size() < 2 ) return false;
        for ( const auto ref : family )
        {
            if ( ref.sector != family.front().sector || ref.sector < 0 ||
                 static_cast<std::size_t>(ref.sector) >= level.sectors.size() || ref.surface < 0 ||
                 static_cast<std::size_t>(ref.surface) >= level.sectors[ref.sector].surfaces.size() ||
                 !unique.emplace(ref.sector, ref.surface).second ) return false;
            tracked.push_back(ref);
        }
    }
    Level candidate = level;
    std::size_t offset = trackedSurfaces.size();
    for ( const auto& family : families )
    {
        const auto current = std::span<const SurfaceRef>(tracked).subspan(offset, family.size());
        const auto batch = levelMergeSelectedSurfaces(candidate, current,
            SurfaceMergePolicy::preserveAuthoredSeams, tracked);
        if ( !batch || batch->mergeCount != family.size() - 1 ) return false;
        offset += family.size();
    }
    level = std::move(candidate);
    std::copy_n(tracked.begin(), trackedSurfaces.size(), trackedSurfaces.begin());
    return true;
}

} // namespace sed::level
