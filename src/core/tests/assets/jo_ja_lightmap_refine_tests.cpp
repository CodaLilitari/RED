#include "raven_bsp_fixture.hpp"
#include "sed/assets/jo_ja_lightmap_refine.hpp"
#include "sed/assets/raven_lightmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace
{
using namespace sed;
using namespace sed::assets;

void require(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename F> void mustThrow(F&& action, const char* message)
{
    try
    {
        action();
    }
    catch ( const Exception& )
    {
        return;
    }
    throw std::runtime_error(message);
}

using Point = std::tuple<double, double, double>;

Point pointOf(const RavenBspVertex& vertex)
{
    return {vertex.position.x, vertex.position.y, vertex.position.z};
}

double cross2(const RavenBspTriangle& triangle)
{
    const auto& a = triangle.corners[0].position;
    const auto& b = triangle.corners[1].position;
    const auto& c = triangle.corners[2].position;
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double longestEdge(const RavenBspTriangle& triangle)
{
    double longest = 0;
    for ( std::size_t i = 0; i < 3; ++i )
    {
        const auto& a = triangle.corners[i].position;
        const auto& b = triangle.corners[(i + 1) % 3].position;
        longest = std::max(longest, std::hypot(b.x - a.x, b.y - a.y, b.z - a.z));
    }
    return longest;
}

struct Pool
{
    RavenBsp bsp;
    std::vector<RavenBspTriangle> triangles;
};

Pool pool()
{
    Pool result;
    result.bsp = parseRavenBsp(sed::tests::makeRavenLightPoolFixture().bytes());
    for ( std::size_t surface = 0; surface < 2; ++surface )
    {
        for ( auto& triangle : ravenBspSurfaceTriangles(result.bsp, surface, 4) )
        {
            result.triangles.push_back(triangle);
        }
    }
    require(result.triangles.size() == 2 && result.bsp.surfaces[0].lightmapIndex == 0 &&
                result.bsp.surfaces[1].lightmapIndex < 0,
            "Fixture holds one lightmapped and one vertex-lit triangle");
    return result;
}

void checkErrorMetric()
{
    const auto scene = pool();
    // The centroid's lightmap coordinate falls inside the white luxels while every corner is black.
    require(std::abs(joJaLightmapInterpolationError(scene.bsp, scene.triangles[0]) - 1.0) < 1e-6,
            "A bright pool between black corners measures the full interpolation error");
    require(joJaLightmapInterpolationError(scene.bsp, scene.triangles[1]) == 0,
            "A vertex-lit triangle has no lightmap interpolation error");
}

void checkValidation()
{
    const auto refuse = [](double tolerance, double minimumEdge)
    {
        mustThrow<std::invalid_argument>([&] { validateJoJaLightmapRefinement({tolerance, minimumEdge}); },
                                         "Out-of-range refinement settings are refused");
    };
    refuse(0, 16);
    refuse(1, 16);
    refuse(-0.1, 16);
    refuse(std::numeric_limits<double>::quiet_NaN(), 16);
    refuse(0.05, 0.5);
    refuse(0.05, 4097);
    refuse(0.05, std::numeric_limits<double>::infinity());
    validateJoJaLightmapRefinement({0.05, 1});
    validateJoJaLightmapRefinement({0.999, 4096});
}

// Every interior edge is shared by exactly two triangles with opposite directions, and no corner lies
// strictly inside another triangle's edge.
void requireConforming(const std::vector<RavenBspTriangle>& triangles)
{
    std::map<std::pair<Point, Point>, int> directed;
    std::vector<Point> corners;
    for ( const auto& triangle : triangles )
    {
        for ( std::size_t i = 0; i < 3; ++i )
        {
            const auto a = pointOf(triangle.corners[i]);
            const auto b = pointOf(triangle.corners[(i + 1) % 3]);
            require(++directed[{a, b}] == 1, "No directed edge repeats");
            corners.push_back(a);
        }
    }
    for ( const auto& [edge, count] : directed )
    {
        const auto& [a, b] = edge;
        const bool boundary = (std::get<0>(a) == 0 && std::get<0>(b) == 0) || (std::get<1>(a) == 0 && std::get<1>(b) == 0) ||
                              (std::get<0>(a) == 256 && std::get<0>(b) == 256) || (std::get<1>(a) == 256 && std::get<1>(b) == 256);
        require(boundary || directed.contains({b, a}), "Interior edges are shared with opposite winding");
        for ( const auto& p : corners )
        {
            const double ex = std::get<0>(b) - std::get<0>(a), ey = std::get<1>(b) - std::get<1>(a);
            const double px = std::get<0>(p) - std::get<0>(a), py = std::get<1>(p) - std::get<1>(a);
            const double along = (px * ex + py * ey) / (ex * ex + ey * ey);
            require(std::abs(px * ey - py * ex) > 1e-9 || along <= 1e-12 || along >= 1 - 1e-12,
                    "No corner lies inside another triangle's edge");
        }
    }
}

void checkRefinement()
{
    const auto scene = pool();
    const JoJaLightmapRefinement settings{0.05, 16};
    const auto refined = refineJoJaLightmapTriangles(scene.bsp, scene.triangles, settings, 100000);
    require(refined.triangles.size() > scene.triangles.size() && refined.parents.size() == refined.triangles.size(),
            "The bright pool splits the floor");
    require(std::is_sorted(refined.parents.begin(), refined.parents.end()) && refined.parents.back() == 1 &&
                refined.parents.front() == 0,
            "Children keep their parent's place in the input order");
    require(refined.splitSourceTriangles == 2 && refined.passes > 0,
            "The vertex-lit neighbour splits the shared long edge too");
    require(std::abs(refined.largestErrorBefore - 1.0) < 1e-6 && refined.largestErrorAfter < refined.largestErrorBefore,
            "Refinement reports and reduces the largest interpolation error");
    requireConforming(refined.triangles);

    double area = 0, largest = 0;
    float brightest = 0;
    for ( std::size_t t = 0; t < refined.triangles.size(); ++t )
    {
        const auto& triangle = refined.triangles[t];
        const auto& parent = scene.triangles[refined.parents[t]];
        require(triangle.surface == parent.surface, "Children keep the source surface");
        require(cross2(triangle) > 0, "Children keep the source winding");
        area += cross2(triangle) / 2;
        for ( const auto& corner : triangle.corners )
        {
            const auto& p = corner.position;
            require(p.z == 0 && corner.normal.z == 1 && corner.normal.x == 0 && corner.normal.y == 0,
                    "Corners stay on the source plane with its normal");
            require(std::abs(corner.u - p.x / 64) < 1e-12 && std::abs(corner.v - p.y / 64) < 1e-12,
                    "Texture coordinates are interpolated along split edges");
            require(std::abs(corner.lightmapU - (p.x / 16 + 0.5) / 128) < 1e-9 &&
                        std::abs(corner.lightmapV - (p.y / 16 + 0.5) / 128) < 1e-9,
                    "Lightmap coordinates are interpolated along split edges");
            require(std::abs(corner.color.r - 200.0f / 255) < 1e-6 && corner.color.a == 1.0f,
                    "Vertex colors are interpolated along split edges");
            if ( triangle.surface == 0 )
            {
                brightest = std::max(brightest, ravenLightmapLuminance(scene.bsp, 0, corner.lightmapU, corner.lightmapV));
            }
        }
        if ( triangle.surface == 0 )
        {
            const auto error = joJaLightmapInterpolationError(scene.bsp, triangle);
            largest = std::max(largest, error);
            require(error <= settings.tolerance || longestEdge(triangle) < settings.minimumEdge,
                    "Each remaining triangle fits the tolerance or is too small to split");
        }
        else
        {
            require(joJaLightmapInterpolationError(scene.bsp, triangle) == 0, "Vertex-lit children stay error free");
        }
    }
    require(std::abs(area - 2 * 256.0 * 256.0 / 2) < 1e-6, "Refinement covers exactly the source area");
    require(brightest > 0.9f, "Corners now sample the bright pool");
    require(std::abs(largest - refined.largestErrorAfter) < 1e-12, "The reported remaining error is measured");

    const auto again = refineJoJaLightmapTriangles(scene.bsp, scene.triangles, settings, 100000);
    require(again.triangles.size() == refined.triangles.size() && again.parents == refined.parents,
            "Refinement is deterministic");
    for ( std::size_t t = 0; t < again.triangles.size(); ++t )
    {
        for ( std::size_t i = 0; i < 3; ++i )
        {
            require(pointOf(again.triangles[t].corners[i]) == pointOf(refined.triangles[t].corners[i]),
                    "Repeated refinement places identical corners");
        }
    }

    const auto coarse = refineJoJaLightmapTriangles(scene.bsp, scene.triangles, {0.05, 64}, 100000);
    require(coarse.triangles.size() < refined.triangles.size() && coarse.splitSourceTriangles == 2,
            "A larger minimum edge stops refinement earlier");
}

void checkUnchangedAndBudget()
{
    const auto scene = pool();
    const auto untouched = refineJoJaLightmapTriangles(scene.bsp, scene.triangles, {0.05, 4096}, 100000);
    require(untouched.triangles.size() == 2 && untouched.splitSourceTriangles == 0 &&
                untouched.parents == std::vector<std::size_t>{0, 1},
            "Edges shorter than the minimum are not split");
    for ( std::size_t t = 0; t < 2; ++t )
    {
        for ( std::size_t i = 0; i < 3; ++i )
        {
            require(pointOf(untouched.triangles[t].corners[i]) == pointOf(scene.triangles[t].corners[i]),
                    "Unsplit triangles are copied unchanged");
        }
    }
    const std::vector<RavenBspTriangle> vertexLit{scene.triangles[1]};
    const auto plain = refineJoJaLightmapTriangles(scene.bsp, vertexLit, {0.001, 1}, 100000);
    require(plain.triangles.size() == 1 && plain.splitSourceTriangles == 0 && plain.largestErrorBefore == 0,
            "Vertex-lit surfaces alone never start a split");
    mustThrow<std::runtime_error>([&] { (void)refineJoJaLightmapTriangles(scene.bsp, scene.triangles, {0.05, 16}, 3); },
                                  "Refinement beyond the triangle budget is refused");
    mustThrow<std::invalid_argument>([&] { (void)refineJoJaLightmapTriangles(scene.bsp, scene.triangles, {0, 16}, 100000); },
                                     "Refinement validates its settings");
}

} // namespace

int main()
{
    int failures = 0;
    for ( auto test : {checkErrorMetric, checkValidation, checkRefinement, checkUnchangedAndBudget} )
    {
        try
        {
            test();
        }
        catch ( const std::exception& error )
        {
            std::cerr << error.what() << '\n';
            ++failures;
        }
    }
    return failures ? 1 : 0;
}
