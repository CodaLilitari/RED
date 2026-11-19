#include "sed/level/shadow_cuts.hpp"

#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/merge.hpp"

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
            reached.insert({ sectorIndex, static_cast<std::int32_t>(s) });
            if ( frustum.empty() || options.mode != ShadowCutMode::shadowEdges ) continue;
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

/** Cuts one surface by a plane; returns the new piece's index, or -1. */
std::int32_t cutOnce(Level& level, const SurfaceRef ref, const Plane& plane, const V3& point)
{
    Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const std::size_t before = sector.surfaces.size();
    if ( !levelCleaveSurface(level, ref, TVector{ plane.n.x, plane.n.y, plane.n.z }, point.x, point.y, point.z) )
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

} // namespace

ShadowCutResult levelShadowCuts(Level& level, const ShadowCutOptions& options)
{
    ShadowCutResult result;
    if ( !(options.maxEdge > 0.0) || !(options.minPiece >= 0.0) || options.maxNewSurfaces < 0 || options.maxDepth < 0 )
    {
        result.refusal = "badOptions";
        return result;
    }
    std::vector<std::int32_t> lights = options.lights;
    if ( lights.empty() )
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

    Level work = level;
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
                    const std::int32_t added = cutOnce(work, ref, plane, point);
                    if ( added < 0 )
                    {
                        ++result.refusedCuts;
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
                if ( longestEdge(work, ref, from, to) <= options.maxEdge )
                {
                    done.push_back(piece);
                    continue;
                }
                ++result.plannedCuts;
                V3 n = to - from;
                n = n * (1.0 / length(n));
                const V3 middle = (from + to) * 0.5;
                const std::int32_t added = cutOnce(work, ref, Plane{ n, -dot(n, middle) }, middle);
                if ( added < 0 )
                {
                    ++result.refusedCuts;
                    done.push_back(piece);
                    continue;
                }
                ++result.newSurfaces;
                todo.push_back(piece);
                todo.push_back(added);
            }
            done.insert(done.end(), todo.begin(), todo.end());
            if ( done.size() > 1 ) pieces[key] = done;
        }
    }
    if ( result.newSurfaces > options.maxNewSurfaces )
    {
        result.refusal = "overBudget";
        return result;
    }
    for ( const auto& [key, list] : pieces )
    {
        ++result.surfacesCut;
        for ( const std::int32_t piece : list ) result.pieces.push_back(SurfaceRef{ key.first, piece });
    }
    level = std::move(work);
    result.ok = true;
    return result;
}

ShadowCutRemoval levelRemoveShadowCuts(Level& level, std::span<const std::int32_t> sectors)
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
                const auto batch = levelMergeSelectedSurfaces(level, refs);
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

} // namespace sed::level
