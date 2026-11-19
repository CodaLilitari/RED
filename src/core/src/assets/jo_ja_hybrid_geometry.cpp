#include "sed/assets/jo_ja_hybrid_geometry.hpp"
#include "sed/assets/jo_ja_hybrid_validation.hpp"
#include "sed/assets/jo_ja_structural_planes.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace sed::assets
{
namespace
{
using Polygon = std::vector<RavenBspVertex>;
using Plane = RavenBspPlane;

[[noreturn]] void refuse(const std::string& message)
{
    throw std::runtime_error("joJaHybrid: " + message);
}

double dot(const TVector& a, const TVector& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

TVector add(const TVector& a, const TVector& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

TVector subtract(const TVector& a, const TVector& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

TVector multiply(const TVector& a, double b)
{
    return {a.x * b, a.y * b, a.z * b};
}

TVector cross(const TVector& a, const TVector& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(const TVector& v)
{
    return std::sqrt(dot(v, v));
}

TVector unit(const TVector& v)
{
    const double magnitude = length(v);
    if ( !std::isfinite(magnitude) || magnitude <= 0 )
    {
        refuse("a plane or polygon has no finite normal");
    }
    return multiply(v, 1.0 / magnitude);
}

Plane opposite(const Plane& plane)
{
    return {multiply(plane.normal, -1), -plane.distance};
}

double distance(const Plane& plane, const TVector& p)
{
    return dot(plane.normal, p) - plane.distance;
}

bool finite(const TVector& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

TVector areaVector(const Polygon& polygon)
{
    TVector result{};
    if ( polygon.size() >= 3 )
    {
        for ( std::size_t i = 1; i + 1 < polygon.size(); ++i )
        {
            result = add(result, cross(subtract(polygon[i].position, polygon[0].position),
                                       subtract(polygon[i + 1].position, polygon[0].position)));
        }
    }
    return multiply(result, 0.5);
}

double area(const Polygon& polygon)
{
    return length(areaVector(polygon));
}

RavenBspVertex interpolate(const RavenBspVertex& a, const RavenBspVertex& b, double t)
{
    const auto lerp = [t](double x, double y)
    {
        return x + t * (y - x);
    };
    RavenBspVertex result;
    result.position = add(a.position, multiply(subtract(b.position, a.position), t));
    result.normal = add(a.normal, multiply(subtract(b.normal, a.normal), t));
    result.u = lerp(a.u, b.u);
    result.v = lerp(a.v, b.v);
    result.lightmapU = lerp(a.lightmapU, b.lightmapU);
    result.lightmapV = lerp(a.lightmapV, b.lightmapV);
    result.color = {static_cast<float>(lerp(a.color.r, b.color.r)), static_cast<float>(lerp(a.color.g, b.color.g)),
                    static_cast<float>(lerp(a.color.b, b.color.b)), static_cast<float>(lerp(a.color.a, b.color.a))};
    return result;
}

struct Work
{
    double epsilon{1e-8};
    std::size_t remaining{500000000};
    std::size_t fragmentLimit{};
    std::size_t fragments{};

    void consume(std::size_t count)
    {
        if ( count > remaining )
        {
            refuse("geometry work budget exceeded; use fewer sectors or a smaller source");
        }
        remaining -= count;
    }

    void fragment(std::size_t count = 1, const char* context = "geometry")
    {
        if ( count > fragmentLimit - fragments )
        {
            refuse("maxFragments exceeded while emitting " + std::string(context) + " after " +
                   std::to_string(fragments) + " fragments; no partial geometry was published");
        }
        fragments += count;
    }
};

void clean(Polygon& polygon, double epsilon)
{
    if ( polygon.empty() )
    {
        return;
    }
    Polygon out;
    for ( const auto& p : polygon )
    {
        if ( out.empty() || length(subtract(p.position, out.back().position)) > epsilon )
        {
            out.push_back(p);
        }
    }
    if ( out.size() > 1 && length(subtract(out.front().position, out.back().position)) <= epsilon )
    {
        out.pop_back();
    }
    if ( out.size() < 3 || area(out) <= epsilon * epsilon )
    {
        out.clear();
    }
    polygon = std::move(out);
}

std::pair<Polygon, Polygon> split(const Polygon& polygon, const Plane& plane, Work& work)
{
    work.consume(polygon.size());
    if ( polygon.empty() )
    {
        return {};
    }
    double low = std::numeric_limits<double>::infinity();
    double high = -low;
    std::vector<double> distances;
    distances.reserve(polygon.size());
    for ( const auto& corner : polygon )
    {
        const double d = distance(plane, corner.position);
        distances.push_back(d);
        low = std::min(low, d);
        high = std::max(high, d);
    }
    // Coplanar polygons belong to the retained half-space exactly once.
    if ( high <= work.epsilon )
    {
        return {polygon, {}};
    }
    if ( low >= -work.epsilon )
    {
        return {{}, polygon};
    }
    Polygon inside, outside;
    for ( std::size_t i = 0; i < polygon.size(); ++i )
    {
        const auto next = (i + 1) % polygon.size();
        const double a = distances[i], b = distances[next];
        if ( a <= work.epsilon )
        {
            inside.push_back(polygon[i]);
        }
        if ( a >= -work.epsilon )
        {
            outside.push_back(polygon[i]);
        }
        if ( (a < -work.epsilon && b > work.epsilon) || (a > work.epsilon && b < -work.epsilon) )
        {
            auto point = interpolate(polygon[i], polygon[next], a / (a - b));
            // Remove the final distance roundoff from the new split coordinate.
            point.position = subtract(point.position, multiply(plane.normal, distance(plane, point.position)));
            inside.push_back(point);
            outside.push_back(point);
        }
    }
    clean(inside, work.epsilon);
    clean(outside, work.epsilon);
    return {std::move(inside), std::move(outside)};
}

Polygon clip(Polygon polygon, std::span<const Plane> planes, Work& work)
{
    for ( const auto& plane : planes )
    {
        polygon = split(polygon, plane, work).first;
        if ( polygon.empty() )
        {
            break;
        }
    }
    return polygon;
}

std::vector<Plane> edgePlanes(const Polygon& polygon)
{
    const auto normal = unit(areaVector(polygon));
    std::vector<Plane> planes;
    for ( std::size_t i = 0; i < polygon.size(); ++i )
    {
        const auto& a = polygon[i].position;
        const auto n = unit(cross(subtract(polygon[(i + 1) % polygon.size()].position, a), normal));
        planes.push_back({n, dot(n, a)});
    }
    return planes;
}

struct PolygonBounds
{
    TVector low, high;

    PolygonBounds(TVector minimum, TVector maximum) : low(minimum), high(maximum)
    {
    }

    explicit PolygonBounds(std::span<const RavenBspVertex> polygon) : low(polygon.front().position), high(low)
    {
        for ( const auto& corner : polygon )
        {
            const auto& p = corner.position;
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    }

    bool overlaps(const PolygonBounds& other, double epsilon) const
    {
        return high.x + epsilon >= other.low.x && other.high.x + epsilon >= low.x && high.y + epsilon >= other.low.y &&
               other.high.y + epsilon >= low.y && high.z + epsilon >= other.low.z && other.high.z + epsilon >= low.z;
    }
};

std::vector<Polygon> difference(Polygon polygon, const Polygon& cutter, Work& work)
{
    std::vector<Polygon> outside;
    if ( polygon.empty() )
    {
        return outside;
    }
    if ( cutter.empty() )
    {
        return {std::move(polygon)};
    }
    if ( !PolygonBounds(polygon).overlaps(PolygonBounds(cutter), work.epsilon) )
    {
        return {std::move(polygon)};
    }
    const auto planes = edgePlanes(cutter);
    // Infinite cutter edge lines can cross a distant face even though their
    // finite polygons do not overlap. Such a subtraction must be an identity,
    // including point/edge contact; otherwise unrelated cuts multiply shell
    // seams, model triangles and the neighboring portal corner counts.
    if ( clip(polygon, planes, work).empty() )
    {
        return {std::move(polygon)};
    }
    for ( const auto& plane : planes )
    {
        auto parts = split(polygon, plane, work);
        if ( !parts.second.empty() )
        {
            outside.push_back(std::move(parts.second));
        }
        polygon = std::move(parts.first);
        if ( polygon.empty() )
        {
            break;
        }
    }
    return outside;
}

Polygon onPlane(const Plane& plane, double extent)
{
    const auto center = multiply(plane.normal, plane.distance);
    const auto u = unit(cross(plane.normal, std::abs(plane.normal.z) < 0.8 ? TVector{0, 0, 1} : TVector{1, 0, 0}));
    const auto v = cross(plane.normal, u);
    Polygon polygon;
    for ( const auto& [a, b] : std::array<std::pair<int, int>, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}} )
    {
        RavenBspVertex corner;
        corner.position = add(center, multiply(add(multiply(u, a), multiply(v, b)), extent));
        polygon.push_back(corner);
    }
    return polygon;
}

struct Boundary
{
    Plane plane;
    Polygon polygon;
    std::vector<Polygon> walls;
    std::vector<Polygon> openings;
    std::vector<Polygon> promoted;
};

struct Cell
{
    std::int32_t root{};
    std::vector<Plane> planes;
    std::vector<Plane> deferredPlanes;
    std::vector<Boundary> boundaries;
    TVector minimum{}, maximum{};
};

bool samePlane(const Plane& a, const Plane& b, double epsilon)
{
    return length(subtract(a.normal, b.normal)) <= 1e-9 && std::abs(a.distance - b.distance) <= epsilon;
}

void makeBoundaries(Cell& cell, double extent, Work& work)
{
    cell.boundaries.clear();
    std::vector<Plane> unique;
    for ( const auto& plane : cell.planes )
    {
        if ( std::none_of(unique.begin(), unique.end(),
                          [&](const auto& p)
                          {
                              return samePlane(p, plane, work.epsilon);
                          }) )
        {
            unique.push_back(plane);
        }
    }
    for ( std::size_t i = 0; i < unique.size(); ++i )
    {
        auto polygon = onPlane(unique[i], extent);
        for ( std::size_t j = 0; j < unique.size() && !polygon.empty(); ++j )
        {
            if ( i != j )
            {
                polygon = split(polygon, unique[j], work).first;
            }
        }
        if ( !polygon.empty() )
        {
            cell.boundaries.push_back({unique[i], polygon, {polygon}, {}, {}});
        }
    }
    if ( cell.boundaries.size() < 4 )
    {
        refuse("a source partition does not bound a finite convex cell");
    }
    cell.planes.clear();
    cell.minimum = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity()};
    cell.maximum = multiply(cell.minimum, -1);
    for ( const auto& boundary : cell.boundaries )
    {
        cell.planes.push_back(boundary.plane);
        for ( const auto& corner : boundary.polygon )
        {
            const auto& p = corner.position;
            cell.minimum = {std::min(cell.minimum.x, p.x), std::min(cell.minimum.y, p.y),
                            std::min(cell.minimum.z, p.z)};
            cell.maximum = {std::max(cell.maximum.x, p.x), std::max(cell.maximum.y, p.y),
                            std::max(cell.maximum.z, p.z)};
        }
    }
}

Polygon clipToCell(const Polygon& polygon, const Cell& cell, Work& work)
{
    if ( std::all_of(polygon.begin(), polygon.end(),
                     [&](const auto& p)
                     {
                         return p.position.x < cell.minimum.x - work.epsilon;
                     }) ||
         std::all_of(polygon.begin(), polygon.end(),
                     [&](const auto& p)
                     {
                         return p.position.x > cell.maximum.x + work.epsilon;
                     }) ||
         std::all_of(polygon.begin(), polygon.end(),
                     [&](const auto& p)
                     {
                         return p.position.y < cell.minimum.y - work.epsilon;
                     }) ||
         std::all_of(polygon.begin(), polygon.end(),
                     [&](const auto& p)
                     {
                         return p.position.y > cell.maximum.y + work.epsilon;
                     }) ||
         std::all_of(polygon.begin(), polygon.end(),
                     [&](const auto& p)
                     {
                         return p.position.z < cell.minimum.z - work.epsilon;
                     }) ||
         std::all_of(polygon.begin(), polygon.end(),
                     [&](const auto& p)
                     {
                         return p.position.z > cell.maximum.z + work.epsilon;
                     }) )
    {
        return {};
    }
    return clip(polygon, cell.planes, work);
}

std::size_t admitDeferredPlanes(Cell& cell, std::span<const JoJaHybridTriangle> triangles, Work& work)
{
    if ( cell.deferredPlanes.empty() )
    {
        return 0;
    }
    std::vector<bool> retain(cell.deferredPlanes.size(), true);
    for ( const auto& triangle : triangles )
    {
        const Polygon source(triangle.triangle.corners.begin(), triangle.triangle.corners.end());
        const auto part = clip(source, cell.planes, work);
        if ( part.empty() )
        {
            continue;
        }
        for ( std::size_t plane = 0; plane < retain.size(); ++plane )
        {
            if ( retain[plane] && std::any_of(part.begin(), part.end(),
                                              [&](const auto& p)
                                              {
                                                  return distance(cell.deferredPlanes[plane], p.position) >
                                                         work.epsilon;
                                              }) )
            {
                retain[plane] = false;
            }
        }
    }
    std::size_t untrimmed = 0;
    for ( std::size_t plane = 0; plane < retain.size(); ++plane )
    {
        if ( retain[plane] )
        {
            cell.planes.push_back(cell.deferredPlanes[plane]);
        }
        else
        {
            ++untrimmed;
        }
    }
    cell.deferredPlanes.clear();
    return untrimmed;
}

bool axial(const Plane& plane)
{
    return (std::abs(plane.normal.x) == 1 && plane.normal.y == 0 && plane.normal.z == 0) ||
           (std::abs(plane.normal.y) == 1 && plane.normal.x == 0 && plane.normal.z == 0) ||
           (std::abs(plane.normal.z) == 1 && plane.normal.x == 0 && plane.normal.y == 0);
}

void refineArchitecture(std::vector<Cell>& cells, const std::vector<JoJaStructuralPlane>& groups,
                        std::span<const JoJaHybridTriangle> triangles, std::size_t maximum, double extent, Work& work)
{
    if ( maximum <= cells.size() )
    {
        return;
    }
    struct Source
    {
        PolygonBounds bounds;
        double area;
    };
    struct Choice
    {
        std::size_t group{};
        double score{};
    };
    struct AxisRanks
    {
        std::vector<double> minima, maxima, flat;
    };
    const auto xyz = [](const TVector& p)
    {
        return std::array{p.x, p.y, p.z};
    };
    std::vector<Source> sources;
    sources.reserve(triangles.size());
    work.consume(triangles.size() * 3);
    for ( const auto& input : triangles )
    {
        const auto& corners = input.triangle.corners;
        sources.push_back(
            {PolygonBounds(corners), 0.5 * length(cross(subtract(corners[1].position, corners[0].position),
                                                        subtract(corners[2].position, corners[0].position)))});
    }
    const auto choose = [&](const Cell& cell)
    {
        Choice best;
        const auto low = xyz(cell.minimum), high = xyz(cell.maximum);
        const PolygonBounds cellBounds(cell.minimum, cell.maximum);
        std::array<AxisRanks, 3> ranks;
        work.consume(sources.size());
        for ( const auto& source : sources )
        {
            if ( !source.bounds.overlaps(cellBounds, work.epsilon) )
            {
                continue;
            }
            const auto minimum = xyz(source.bounds.low), maximum = xyz(source.bounds.high);
            for ( std::size_t axis = 0; axis < ranks.size(); ++axis )
            {
                auto& rank = ranks[axis];
                rank.minima.push_back(minimum[axis]);
                rank.maxima.push_back(maximum[axis]);
                if ( maximum[axis] - minimum[axis] <= work.epsilon )
                {
                    rank.flat.push_back(std::midpoint(minimum[axis], maximum[axis]));
                }
            }
        }
        for ( auto& rank : ranks )
        {
            for ( auto* values : {&rank.minima, &rank.maxima, &rank.flat} )
            {
                work.consume(values->size() * std::bit_width(values->size()));
                std::sort(values->begin(), values->end());
            }
        }
        work.consume(groups.size());
        for ( std::size_t g = 0; g < groups.size(); ++g )
        {
            const auto& group = groups[g];
            if ( !axial(group.plane) )
            {
                continue;
            }
            const auto normal = xyz(group.plane.normal);
            const std::size_t axis = normal[0] != 0 ? 0 : (normal[1] != 0 ? 1 : 2);
            const double cut = group.plane.distance / normal[axis];
            if ( cut <= low[axis] + work.epsilon || cut >= high[axis] - work.epsilon )
            {
                continue;
            }
            // These interval ranks estimate model splits and the smaller side's
            // resident draw load. Near-coplanar triangles count toward neither side.
            const auto& rank = ranks[axis];
            work.consume(4 * std::bit_width(rank.minima.size()));
            const auto left =
                std::upper_bound(rank.maxima.begin(), rank.maxima.end(), cut + work.epsilon) - rank.maxima.begin();
            const auto right =
                rank.minima.end() - std::lower_bound(rank.minima.begin(), rank.minima.end(), cut - work.epsilon);
            const auto coplanar = std::upper_bound(rank.flat.begin(), rank.flat.end(), cut + work.epsilon) -
                                  std::lower_bound(rank.flat.begin(), rank.flat.end(), cut - work.epsilon);
            const auto crossing =
                std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(rank.minima.size()) - left - right + coplanar);
            const auto balance = std::max<std::ptrdiff_t>(0, std::min(left, right) - coplanar);
            if ( balance + 0.35 * group.triangles.size() - 3.5 * crossing <= best.score )
            {
                continue;
            }
            double covered = 0;
            std::size_t native = 0;
            work.consume(group.triangles.size());
            for ( const auto index : group.triangles )
            {
                if ( !sources[index].bounds.overlaps(cellBounds, work.epsilon) )
                {
                    continue;
                }
                const auto& corners = triangles[index].triangle.corners;
                work.consume(cell.planes.size() * corners.size());
                const bool inside = std::all_of(cell.planes.begin(), cell.planes.end(),
                                                [&](const auto& plane)
                                                {
                                                    return std::all_of(corners.begin(), corners.end(),
                                                                       [&](const auto& p)
                                                                       {
                                                                           return distance(plane, p.position) <= 0;
                                                                       });
                                                });
                const double clippedArea = inside
                                               ? sources[index].area
                                               : area(clipToCell(Polygon(corners.begin(), corners.end()), cell, work));
                if ( clippedArea > work.epsilon * work.epsilon )
                {
                    covered += clippedArea;
                    ++native;
                }
            }
            if ( native == 0 )
            {
                continue;
            }
            const auto u = (axis + 1) % 3, v = (axis + 2) % 3;
            const double occupancy = std::min(1.0, covered / ((high[u] - low[u]) * (high[v] - low[v])));
            // Heuristic in triangle units, not a runtime frame-rate estimate.
            // A balanced cut gets 12% credit even with a small solid footprint.
            // Additional coverage supplies up to 88%; each eligible native face
            // contributes 0.35 and each predicted split costs 3.5.
            const double score = balance * (0.12 + 0.88 * occupancy) + 0.35 * native - 3.5 * crossing;
            if ( score > best.score )
            {
                best = {g, score};
            }
        }
        return best;
    };
    std::vector<Choice> choices{choose(cells[0])};
    while ( cells.size() < maximum )
    {
        const auto picked = std::max_element(choices.begin(), choices.end(),
                                             [](const auto& a, const auto& b)
                                             {
                                                 return a.score < b.score;
                                             });
        if ( picked == choices.end() || picked->score <= 0 )
        {
            break;
        }
        const auto index = static_cast<std::size_t>(picked - choices.begin());
        const auto plane = groups[picked->group].plane;
        Cell front = cells[index], back = cells[index];
        front.planes.push_back(opposite(plane));
        back.planes.push_back(plane);
        makeBoundaries(front, extent, work);
        makeBoundaries(back, extent, work);
        cells[index] = std::move(front);
        cells.push_back(std::move(back));
        if ( cells.size() < maximum )
        {
            choices[index] = choose(cells[index]);
            choices.push_back(choose(cells.back()));
        }
    }
}

struct Tree
{
    const RavenBsp& bsp;
    std::vector<Plane> planes;
    std::vector<std::size_t> counts;
    std::vector<bool> allOpen;
    std::vector<double> weights;
    std::vector<std::int32_t> order;

    explicit Tree(const RavenBsp& input, std::span<const JoJaHybridTriangle> triangles) : bsp(input)
    {
        if ( bsp.nodes.empty() || bsp.leaves.empty() || bsp.models.empty() || bsp.nodes.size() > 100000 )
        {
            refuse("hybrid mode requires bounded world BSP nodes, leaves and model bounds");
        }
        for ( const auto& plane : bsp.planes )
        {
            const double size = length(plane.normal);
            if ( !std::isfinite(plane.distance) || !std::isfinite(size) || size < 1e-12 )
            {
                refuse("invalid source partition plane");
            }
            planes.push_back({multiply(plane.normal, 1.0 / size), plane.distance / size});
        }
        std::vector<std::int32_t> pending{0};
        std::set<std::int32_t> visited;
        while ( !pending.empty() )
        {
            const auto index = pending.back();
            pending.pop_back();
            if ( !visited.insert(index).second )
            {
                refuse("source partition graph repeats a child");
            }
            if ( index < 0 )
            {
                if ( static_cast<std::size_t>(-1LL - index) >= bsp.leaves.size() )
                {
                    refuse("source partition references an absent leaf");
                }
                continue;
            }
            if ( static_cast<std::size_t>(index) >= bsp.nodes.size() )
            {
                refuse("source partition references an absent node");
            }
            const auto& node = bsp.nodes[static_cast<std::size_t>(index)];
            if ( node.plane < 0 || static_cast<std::size_t>(node.plane) >= planes.size() )
            {
                refuse("source partition references an absent plane");
            }
            order.push_back(index);
            pending.push_back(node.children[1]);
            pending.push_back(node.children[0]);
        }
        counts.resize(bsp.nodes.size());
        allOpen.resize(bsp.nodes.size());
        weights.resize(bsp.nodes.size());
        std::vector<double> surfaceWeights(bsp.surfaces.size());
        for ( const auto& triangle : triangles )
        {
            if ( triangle.triangle.surface >= surfaceWeights.size() )
            {
                refuse("a triangle references an absent source surface");
            }
            surfaceWeights[triangle.triangle.surface] += 1;
        }
        leafWeights.resize(bsp.leaves.size(), 1);
        for ( std::size_t i = 0; i < bsp.leaves.size(); ++i )
        {
            const auto& leaf = bsp.leaves[i];
            for ( std::int32_t j = 0; j < leaf.leafSurfaceCount; ++j )
            {
                const auto at = static_cast<std::int64_t>(leaf.firstLeafSurface) + j;
                if ( at < 0 || static_cast<std::size_t>(at) >= bsp.leafSurfaces.size() )
                {
                    refuse("invalid leaf surface span");
                }
                const auto surface = bsp.leafSurfaces[static_cast<std::size_t>(at)];
                if ( surface < 0 || static_cast<std::size_t>(surface) >= surfaceWeights.size() )
                {
                    refuse("invalid leaf surface reference");
                }
                leafWeights[i] += surfaceWeights[static_cast<std::size_t>(surface)];
            }
        }
        for ( auto it = order.rbegin(); it != order.rend(); ++it )
        {
            const auto& node = bsp.nodes[static_cast<std::size_t>(*it)];
            counts[static_cast<std::size_t>(*it)] = count(node.children[0]) + count(node.children[1]);
            allOpen[static_cast<std::size_t>(*it)] = entirelyOpen(node.children[0]) && entirelyOpen(node.children[1]);
            weights[static_cast<std::size_t>(*it)] = weight(node.children[0]) + weight(node.children[1]);
        }
        if ( !counts[0] )
        {
            refuse("source partition has no nonnegative visibility leaves");
        }
    }

    std::vector<double> leafWeights;

    std::size_t count(std::int32_t index) const
    {
        return index >= 0 ? counts[static_cast<std::size_t>(index)]
                          : static_cast<std::size_t>(bsp.leaves[static_cast<std::size_t>(-1LL - index)].cluster >= 0);
    }

    double weight(std::int32_t index) const
    {
        return index >= 0     ? weights[static_cast<std::size_t>(index)]
               : count(index) ? leafWeights[static_cast<std::size_t>(-1LL - index)]
                              : 0;
    }

    bool entirelyOpen(std::int32_t index) const
    {
        return index >= 0 ? allOpen[static_cast<std::size_t>(index)] : count(index) != 0;
    }

    void tighten(Cell& cell) const
    {
        while ( cell.root >= 0 )
        {
            if ( cell.planes.size() + cell.deferredPlanes.size() > 256 )
            {
                refuse("source partition depth exceeds the geometry work bound");
            }
            const auto& node = bsp.nodes[static_cast<std::size_t>(cell.root)];
            const bool front = count(node.children[0]) != 0, back = count(node.children[1]) != 0;
            if ( front && back )
            {
                break;
            }
            if ( !front && !back )
            {
                refuse("empty source partition selected as an occupied cell");
            }
            const auto& plane = planes[static_cast<std::size_t>(node.plane)];
            // Negative visibility leaves can still contain authored draw
            // triangles. Defer tightening until actual geometric coverage has
            // proved this plane will not cut away any admitted source detail.
            cell.deferredPlanes.push_back(front ? opposite(plane) : plane);
            cell.root = node.children[front ? 0 : 1];
        }
    }

    std::vector<Polygon> positivePieces(const Polygon& polygon, const TVector& direction, Work& work) const
    {
        std::vector<Polygon> output;
        std::vector<std::pair<std::int32_t, Polygon>> pending{{0, polygon}};
        while ( !pending.empty() )
        {
            auto [index, piece] = std::move(pending.back());
            pending.pop_back();
            work.consume(piece.size());
            if ( piece.empty() || !count(index) )
            {
                continue;
            }
            if ( entirelyOpen(index) )
            {
                // Visibility leaf seams inside an entirely occupied subtree
                // do not delimit an aperture. Keep their union intact instead
                // of rebuilding it from many artificial clipping fragments.
                output.push_back(std::move(piece));
                continue;
            }
            const auto& node = bsp.nodes[static_cast<std::size_t>(index)];
            const auto& plane = planes[static_cast<std::size_t>(node.plane)];
            const bool coplanar = std::all_of(piece.begin(), piece.end(),
                                              [&](const auto& p)
                                              {
                                                  return std::abs(distance(plane, p.position)) <= work.epsilon;
                                              });
            if ( coplanar )
            {
                pending.emplace_back(node.children[dot(direction, plane.normal) >= 0 ? 0 : 1], std::move(piece));
            }
            else
            {
                auto [back, front] = split(piece, plane, work);
                if ( !back.empty() )
                {
                    pending.emplace_back(node.children[1], std::move(back));
                }
                if ( !front.empty() )
                {
                    pending.emplace_back(node.children[0], std::move(front));
                }
            }
        }
        return output;
    }
};

Polygon convexUnion(const Polygon& a, const Polygon& b, Work& work)
{
    // A convex hull is a valid union only when it adds no area. This rejects
    // gaps, L-shaped unions and disjoint portal openings.
    work.consume(a.size() * b.size());
    const auto normal = unit(areaVector(a));
    const int axis = std::abs(normal.x) > std::abs(normal.y) ? (std::abs(normal.x) > std::abs(normal.z) ? 0 : 2)
                                                             : (std::abs(normal.y) > std::abs(normal.z) ? 1 : 2);
    const auto xy = [axis](const RavenBspVertex& p)
    {
        return axis == 0   ? std::pair{p.position.y, p.position.z}
               : axis == 1 ? std::pair{p.position.z, p.position.x}
                           : std::pair{p.position.x, p.position.y};
    };
    Polygon points = a;
    points.insert(points.end(), b.begin(), b.end());
    std::sort(points.begin(), points.end(),
              [&](const auto& p, const auto& q)
              {
                  return xy(p) < xy(q);
              });
    points.erase(std::unique(points.begin(), points.end(),
                             [&](const auto& p, const auto& q)
                             {
                                 return length(subtract(p.position, q.position)) <= work.epsilon;
                             }),
                 points.end());
    const auto turn = [&](const auto& p, const auto& q, const auto& r)
    {
        const auto [px, py] = xy(p);
        const auto [qx, qy] = xy(q);
        const auto [rx, ry] = xy(r);
        return (qx - px) * (ry - py) - (qy - py) * (rx - px);
    };
    Polygon hull;
    for ( const auto& p : points )
    {
        while ( hull.size() >= 2 && turn(hull[hull.size() - 2], hull.back(), p) <= 0 )
        {
            hull.pop_back();
        }
        hull.push_back(p);
    }
    const auto lower = hull.size();
    for ( auto it = points.rbegin() + 1; it != points.rend(); ++it )
    {
        while ( hull.size() > lower && turn(hull[hull.size() - 2], hull.back(), *it) <= 0 )
        {
            hull.pop_back();
        }
        hull.push_back(*it);
    }
    if ( !hull.empty() )
    {
        hull.pop_back();
    }
    const double total = area(a) + area(b);
    if ( hull.size() < 3 || std::abs(area(hull) - total) > std::max(1e-8, total * 1e-10) )
    {
        return {};
    }
    if ( dot(areaVector(hull), normal) < 0 )
    {
        std::reverse(hull.begin(), hull.end());
    }
    return hull;
}

void mergeConvexPieces(std::vector<Polygon>& pieces, Work& work)
{
    // A pair of disjoint boxes cannot share an edge. Cache each box for
    // the repeated merge passes and charge even the inexpensive rejections.
    std::vector<PolygonBounds> bounds;
    bounds.reserve(pieces.size());
    for ( const auto& piece : pieces )
    {
        bounds.emplace_back(piece);
    }
    bool changed = true;
    while ( changed )
    {
        changed = false;
        for ( std::size_t i = 0; i < pieces.size(); ++i )
        {
            for ( std::size_t j = i + 1; j < pieces.size(); ++j )
            {
                work.consume(1);
                if ( !bounds[i].overlaps(bounds[j], work.epsilon) )
                {
                    continue;
                }
                auto joined = convexUnion(pieces[i], pieces[j], work);
                if ( !joined.empty() )
                {
                    bounds[i] = PolygonBounds(joined);
                    bounds.erase(bounds.begin() + static_cast<std::ptrdiff_t>(j));
                    pieces[i] = std::move(joined);
                    pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(j));
                    changed = true;
                    --j;
                }
            }
        }
    }
}

void removeFromWalls(Boundary& boundary, const Polygon& portal, Work& work)
{
    std::vector<Polygon> remaining;
    for ( const auto& wall : boundary.walls )
    {
        auto pieces = difference(wall, portal, work);
        remaining.insert(remaining.end(), std::make_move_iterator(pieces.begin()),
                         std::make_move_iterator(pieces.end()));
        if ( remaining.size() > work.fragmentLimit )
        {
            refuse("intermediate wall fragmentation exceeds maxFragments");
        }
    }
    boundary.walls = std::move(remaining);
}

struct Portal
{
    std::size_t a{}, b{};
    Polygon polygon; // Outward from a, inward to b.
};

bool onBoundary(const Polygon& polygon, const Plane& plane, double epsilon)
{
    return std::all_of(polygon.begin(), polygon.end(),
                       [&](const auto& p)
                       {
                           return std::abs(distance(plane, p.position)) <= epsilon;
                       });
}

std::vector<Polygon> triangulate(const Polygon& polygon)
{
    std::vector<Polygon> result;
    for ( std::size_t i = 1; i + 1 < polygon.size(); ++i )
    {
        Polygon part{polygon[0], polygon[i], polygon[i + 1]};
        if ( area(part) > 0 )
        {
            result.push_back(std::move(part));
        }
    }
    return result;
}

} // namespace

bool joJaHybridContains(const JoJaHybridCell& cell, const TVector& point, double tolerance) noexcept
{
    if ( !finite(point) || cell.planes.size() < 4 )
    {
        return false;
    }
    return std::all_of(cell.planes.begin(), cell.planes.end(),
                       [&](const auto& plane)
                       {
                           return distance(plane, point) <= tolerance;
                       });
}

JoJaHybridGeometry buildJoJaHybridGeometry(const RavenBsp& bsp, std::span<const JoJaHybridTriangle> triangles,
                                           const JoJaHybridOptions& options)
{
    if ( !std::isfinite(options.scale) || options.scale <= 0 || options.scale > 1 || options.maxSectors < 1 ||
         options.maxSectors > 96 || options.maxFragments < 1 || options.maxFragments > 1000000 || triangles.empty() )
    {
        refuse("invalid hybrid scale, sector budget, fragment budget or empty triangle input");
    }
    Tree tree(bsp, triangles);
    TVector low = bsp.models[0].minimum, high = bsp.models[0].maximum;
    if ( !finite(low) || !finite(high) )
    {
        refuse("world bounds are not finite");
    }
    for ( const auto& triangle : triangles )
    {
        for ( const auto& corner : triangle.triangle.corners )
        {
            if ( !finite(corner.position) || !finite(corner.normal) || !std::isfinite(corner.u) ||
                 !std::isfinite(corner.v) || !std::isfinite(corner.color.r) || !std::isfinite(corner.color.g) ||
                 !std::isfinite(corner.color.b) || !std::isfinite(corner.color.a) )
            {
                refuse("source geometry contains a nonfinite corner");
            }
            const auto& p = corner.position;
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    }
    const double extent = 4 * std::max({std::abs(low.x), std::abs(low.y), std::abs(low.z), std::abs(high.x),
                                        std::abs(high.y), std::abs(high.z), 32.0}) +
                          128;
    Work work;
    work.epsilon = std::max(1e-8, extent * 1e-12);
    work.fragmentLimit = options.maxFragments;
    std::vector<Cell> cells(1);
    cells[0].planes = {{{-1, 0, 0}, -low.x + 32}, {{1, 0, 0}, high.x + 32},  {{0, -1, 0}, -low.y + 32},
                       {{0, 1, 0}, high.y + 32},  {{0, 0, -1}, -low.z + 32}, {{0, 0, 1}, high.z + 32}};
    tree.tighten(cells[0]);
    const Cell bspRoot = cells[0];
    JoJaStructuralBrushIndex structuralIndex(bsp);
    const auto structuralPlanes = structuralIndex.planes(triangles);
    bool architecture = std::any_of(structuralPlanes.begin(), structuralPlanes.end(),
                                    [](const auto& group)
                                    {
                                        return axial(group.plane);
                                    });
    std::size_t untrimmed = 0;
    if ( architecture )
    {
        untrimmed = admitDeferredPlanes(cells[0], triangles, work);
        makeBoundaries(cells[0], extent, work);
        refineArchitecture(cells, structuralPlanes, triangles, options.maxSectors, extent, work);
        if ( cells.size() == 1 )
        {
            // A brush-supported outer floor is useful native artwork, but
            // supplies no interior division. Preserve the source room graph.
            cells = {bspRoot};
            architecture = false;
            untrimmed = 0;
        }
    }
    while ( !architecture && cells.size() < options.maxSectors )
    {
        std::optional<std::size_t> chosen;
        for ( std::size_t i = 0; i < cells.size(); ++i )
        {
            if ( cells[i].root >= 0 && (!chosen || tree.weight(cells[i].root) > tree.weight(cells[*chosen].root)) )
            {
                chosen = i;
            }
        }
        if ( !chosen )
        {
            break;
        }
        const auto& node = bsp.nodes[static_cast<std::size_t>(cells[*chosen].root)];
        const auto plane = tree.planes[static_cast<std::size_t>(node.plane)];
        auto front = cells[*chosen], back = cells[*chosen];
        front.root = node.children[0];
        front.planes.push_back(opposite(plane));
        back.root = node.children[1];
        back.planes.push_back(plane);
        tree.tighten(front);
        tree.tighten(back);
        cells[*chosen] = std::move(front);
        cells.push_back(std::move(back));
    }
    JoJaHybridGeometry result;
    result.untrimmedBoundaryPlanes = untrimmed;
    result.cells.resize(cells.size());
    for ( std::size_t i = 0; i < cells.size(); ++i )
    {
        auto& cell = cells[i];
        result.untrimmedBoundaryPlanes += admitDeferredPlanes(cell, triangles, work);
        makeBoundaries(cells[i], extent, work);
        auto& output = result.cells[i];
        std::vector<std::int32_t> pending{cells[i].root};
        while ( !pending.empty() )
        {
            const auto index = pending.back();
            pending.pop_back();
            if ( index < 0 )
            {
                if ( tree.count(index) )
                {
                    const auto leaf = static_cast<std::size_t>(-1LL - index);
                    const auto& source = bsp.leaves[leaf];
                    if ( !architecture || (source.maximum[0] >= cell.minimum.x && source.minimum[0] <= cell.maximum.x &&
                                           source.maximum[1] >= cell.minimum.y && source.minimum[1] <= cell.maximum.y &&
                                           source.maximum[2] >= cell.minimum.z && source.minimum[2] <= cell.maximum.z) )
                    {
                        output.sourceLeaves.push_back(leaf);
                    }
                }
            }
            else
            {
                const auto& node = bsp.nodes[static_cast<std::size_t>(index)];
                pending.push_back(node.children[1]);
                pending.push_back(node.children[0]);
            }
        }
        std::size_t corners = 0;
        for ( const auto& boundary : cells[i].boundaries )
        {
            output.planes.push_back({boundary.plane.normal, boundary.plane.distance * options.scale});
            for ( const auto& corner : boundary.polygon )
            {
                output.interior = add(output.interior, multiply(corner.position, options.scale));
                ++corners;
            }
        }
        output.interior = multiply(output.interior, 1.0 / static_cast<double>(corners));
    }

    std::vector<Portal> portals;
    for ( std::size_t a = 0; a < cells.size(); ++a )
    {
        for ( std::size_t b = a + 1; b < cells.size(); ++b )
        {
            for ( auto& fa : cells[a].boundaries )
            {
                for ( auto& fb : cells[b].boundaries )
                {
                    if ( !samePlane(fa.plane, opposite(fb.plane), work.epsilon) )
                    {
                        continue;
                    }
                    const auto interface = clip(fa.polygon, cells[b].planes, work);
                    if ( interface.empty() )
                    {
                        continue;
                    }
                    std::vector<Polygon> openings;
                    for ( const auto& front : tree.positivePieces(interface, fa.plane.normal, work) )
                    {
                        for ( auto& shared : tree.positivePieces(front, multiply(fa.plane.normal, -1), work) )
                        {
                            openings.push_back(std::move(shared));
                        }
                    }
                    // Detail brushes can occupy a positive visibility leaf.
                    // Intersect their actual finite solids with the aperture;
                    // a missing draw face must not create a passable portal
                    // through the middle of an opaque static column or wall.
                    std::vector<TVector> aperture;
                    aperture.reserve(interface.size());
                    for ( const auto& corner : interface )
                    {
                        aperture.push_back(corner.position);
                    }
                    for ( const auto& section :
                          structuralIndex.crossSections(fa.plane, aperture, options.maxFragments) )
                    {
                        Polygon cutter;
                        for ( const auto& p : section )
                        {
                            RavenBspVertex corner;
                            corner.position = p;
                            cutter.push_back(corner);
                        }
                        std::vector<Polygon> left;
                        for ( const auto& opening : openings )
                        {
                            auto parts = difference(opening, cutter, work);
                            left.insert(left.end(), std::make_move_iterator(parts.begin()),
                                        std::make_move_iterator(parts.end()));
                        }
                        if ( left.size() > options.maxFragments )
                        {
                            refuse("solid aperture fragmentation exceeds maxFragments");
                        }
                        openings = std::move(left);
                        if ( openings.empty() )
                        {
                            break;
                        }
                    }
                    // A verified static opaque face at this interface is a
                    // real wall/floor, even when both sides occupy the same
                    // source visibility leaf. Remove its footprint once from
                    // the shared aperture; native promotion below textures the
                    // front side and retains the opposite closed shell.
                    for ( const auto& group : structuralPlanes )
                    {
                        if ( !samePlane(fa.plane, group.plane, work.epsilon) &&
                             !samePlane(fa.plane, opposite(group.plane), work.epsilon) )
                        {
                            continue;
                        }
                        for ( const auto index : group.triangles )
                        {
                            const auto& source = triangles[index].triangle.corners;
                            const Polygon face(source.begin(), source.end());
                            if ( !onBoundary(face, fa.plane, work.epsilon) )
                            {
                                continue;
                            }
                            const auto cutter = clip(face, edgePlanes(interface), work);
                            if ( cutter.empty() )
                            {
                                continue;
                            }
                            std::vector<Polygon> left;
                            for ( const auto& opening : openings )
                            {
                                auto parts = difference(opening, cutter, work);
                                left.insert(left.end(), std::make_move_iterator(parts.begin()),
                                            std::make_move_iterator(parts.end()));
                            }
                            if ( left.size() > options.maxFragments )
                            {
                                refuse("native aperture fragmentation exceeds maxFragments");
                            }
                            openings = std::move(left);
                        }
                    }
                    mergeConvexPieces(openings, work);
                    for ( auto& opening : openings )
                    {
                        work.fragment(2, "paired portals");
                        removeFromWalls(fa, opening, work);
                        removeFromWalls(fb, opening, work);
                        fa.openings.push_back(opening);
                        fb.openings.push_back(opening);
                        portals.push_back({a, b, std::move(opening)});
                    }
                }
            }
        }
    }

    const auto addFace = [&](std::size_t sector, Polygon polygon, const TVector& normal,
                             std::optional<std::size_t> material, std::size_t sourceSurface)
    {
        if ( dot(areaVector(polygon), normal) < 0 )
        {
            std::reverse(polygon.begin(), polygon.end());
        }
        for ( auto& corner : polygon )
        {
            corner.position = multiply(corner.position, options.scale);
        }
        work.fragment(1, material ? "native faces" : "untextured shell faces");
        result.cells[sector].faces.push_back({std::move(polygon), normal, material, sourceSurface, -1, -1});
    };

    // Precision can make a valid source triangle unsuitable for a native
    // world boundary while its local model coordinates remain representable.
    // Retry placement, keeping all genuine portal masks and source partitions.
    const auto placementCells = cells;
    const auto placementResult = result;
    const auto placementFragments = work.fragments;
    std::set<std::size_t> residualOnly;
    for ( std::size_t retry = 0;; ++retry )
    {
        try
        {
            for ( std::size_t inputIndex = 0; inputIndex < triangles.size(); ++inputIndex )
            {
                const auto& input = triangles[inputIndex];
                const Polygon source(input.triangle.corners.begin(), input.triangle.corners.end());
                const double sourceArea = area(source);
                if ( !std::isfinite(sourceArea) || sourceArea <= work.epsilon * work.epsilon )
                {
                    refuse("a source triangle is degenerate before partitioning");
                }
                const auto sourceNormal = unit(areaVector(source));
                double outputArea = 0;
                std::size_t fragments = 0;
                struct Candidate
                {
                    std::size_t sector{};
                    Polygon polygon;
                    bool reverseBoundary{};
                };
                std::vector<Candidate> candidates;
                for ( std::size_t sector = 0; sector < cells.size(); ++sector )
                {
                    const auto& cell = cells[sector];
                    auto part = clipToCell(source, cell, work);
                    if ( part.empty() )
                    {
                        continue;
                    }
                    const bool reverseBoundary = std::any_of(cell.planes.begin(), cell.planes.end(),
                                                             [&](const auto& plane)
                                                             {
                                                                 return dot(plane.normal, sourceNormal) > 0.999999 &&
                                                                        onBoundary(part, plane, work.epsilon);
                                                             });
                    candidates.push_back({sector, std::move(part), reverseBoundary});
                }
                // Prefer the inward side of a shared plane. If no opposite owner
                // exists, retain reverse-facing detail in the sole containing cell.
                std::stable_sort(candidates.begin(), candidates.end(),
                                 [](const auto& a, const auto& b)
                                 {
                                     return a.reverseBoundary < b.reverseBoundary;
                                 });
                std::vector<Polygon> claimed;
                std::vector<Candidate> owned;
                for ( auto& candidate : candidates )
                {
                    std::vector<Polygon> remaining{std::move(candidate.polygon)};
                    for ( const auto& previous : claimed )
                    {
                        std::vector<Polygon> next;
                        for ( const auto& part : remaining )
                        {
                            if ( clip(part, edgePlanes(previous), work).empty() )
                            {
                                next.push_back(part);
                            }
                            else
                            {
                                auto pieces = difference(part, previous, work);
                                next.insert(next.end(), std::make_move_iterator(pieces.begin()),
                                            std::make_move_iterator(pieces.end()));
                            }
                        }
                        remaining = std::move(next);
                    }
                    for ( auto& part : remaining )
                    {
                        claimed.push_back(part);
                        owned.push_back({candidate.sector, std::move(part), candidate.reverseBoundary});
                    }
                }
                for ( const auto& candidate : owned )
                {
                    const auto sector = candidate.sector;
                    auto& cell = cells[sector];
                    const auto& part = candidate.polygon;
                    ++fragments;
                    std::vector<Polygon> residual{part};
                    const auto& sourceSurface = bsp.surfaces[input.triangle.surface];
                    const auto& shader = bsp.shaders.at(static_cast<std::size_t>(sourceSurface.shader));
                    if ( sourceSurface.type == 1 && (shader.contents & 1u) != 0 &&
                         (shader.contents & 0x80000000u) == 0 && !residualOnly.contains(input.triangle.surface) )
                    {
                        for ( auto& boundary : cell.boundaries )
                        {
                            if ( dot(boundary.plane.normal, sourceNormal) > -0.999999 ||
                                 !onBoundary(part, boundary.plane, work.epsilon) )
                            {
                                continue;
                            }
                            // Cut against the holes and already authored overlays,
                            // not each fragment of the invisible background mask.
                            // The latter needlessly diced every texture triangle and
                            // amplified near-collinear wall remnants on large maps.
                            std::vector<Polygon> nativePieces{part};
                            const auto exclude = [&](const std::vector<Polygon>& cutters)
                            {
                                for ( const auto& cutter : cutters )
                                {
                                    std::vector<Polygon> remaining;
                                    for ( const auto& native : nativePieces )
                                    {
                                        auto pieces = difference(native, cutter, work);
                                        remaining.insert(remaining.end(), std::make_move_iterator(pieces.begin()),
                                                         std::make_move_iterator(pieces.end()));
                                    }
                                    nativePieces = std::move(remaining);
                                    if ( nativePieces.empty() )
                                    {
                                        break;
                                    }
                                }
                            };
                            exclude(boundary.openings);
                            exclude(boundary.promoted);
                            for ( const auto& native : nativePieces )
                            {
                                addFace(sector, native, sourceNormal, input.material, input.triangle.surface);
                                outputArea +=
                                    area(result.cells[sector].faces.back().corners) / (options.scale * options.scale);
                                ++result.nativeFaces;
                                boundary.promoted.push_back(native);
                                std::vector<Polygon> left;
                                for ( const auto& piece : residual )
                                {
                                    auto parts = difference(piece, native, work);
                                    left.insert(left.end(), std::make_move_iterator(parts.begin()),
                                                std::make_move_iterator(parts.end()));
                                }
                                residual = std::move(left);
                            }
                            break;
                        }
                    }
                    for ( const auto& polygon : residual )
                    {
                        // Each clipped piece of an original triangle still has the
                        // same affine texture and lighting. Keep its convex model
                        // face intact, retaining the fan only for coverage/workload
                        // accounting. Larger rings share a fan diagonal at 24 corners.
                        for ( std::size_t first = 1; first + 1 < polygon.size(); )
                        {
                            const auto last = std::min(first + 22, polygon.size() - 1);
                            Polygon face{polygon.front()};
                            face.insert(face.end(), polygon.begin() + static_cast<std::ptrdiff_t>(first),
                                        polygon.begin() + static_cast<std::ptrdiff_t>(last + 1));
                            if ( area(face) > 0 )
                            {
                                for ( auto& corner : face )
                                {
                                    corner.position = multiply(corner.position, options.scale);
                                }
                                result.residualPolygons.push_back(
                                    {std::move(face), input.material, sector, input.triangle.surface});
                            }
                            first = last;
                        }
                        for ( const auto& triangle : triangulate(polygon) )
                        {
                            work.fragment(1, "residual triangles");
                            JoJaHybridTriangle out;
                            out.material = input.material;
                            out.sector = sector;
                            out.triangle.surface = input.triangle.surface;
                            for ( std::size_t j = 0; j < 3; ++j )
                            {
                                out.triangle.corners[j] = triangle[j];
                                out.triangle.corners[j].position = multiply(triangle[j].position, options.scale);
                            }
                            result.residual.push_back(std::move(out));
                            const auto& emitted = result.residual.back().triangle.corners;
                            outputArea +=
                                area(Polygon(emitted.begin(), emitted.end())) / (options.scale * options.scale);
                        }
                    }
                }
                const double relative = std::abs(outputArea - sourceArea) / sourceArea;
                result.maximumRelativeCoverageError = std::max(result.maximumRelativeCoverageError, relative);
                if ( relative > 1e-6 && std::abs(outputArea - sourceArea) > work.epsilon * work.epsilon * 16 )
                {
                    refuse("source triangle " + std::to_string(inputIndex) + " (surface " +
                           std::to_string(input.triangle.surface) +
                           ") is not covered exactly once by the selected cells");
                }
                result.sourceArea += sourceArea * options.scale * options.scale;
                result.outputArea += outputArea * options.scale * options.scale;
                result.splitSourceTriangles += fragments > 1;
            }

            for ( std::size_t sector = 0; sector < cells.size(); ++sector )
            {
                for ( auto& boundary : cells[sector].boundaries )
                {
                    for ( const auto& native : boundary.promoted )
                    {
                        removeFromWalls(boundary, native, work);
                    }
                    for ( const auto& wall : boundary.walls )
                    {
                        addFace(sector, wall, multiply(boundary.plane.normal, -1), std::nullopt, 0);
                    }
                }
            }
            for ( const auto& portal : portals )
            {
                const auto normal = unit(areaVector(portal.polygon));
                const auto a = result.cells[portal.a].faces.size(), b = result.cells[portal.b].faces.size();
                addFace(portal.a, portal.polygon, multiply(normal, -1), std::nullopt, 0);
                addFace(portal.b, portal.polygon, normal, std::nullopt, 0);
                auto& first = result.cells[portal.a].faces.back();
                auto& second = result.cells[portal.b].faces.back();
                first.neighbor = static_cast<std::int32_t>(portal.b);
                first.mirror = static_cast<std::int32_t>(b);
                second.neighbor = static_cast<std::int32_t>(portal.a);
                second.mirror = static_cast<std::int32_t>(a);
                ++result.portalPairs;
            }
            finalizeJoJaHybridGeometry(result, options.maxFragments);
            result.nativePromotionFallbacks = residualOnly.size();
            return result;
        }
        catch ( const JoJaNativePromotionError& error )
        {
            const auto previous = residualOnly.size();
            for ( const auto source : error.sourceSurfaces )
            {
                if ( source >= bsp.surfaces.size() )
                {
                    throw;
                }
                residualOnly.insert(source);
            }
            if ( retry >= 8 || residualOnly.size() == previous )
            {
                throw;
            }
            cells = placementCells;
            result = placementResult;
            work.fragments = placementFragments;
            // The shared work budget is deliberately not reset across retries.
        }
    }
}

} // namespace sed::assets
