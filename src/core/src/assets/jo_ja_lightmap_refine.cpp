#include "sed/assets/jo_ja_lightmap_refine.hpp"

#include "sed/assets/raven_lightmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace sed::assets
{
namespace
{

// Bisection halves the marked edges on every pass. A source map spans at most 2^17 units and the minimum
// edge is at least one unit, so a conforming refinement finishes in far fewer passes than this bound.
constexpr std::size_t kMaximumPasses = 128;

using Point = std::array<double, 3>;
// Endpoints in lexicographic order, so both triangles that share an edge find the same key.
using EdgeKey = std::pair<Point, Point>;

Point pointOf(const TVector& position)
{
    return {position.x, position.y, position.z};
}

EdgeKey edgeKey(const RavenBspTriangle& triangle, std::size_t edge)
{
    auto a = pointOf(triangle.corners[edge].position);
    auto b = pointOf(triangle.corners[(edge + 1) % 3].position);
    if ( b < a )
    {
        std::swap(a, b);
    }
    return {a, b};
}

double edgeLengthSquared(const RavenBspTriangle& triangle, std::size_t edge)
{
    const auto& a = triangle.corners[edge].position;
    const auto& b = triangle.corners[(edge + 1) % 3].position;
    const double x = b.x - a.x, y = b.y - a.y, z = b.z - a.z;
    return x * x + y * y + z * z;
}

// Equal lengths go to the smaller key, so the choice depends only on the triangle's positions.
std::size_t longestEdge(const RavenBspTriangle& triangle)
{
    std::size_t best = 0;
    for ( std::size_t edge = 1; edge < 3; ++edge )
    {
        const double length = edgeLengthSquared(triangle, edge), bestLength = edgeLengthSquared(triangle, best);
        if ( length > bestLength || (length == bestLength && edgeKey(triangle, edge) < edgeKey(triangle, best)) )
        {
            best = edge;
        }
    }
    return best;
}

RavenBspVertex midpoint(const RavenBspVertex& a, const RavenBspVertex& b)
{
    RavenBspVertex m;
    m.position = {(a.position.x + b.position.x) * 0.5, (a.position.y + b.position.y) * 0.5,
                  (a.position.z + b.position.z) * 0.5};
    m.u = (a.u + b.u) * 0.5;
    m.v = (a.v + b.v) * 0.5;
    m.lightmapU = (a.lightmapU + b.lightmapU) * 0.5;
    m.lightmapV = (a.lightmapV + b.lightmapV) * 0.5;
    m.color = {(a.color.r + b.color.r) * 0.5f, (a.color.g + b.color.g) * 0.5f, (a.color.b + b.color.b) * 0.5f,
               (a.color.a + b.color.a) * 0.5f};
    const auto& n = a.normal;
    const auto& o = b.normal;
    if ( n.x == o.x && n.y == o.y && n.z == o.z )
    {
        // A flat surface keeps its stored normal exactly.
        m.normal = n;
        return m;
    }
    const TVector sum{n.x + o.x, n.y + o.y, n.z + o.z};
    const double length = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
    if ( length > 1e-12 && std::isfinite(length) )
    {
        m.normal = {sum.x / length, sum.y / length, sum.z / length};
    }
    else
    {
        m.normal = pointOf(a.position) < pointOf(b.position) ? n : o;
    }
    return m;
}

// Replace a triangle by the two halves across one of its edges, keeping its winding.
std::pair<RavenBspTriangle, RavenBspTriangle> bisect(const RavenBspTriangle& triangle, std::size_t edge)
{
    const auto& c = triangle.corners;
    const std::size_t i = edge, j = (edge + 1) % 3, k = (edge + 2) % 3;
    const auto m = midpoint(c[i], c[j]);
    return {RavenBspTriangle{{c[i], m, c[k]}, triangle.surface}, RavenBspTriangle{{m, c[j], c[k]}, triangle.surface}};
}

void appendHalf(const RavenBspTriangle& half, std::size_t originalEdge, const std::set<EdgeKey>& marked,
                std::vector<RavenBspTriangle>& out)
{
    if ( !marked.contains(edgeKey(half, originalEdge)) )
    {
        out.push_back(half);
        return;
    }
    auto [first, second] = bisect(half, originalEdge);
    out.push_back(std::move(first));
    out.push_back(std::move(second));
}

struct Work
{
    RavenBspTriangle triangle;
    std::size_t parent{};
    double error{};
    bool fresh{true};
};

} // namespace

void validateJoJaLightmapRefinement(const JoJaLightmapRefinement& settings)
{
    if ( !std::isfinite(settings.tolerance) || !(settings.tolerance > 0 && settings.tolerance < 1) )
    {
        throw std::invalid_argument("lightmapRefinement tolerance must be greater than 0 and less than 1");
    }
    if ( !std::isfinite(settings.minimumEdge) || settings.minimumEdge < 1 ||
         settings.minimumEdge > kJoJaLightmapRefinementMaximumMinimumEdge )
    {
        throw std::invalid_argument("lightmapRefinement minimumEdge must be 1 to 4096 source units");
    }
}

double joJaLightmapInterpolationError(const RavenBsp& bsp, const RavenBspTriangle& triangle)
{
    const auto atlas = bsp.surfaces.at(triangle.surface).lightmapIndex;
    if ( atlas < 0 )
    {
        return 0;
    }
    const auto& corners = triangle.corners;
    std::array<double, 3> light{};
    for ( std::size_t i = 0; i < corners.size(); ++i )
    {
        light[i] = ravenLightmapLuminance(bsp, atlas, corners[i].lightmapU, corners[i].lightmapV);
    }
    double worst = 0;
    for ( int a = 0; a <= kRavenLightmapGridSteps; ++a )
    {
        for ( int b = 0; b <= kRavenLightmapGridSteps - a; ++b )
        {
            constexpr double grid = kRavenLightmapGridSteps;
            const std::array<double, 3> weights{a / grid, b / grid, (kRavenLightmapGridSteps - a - b) / grid};
            double u = 0, v = 0, interpolated = 0;
            for ( std::size_t i = 0; i < corners.size(); ++i )
            {
                u += weights[i] * corners[i].lightmapU;
                v += weights[i] * corners[i].lightmapV;
                interpolated += weights[i] * light[i];
            }
            worst = std::max(worst, std::abs(ravenLightmapLuminance(bsp, atlas, u, v) - interpolated));
        }
    }
    return worst;
}

JoJaLightmapRefinementResult refineJoJaLightmapTriangles(const RavenBsp& bsp,
                                                         std::span<const RavenBspTriangle> triangles,
                                                         const JoJaLightmapRefinement& settings,
                                                         std::size_t maxTriangles)
{
    validateJoJaLightmapRefinement(settings);
    if ( triangles.size() > maxTriangles )
    {
        throw std::runtime_error("lightmapRefinement input already exceeds maxTriangles");
    }
    JoJaLightmapRefinementResult result;
    std::vector<Work> work;
    work.reserve(triangles.size());
    for ( std::size_t i = 0; i < triangles.size(); ++i )
    {
        work.push_back({triangles[i], i});
    }
    const double minimumSquared = settings.minimumEdge * settings.minimumEdge;
    std::set<std::size_t> splitParents;
    for ( bool initialPass = true;; initialPass = false )
    {
        std::set<EdgeKey> marked;
        for ( auto& item : work )
        {
            if ( !item.fresh )
            {
                continue;
            }
            item.fresh = false;
            item.error = joJaLightmapInterpolationError(bsp, item.triangle);
            if ( initialPass )
            {
                result.largestErrorBefore = std::max(result.largestErrorBefore, item.error);
            }
            const auto edge = longestEdge(item.triangle);
            if ( item.error > settings.tolerance && edgeLengthSquared(item.triangle, edge) >= minimumSquared )
            {
                marked.insert(edgeKey(item.triangle, edge));
            }
        }
        if ( marked.empty() )
        {
            break;
        }
        if ( ++result.passes > kMaximumPasses )
        {
            throw std::runtime_error("lightmapRefinement did not converge; raise the tolerance or minimumEdge");
        }

        // Closure: a triangle with any marked edge must also split its longest edge.
        std::map<EdgeKey, std::vector<std::size_t>> owners;
        std::vector<EdgeKey> longest(work.size());
        for ( std::size_t t = 0; t < work.size(); ++t )
        {
            for ( std::size_t edge = 0; edge < 3; ++edge )
            {
                owners[edgeKey(work[t].triangle, edge)].push_back(t);
            }
            longest[t] = edgeKey(work[t].triangle, longestEdge(work[t].triangle));
        }
        std::vector<EdgeKey> pending(marked.begin(), marked.end());
        while ( !pending.empty() )
        {
            const auto key = pending.back();
            pending.pop_back();
            for ( const auto t : owners.at(key) )
            {
                if ( marked.insert(longest[t]).second )
                {
                    pending.push_back(longest[t]);
                }
            }
        }

        std::vector<Work> next;
        next.reserve(work.size() + 2 * marked.size());
        std::vector<RavenBspTriangle> children;
        for ( std::size_t t = 0; t < work.size(); ++t )
        {
            auto& item = work[t];
            const auto edge = longestEdge(item.triangle);
            if ( !marked.contains(longest[t]) )
            {
                next.push_back(std::move(item));
                continue;
            }
            children.clear();
            auto [lower, upper] = bisect(item.triangle, edge);
            // The first half keeps the original edge from the far corner back to the split edge's start,
            // the second half the original edge from the split edge's end to the far corner.
            appendHalf(lower, 2, marked, children);
            appendHalf(upper, 1, marked, children);
            splitParents.insert(item.parent);
            for ( auto& child : children )
            {
                next.push_back({std::move(child), item.parent});
            }
        }
        if ( next.size() > maxTriangles )
        {
            throw std::runtime_error(
                "lightmapRefinement exceeds maxTriangles; raise maxTriangles, the tolerance or minimumEdge");
        }
        work = std::move(next);
    }
    result.splitSourceTriangles = splitParents.size();
    result.triangles.reserve(work.size());
    result.parents.reserve(work.size());
    for ( auto& item : work )
    {
        result.largestErrorAfter = std::max(result.largestErrorAfter, item.error);
        result.triangles.push_back(std::move(item.triangle));
        result.parents.push_back(item.parent);
    }
    return result;
}

} // namespace sed::assets
