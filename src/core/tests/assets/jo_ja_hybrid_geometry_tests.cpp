#include "sed/assets/jo_ja_hybrid_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace sed;
using namespace sed::assets;

constexpr double kScale = 1.0 / 16.0;
constexpr double kEpsilon = 1e-6;

void check(bool good, const char* why)
{
    if ( !good )
    {
        throw std::runtime_error(why);
    }
}

void near(double actual, double expected, const char* why)
{
    if ( !std::isfinite(actual) || std::abs(actual - expected) > kEpsilon )
    {
        throw std::runtime_error(std::string(why) + " (got " + std::to_string(actual) + ", expected " +
                                 std::to_string(expected) + ")");
    }
}

template <class F> void rejects(F&& operation, const char* why)
{
    try
    {
        operation();
    }
    catch ( const std::runtime_error& )
    {
        return;
    }
    throw std::runtime_error(why);
}

// Six ancestor planes define a finite room independent of the intentionally
// larger model bounds. Positive cells get distinct visibility cluster numbers.
struct Rooms
{
    RavenBsp bsp;
    std::vector<JoJaHybridTriangle> triangles;
    int clusterCount{};

    Rooms()
    {
        bsp.models.push_back({{-24, -16, -8}, {24, 16, 16}, 0, 0, 0, 0});
        bsp.shaders.push_back({"textures/test/floor", 0, 1});
        bsp.shaders.push_back({"textures/test/detail", 0, 1});
        bsp.entities.push_back({{{"classname", "worldspawn"}}});
        bsp.entities.push_back({{{"classname", "info_player_start"}, {"origin", "-8 -4 4"}}});
        leaf(false);
        const std::array<RavenBspPlane, 6> envelope{
            {{{-1, 0, 0}, 16}, {{1, 0, 0}, 16}, {{0, -1, 0}, 8}, {{0, 1, 0}, 8}, {{0, 0, -1}, 0}, {{0, 0, 1}, 8}}};
        for ( const auto& plane : envelope )
        {
            const auto i = node(plane.normal, plane.distance);
            bsp.nodes[i].children = {leaf(false), i + 1};
        }
    }

    int node(TVector normal, double distance)
    {
        const auto index = static_cast<int>(bsp.nodes.size());
        bsp.planes.push_back({normal, distance});
        bsp.nodes.push_back({index, {}, {-24, -16, -8}, {24, 16, 16}});
        return index;
    }

    int leaf(bool visible)
    {
        const int index = static_cast<int>(bsp.leaves.size());
        bsp.leaves.push_back(
            {visible ? clusterCount++ : -1, visible ? 0 : -1, {-24, -16, -8}, {24, 16, 16}, 0, 0, 0, 0});
        return -1 - index;
    }

    void fill(int subtree)
    {
        bsp.nodes[5].children[1] = subtree;
    }

    void addSolidBox(TVector low, TVector high)
    {
        const auto first = static_cast<int>(bsp.brushSides.size());
        const std::array<RavenBspPlane, 6> sides{{{{-1, 0, 0}, -low.x},
                                                  {{1, 0, 0}, high.x},
                                                  {{0, -1, 0}, -low.y},
                                                  {{0, 1, 0}, high.y},
                                                  {{0, 0, -1}, -low.z},
                                                  {{0, 0, 1}, high.z}}};
        for ( const auto& side : sides )
        {
            bsp.brushSides.push_back({static_cast<int>(bsp.planes.size()), 0, -1});
            bsp.planes.push_back(side);
        }
        bsp.brushes.push_back({first, 6, 0});
        ++bsp.models[0].brushCount;
        ++bsp.brushCount;
    }

    void addTriangle(TVector a, TVector b, TVector c, int material)
    {
        const auto id = bsp.surfaces.size();
        JoJaHybridTriangle source;
        source.material = static_cast<std::size_t>(material);
        source.triangle.surface = id;
        const auto normal = b.Sub(a).Cross(c.Sub(a)).Normalized();
        const auto firstVertex = static_cast<int>(bsp.vertices.size());
        const auto firstIndex = static_cast<int>(bsp.indices.size());
        int corner = 0;
        for ( auto p : {a, b, c} )
        {
            const auto u = (p.x + 8) / 16, v = (p.y + 4) / 8;
            RavenBspVertex vert{p,
                                normal,
                                u,
                                v,
                                u / 2,
                                v / 4,
                                {static_cast<float>(0.25 + 0.5 * u), static_cast<float>(v / 2), 0.5f, 1}};
            bsp.vertices.push_back(vert);
            source.triangle.corners[corner] = vert;
            bsp.indices.push_back(corner++);
        }
        bsp.surfaces.push_back({material, -1, 1, firstVertex, 3, firstIndex, 3, -1, 0, 0});
        ++bsp.models[0].surfaceCount;
        triangles.push_back(source);
    }

    JoJaHybridGeometry build(std::size_t maxSectors = 64, std::size_t maxFragments = 500000)
    {
        // Conservative references may span several leaves. Ownership must come
        // from the partition geometry, never the first draw-surface reference.
        bsp.leafSurfaces.clear();
        for ( auto& leaf : bsp.leaves )
        {
            if ( leaf.cluster < 0 )
            {
                continue;
            }
            leaf.firstLeafSurface = static_cast<int>(bsp.leafSurfaces.size());
            leaf.leafSurfaceCount = static_cast<int>(bsp.surfaces.size());
            for ( std::size_t s = 0; s < bsp.surfaces.size(); ++s )
            {
                bsp.leafSurfaces.push_back(static_cast<int>(s));
            }
        }
        return buildJoJaHybridGeometry(bsp, triangles, {kScale, maxSectors, maxFragments});
    }
};

double triangleArea(const TVector& a, const TVector& b, const TVector& c)
{
    return b.Sub(a).Cross(c.Sub(a)).Length() / 2;
}

double faceArea(const JoJaHybridFace& face)
{
    check(face.corners.size() >= 3, "piece of shell has fewer than three corners");
    double result = 0;
    for ( std::size_t i = 2; i < face.corners.size(); ++i )
    {
        result += triangleArea(face.corners.front().position, face.corners[i - 1].position, face.corners[i].position);
    }
    check(result > 0, "zero area shell piece");
    return result;
}

void checkShellEdges(const JoJaHybridCell& cell)
{
    std::vector<TVector> vertices;
    for ( const auto& face : cell.faces )
    {
        for ( const auto& corner : face.corners )
        {
            if ( std::none_of(vertices.begin(), vertices.end(),
                              [&](const auto& v)
                              {
                                  return v.Distance(corner.position) <= kEpsilon;
                              }) )
            {
                vertices.push_back(corner.position);
            }
        }
    }
    // Split at collinear vertices from neighboring faces before matching. A
    // triangulated floor and an adjoining uncut wall need not list equal edges.
    std::map<std::pair<std::size_t, std::size_t>, int> edges;
    for ( const auto& face : cell.faces )
    {
        for ( std::size_t i = 0; i < face.corners.size(); ++i )
        {
            const auto& a = face.corners[i].position;
            const auto& b = face.corners[(i + 1) % face.corners.size()].position;
            const auto delta = b.Sub(a);
            const auto len = delta.Length();
            check(len > kEpsilon, "zero-length shell edge");
            const auto unit = delta.Scaled(1 / len);
            std::vector<std::pair<double, std::size_t>> onEdge;
            for ( std::size_t j = 0; j < vertices.size(); ++j )
            {
                const auto offset = vertices[j].Sub(a);
                const auto along = offset.Dot(unit);
                if ( along >= -kEpsilon && along <= len + kEpsilon && offset.Cross(unit).Length() <= kEpsilon )
                {
                    onEdge.emplace_back(along, j);
                }
            }
            std::sort(onEdge.begin(), onEdge.end());
            check(onEdge.size() >= 2, "missing shell edge endpoints");
            for ( std::size_t j = 1; j < onEdge.size(); ++j )
            {
                ++edges[{onEdge[j - 1].second, onEdge[j].second}];
            }
        }
    }
    for ( const auto& [edge, count] : edges )
    {
        const auto reverse = edges.find({edge.second, edge.first});
        check(count == 1 && reverse != edges.end() && reverse->second == 1, "unpaired or overlapping shell edge");
    }
}

double checkShells(const JoJaHybridGeometry& output)
{
    double portalArea = 0;
    std::size_t portals = 0;
    for ( std::size_t s = 0; s < output.cells.size(); ++s )
    {
        const auto& cell = output.cells[s];
        checkShellEdges(cell);
        TVector balance{};
        for ( std::size_t f = 0; f < cell.faces.size(); ++f )
        {
            const auto& face = cell.faces[f];
            const auto area = faceArea(face);
            near(face.normal.Length(), 1, "unit shell normal");
            const auto p = face.corners.front().position;
            const auto interiorDelta = cell.interior.Sub(p).Dot(face.normal);
            check(std::abs(interiorDelta) > 1e-10, "interior lies on the boundary");
            for ( auto& v : face.corners )
            {
                near(v.position.Sub(p).Dot(face.normal), 0, "nonplanar shell piece");
                check(joJaHybridContains(cell, v.position, kEpsilon), "shell corner outside owner");
            }
            balance = balance.Add(face.normal.Scaled(interiorDelta > 0 ? -area : area));
            if ( face.neighbor < 0 )
            {
                continue;
            }
            check(!face.material && face.mirror >= 0, "portal must have mirror and clear material");
            const auto& other = output.cells.at(static_cast<std::size_t>(face.neighbor))
                                    .faces.at(static_cast<std::size_t>(face.mirror));
            check(other.neighbor == static_cast<int>(s) && other.mirror == static_cast<int>(f),
                  "reciprocal portal indices");
            check(face.corners.size() == other.corners.size(), "portal corner count mismatch");
            near(face.normal.Dot(other.normal), -1, "opposite portal normals");
            const auto start = std::find_if(other.corners.begin(), other.corners.end(),
                                            [&](const auto& v)
                                            {
                                                return v.position.Distance(p) < kEpsilon;
                                            });
            check(start != other.corners.end(), "mirror missing portal corner");
            const auto n = face.corners.size();
            const auto base = static_cast<std::size_t>(start - other.corners.begin());
            for ( std::size_t i = 0; i < n; ++i )
            {
                near(face.corners[i].position.Distance(other.corners[(base + n - i) % n].position), 0,
                     "portal has different contour or winding");
            }
            portalArea += area;
            ++portals;
        }
        near(balance.Length(), 0, "shell area vectors do not close");
    }
    check(portals == 2 * output.portalPairs, "portal counts disagree with independent traversal");
    return portalArea / 2;
}

double checkSourceCoverage(const JoJaHybridGeometry& g, std::size_t source, std::size_t material)
{
    double area = 0;
    for ( const auto& cell : g.cells )
    {
        for ( const auto& face : cell.faces )
        {
            if ( face.material && face.sourceSurface == source )
            {
                check(*face.material == material && face.neighbor < 0, "native material or ownership mismatch");
                area += faceArea(face);
            }
        }
    }
    for ( const auto& chunk : g.residual )
    {
        if ( chunk.triangle.surface != source )
        {
            continue;
        }
        const auto& c = chunk.triangle.corners;
        check(chunk.material == material, "residual lost material");
        for ( const auto& v : c )
        {
            check(joJaHybridContains(g.cells.at(chunk.sector), v.position, kEpsilon), "residual exceeds owner");
        }
        area += triangleArea(c[0].position, c[1].position, c[2].position);
    }
    return area;
}

void closedRoom()
{
    Rooms r;
    r.fill(r.leaf(true));
    r.addTriangle({-8, -4, 0}, {8, -4, 0}, {-8, 4, 0}, 0);
    r.addTriangle({-8, -4, 4}, {8, -4, 4}, {-8, 4, 4}, 1);
    r.addTriangle({12, 4, 0}, {8, 2, 0}, {4, 4, 0}, 1); // Faces away from the room.
    const auto g = r.build();
    check(g.cells.size() == 1 && g.portalPairs == 0 && g.nativeFaces > 0, "closed room with real native floor");
    near(checkShells(g), 0, "closed room must have no opening");
    double shell = 0;
    for ( const auto& face : g.cells[0].faces )
    {
        shell += faceArea(face);
        if ( face.material )
        {
            check(face.sourceSurface == 0 && face.normal.z > 0.999, "only inward-facing floor is promoted");
        }
    }
    near(shell, 2 * (32 * 16 + 16 * 8 + 32 * 8) * kScale * kScale, "finite room shell area");
    near(checkSourceCoverage(g, 0, 0), 64 * kScale * kScale, "promoted floor coverage");
    near(checkSourceCoverage(g, 1, 1), 64 * kScale * kScale, "interior detail coverage");
    near(checkSourceCoverage(g, 2, 1), 8 * kScale * kScale, "reverse-facing boundary detail coverage");
    check(g.residual.size() == 2 && std::none_of(g.residual.begin(), g.residual.end(),
                                                 [](const auto& item)
                                                 {
                                                     return item.triangle.surface == 0;
                                                 }),
          "interior and reverse-facing detail remain sector-local");
    check(!joJaHybridContains(g.cells[0], {-17 * kScale, 0, 0}), "room may not extend to padded world bounds");

    auto missing = r.bsp;
    missing.nodes.clear();
    rejects(
        [&]
        {
            buildJoJaHybridGeometry(missing, r.triangles);
        },
        "missing source tree accepted");
    rejects(
        [&]
        {
            r.build(0);
        },
        "zero sector budget accepted");
    rejects(
        [&]
        {
            r.build(97);
        },
        "above-contract sector budget accepted");
}

void negativeLeafDetail()
{
    Rooms r;
    r.fill(r.leaf(true));
    r.addTriangle({-8, -4, 0}, {8, -4, 0}, {-8, 4, 0}, 0);
    // Entirely outside the +X envelope in a negative visibility leaf. Draw
    // surfaces in a finished Raven BSP need not lie within its positive leaves.
    r.addTriangle({17, -1, 2}, {17, 1, 2}, {17, -1, 4}, 1);
    const auto g = r.build();
    check(g.cells.size() == 1 && g.portalPairs == 0, "negative-leaf detail creates no phantom portal or sector");
    const auto& cell = g.cells.front();
    near(checkShells(g), 0, "closed room with negative-leaf detail");
    near(checkSourceCoverage(g, 0, 0), 64 * kScale * kScale, "unaffected floor remains covered");
    check(g.nativeFaces > 0 && std::any_of(cell.faces.begin(), cell.faces.end(),
                                           [](const auto& face)
                                           {
                                               return face.material && face.sourceSurface == 0 && face.normal.z > 0.999;
                                           }),
          "unaffected floor remains native");
    near(checkSourceCoverage(g, 1, 1), 2 * kScale * kScale, "negative-leaf detail emitted exactly once");
    check(std::any_of(g.residual.begin(), g.residual.end(),
                      [](const auto& residual)
                      {
                          return residual.triangle.surface == 1 && residual.sector == 0;
                      }),
          "negative-leaf detail stays in a containing residual owner");
    check(joJaHybridContains(cell, {17 * kScale, 0, 3 * kScale}), "relax the offending +X constraint");
    for ( const TVector p :
          {TVector{-17, 0, 4}, TVector{0, -9, 4}, TVector{0, 9, 4}, TVector{0, 0, -1}, TVector{0, 0, 9}} )
    {
        check(!joJaHybridContains(cell, p.Scaled(kScale)), "preserve each unrelated unilateral room boundary");
    }
}

void partialDoorway()
{
    Rooms r;
    const auto split = r.node({1, 0, 0}, 0);
    const auto wallEnd = r.node({1, 0, 0}, 2);
    const auto doorway = r.node({0, 1, 0}, 0);
    r.fill(split);
    r.bsp.nodes[split].children = {wallEnd, r.leaf(true)};
    r.bsp.nodes[wallEnd].children = {r.leaf(true), doorway};
    r.bsp.nodes[doorway].children = {r.leaf(false), r.leaf(true)};
    r.addTriangle({-8, -4, 4}, {8, -4, 4}, {-8, 4, 4}, 1);
    const auto g = r.build(2);
    check(g.cells.size() == 2 && g.portalPairs > 0, "expected two portalized coarse sectors");
    near(checkShells(g), 8 * 8 * kScale * kScale, "door must cover only the source aperture");
    for ( const auto& cell : g.cells )
    {
        for ( const auto& face : cell.faces )
        {
            if ( face.neighbor >= 0 )
            {
                for ( const auto& v : face.corners )
                {
                    near(v.position.x, 0, "coarse interface X");
                    check(v.position.y <= kEpsilon, "positive Y is solid at the interface");
                }
            }
        }
    }
    near(checkSourceCoverage(g, 0, 1), 64 * kScale * kScale, "crossing triangle conservation");
    check(g.splitSourceTriangles == 1 && g.residual.size() >= 2, "crossing detail must be partitioned");
    check(g.residualPolygons.size() == 2, "one clipped model polygon per side of the doorway");
    std::size_t quadCount = 0, fanTriangles = 0;
    double polygonArea = 0;
    for ( const auto& polygon : g.residualPolygons )
    {
        check(polygon.sourceSurface == 0 && polygon.material == 1 && polygon.corners.size() >= 3,
              "retained model polygon preserves source surface and material");
        quadCount += polygon.corners.size() == 4;
        fanTriangles += polygon.corners.size() - 2;
        for ( std::size_t i = 2; i < polygon.corners.size(); ++i )
        {
            polygonArea +=
                triangleArea(polygon.corners[0].position, polygon.corners[i - 1].position, polygon.corners[i].position);
        }
        for ( const auto& v : polygon.corners )
        {
            check(joJaHybridContains(g.cells.at(polygon.sector), v.position, kEpsilon),
                  "n-gon stays in its own sector");
            const double u = (v.position.x / kScale + 8) / 16, valueV = (v.position.y / kScale + 4) / 8;
            near(v.u, u, "retained model polygon U");
            near(v.v, valueV, "retained model polygon V");
            near(v.color.r, 0.25 + u / 2, "retained model polygon red");
            near(v.color.g, valueV / 2, "retained model polygon green");
            near(v.color.b, 0.5, "retained model polygon blue");
            near(v.color.a, 1, "retained model polygon alpha");
        }
    }
    near(polygonArea, 64 * kScale * kScale, "retained model polygon coverage");
    check(quadCount == 1 && fanTriangles == g.residual.size(),
          "the clipped quad stays one model face and its fan count agrees with triangle coverage");
    bool seamAtBottom = false, seamAtTop = false;
    for ( const auto& piece : g.residual )
    {
        for ( const auto& v : piece.triangle.corners )
        {
            if ( std::abs(v.position.x) > kEpsilon )
            {
                continue;
            }
            const auto expectedV = (v.position.y / kScale + 4) / 8;
            near(v.u, 0.5, "split vertex U");
            near(v.v, expectedV, "split vertex V");
            near(v.lightmapU, 0.25, "split lightmap U");
            near(v.lightmapV, expectedV / 4, "split lightmap V");
            near(v.color.r, 0.5, "interpolated source light R");
            near(v.color.g, expectedV / 2, "interpolated source light G");
            near(v.normal.z, 1, "split vertex normal");
            seamAtBottom |= std::abs(v.position.y + 4 * kScale) < kEpsilon;
            seamAtTop |= std::abs(v.position.y) < kEpsilon;
        }
    }
    check(seamAtBottom && seamAtTop, "missing crossing triangle seam endpoints");
    const auto coarse = r.build(1);
    check(coarse.cells.size() == 1 && coarse.portalPairs == 0, "one-sector budget");
    near(checkSourceCoverage(coarse, 0, 1), 64 * kScale * kScale, "one-sector geometry coverage");
    rejects(
        [&]
        {
            r.build(2, 1);
        },
        "insufficient fragment budget silently dropped geometry");
}

void solidDivider()
{
    Rooms r;
    const auto hi = r.node({1, 0, 0}, 1);
    const auto lo = r.node({1, 0, 0}, -1);
    const auto leftSplit = r.node({1, 0, 0}, -8);
    r.fill(hi);
    r.bsp.nodes[hi].children = {r.leaf(true), lo};
    r.bsp.nodes[lo].children = {r.leaf(false), leftSplit};
    r.bsp.nodes[leftSplit].children = {r.leaf(true), r.leaf(true)};
    r.addTriangle({-14, -3, 4}, {-4, -3, 4}, {-14, 3, 4}, 1);
    r.addTriangle({4, -3, 4}, {12, -3, 4}, {4, 3, 4}, 1);
    const auto g = r.build(3);
    check(g.cells.size() == 3, "keep all three visible leaves, including disconnected room");
    near(checkShells(g), 16 * 8 * kScale * kScale, "exactly one transparent interface in left component");
    std::size_t rightCount = 0;
    for ( const auto& cell : g.cells )
    {
        check(!joJaHybridContains(cell, {0, 0, 4 * kScale}), "opaque strip became playable space");
        if ( joJaHybridContains(cell, {6 * kScale, 0, 4 * kScale}) )
        {
            ++rightCount;
            check(std::none_of(cell.faces.begin(), cell.faces.end(),
                               [](const auto& face)
                               {
                                   return face.neighbor >= 0;
                               }),
                  "portal jumps across a solid wall");
        }
    }
    check(rightCount == 1, "detached room must have one owner");
    near(checkSourceCoverage(g, 0, 1), 30 * kScale * kScale, "left-room detail");
    near(checkSourceCoverage(g, 1, 1), 24 * kScale * kScale, "disconnected-room detail");
}

void obliqueSplit()
{
    Rooms r;
    const auto axis = std::sqrt(0.5);
    const auto split = r.node({axis, axis, 0}, 0);
    r.fill(split);
    r.bsp.nodes[split].children = {r.leaf(true), r.leaf(true)};
    r.addTriangle({-8, -4, 4}, {8, -4, 4}, {-8, 4, 4}, 1);
    const auto g = r.build(2);
    check(g.cells.size() == 2, "oblique sectors");
    near(checkShells(g), 16 * std::sqrt(2.0) * 8 * kScale * kScale, "diagonal interface area");
    near(checkSourceCoverage(g, 0, 1), 64 * kScale * kScale, "oblique geometry conservation");
    for ( const auto& cell : g.cells )
    {
        for ( const auto& face : cell.faces )
        {
            if ( face.neighbor >= 0 )
            {
                for ( const auto& v : face.corners )
                {
                    near(v.position.x + v.position.y, 0, "adjoin follows source plane");
                }
            }
        }
    }
}
} // namespace

namespace
{
void supportedOuterFloorKeepsRooms()
{
    Rooms r;
    const auto split = r.node({1, 0, 0}, 0);
    r.fill(split);
    r.bsp.nodes[split].children = {r.leaf(true), r.leaf(true)};
    r.bsp.shaders[0].contents = 0x8001;
    // The first four envelope planes bound the floor slab in X and Y.
    for ( int i = 0; i < 4; ++i )
    {
        r.bsp.brushSides.push_back({i, 0, -1});
    }
    for ( const auto& p : std::array<RavenBspPlane, 2>{{{{0, 0, -1}, 2}, {{0, 0, 1}, 0}}} )
    {
        r.bsp.brushSides.push_back({static_cast<int>(r.bsp.planes.size()), 0, -1});
        r.bsp.planes.push_back(p);
    }
    r.bsp.brushes.push_back({0, 6, 0});
    r.bsp.brushCount = 1;
    r.bsp.models[0].brushCount = 1;
    r.addTriangle({-8, -4, 0}, {8, -4, 0}, {-8, 4, 0}, 0);
    const auto result = r.build(2);
    check(result.cells.size() == 2 && result.portalPairs > 0,
          "a certified floor already on the shell must preserve both BSP rooms and their portal");
    near(checkShells(result), 16 * 8 * kScale * kScale, "retain the original BSP room aperture");
    near(checkSourceCoverage(result, 0, 0), 64 * kScale * kScale, "retain the complete supported floor");
}

void disjointFloorMasks()
{
    Rooms r;
    r.fill(r.leaf(true));
    // The right triangles touch at (8,0) only. Their infinite edge lines
    // cross the remote left part of the floor, but their finite masks do not.
    r.addTriangle({-16, -8, 0}, {-8, 0, 0}, {-16, 8, 0}, 0);
    r.addTriangle({8, 0, 0}, {16, 0, 0}, {8, 4, 0}, 0);
    r.addTriangle({8, -4, 0}, {16, -4, 0}, {8, 0, 0}, 0);
    const auto result = r.build(1);
    check(result.cells.size() == 1 && result.portalPairs == 0 && result.residual.empty(),
          "disjoint native floor masks stay in one closed room");
    near(checkShells(result), 0, "disjoint masks introduce no portal or unmatched edge");
    std::size_t visible = 0, hidden = 0;
    double covered = 0, remainder = 0;
    for ( const auto& face : result.cells.front().faces )
    {
        if ( face.normal.z > 0.999 )
        {
            if ( face.material )
            {
                ++visible;
                covered += faceArea(face);
            }
            else
            {
                ++hidden;
                remainder += faceArea(face);
            }
        }
    }
    near(covered, 96 * kScale * kScale, "three disjoint source masks retain their complete textured area");
    near(remainder, (512 - 96) * kScale * kScale, "the remaining closed floor covers exactly the complement");
    check(visible == 3 && hidden <= 6, "disjoint and point-touching masks must not slice remote floor pieces");
}

void whollyOpenBspSubtrees()
{
    Rooms r;
    const auto rooms = r.node({1, 0, 0}, 0);
    r.fill(rooms);
    // These source leaves change visibility cluster IDs across the aperture,
    // but none contains a solid/negative leaf. Their seams carry no wall.
    const auto openSplit = [&](TVector normal, double distance)
    {
        const auto node = r.node(normal, distance);
        r.bsp.nodes[node].children = {r.leaf(true), r.leaf(true)};
        return node;
    };
    const auto front = r.node({0, 1, 0}, 1);
    const auto frontHigh = openSplit({0, 1, 1}, 2);
    const auto frontLow = openSplit({0, 0, 1}, 1);
    r.bsp.nodes[front].children = {frontHigh, frontLow};
    const auto back = r.node({0, 1, 0}, 0);
    const auto backHigh = openSplit({0, 1, 1}, 2);
    const auto backLow = openSplit({0, 1, 0}, 2);
    r.bsp.nodes[back].children = {backHigh, backLow};
    r.bsp.nodes[rooms].children = {front, back};
    r.addTriangle({-8, -4, 4}, {8, -4, 4}, {-8, 4, 4}, 1);
    const auto result = r.build(2);
    check(result.cells.size() == 2 && result.portalPairs == 1,
          "fully open visibility subtrees must retain one rectangular room aperture");
    near(checkShells(result), 16 * 8 * kScale * kScale, "the source cluster seams leave the full aperture open");
    near(checkSourceCoverage(result, 0, 1), 64 * kScale * kScale, "retain crossing model detail in both rooms");
}

void interiorSolidFloor()
{
    Rooms r;
    r.fill(r.leaf(true));
    r.bsp.shaders[0].contents = 0x8001;
    const auto firstSide = static_cast<std::int32_t>(r.bsp.brushSides.size());
    const std::array<RavenBspPlane, 6> box{
        {{{-1, 0, 0}, 8}, {{1, 0, 0}, 8}, {{0, -1, 0}, 4}, {{0, 1, 0}, 4}, {{0, 0, -1}, -2}, {{0, 0, 1}, 4}}};
    for ( const auto& plane : box )
    {
        const auto id = static_cast<std::int32_t>(r.bsp.planes.size());
        r.bsp.planes.push_back(plane);
        r.bsp.brushSides.push_back({id, 0, -1});
    }
    r.bsp.brushes.push_back({firstSide, 6, 0});
    r.bsp.models[0].brushCount = 1;
    r.bsp.brushCount = 1;
    r.addTriangle({-8, -4, 4}, {8, -4, 4}, {-8, 4, 4}, 0);
    r.addTriangle({8, -4, 4}, {8, 4, 4}, {-8, 4, 4}, 0);
    r.addTriangle({-8, -4, 0}, {8, -4, 0}, {-8, 4, 0}, 1);
    const auto g = r.build(2);
    check(g.cells.size() == 2, "verified interior solid floor must provide a native partition");
    double top = 0;
    for ( const auto& cell : g.cells )
    {
        for ( const auto& face : cell.faces )
        {
            if ( face.material && face.sourceSurface < 2 )
            {
                near(face.normal.z, 1, "platform retains its front orientation");
                top += faceArea(face);
            }
            if ( face.neighbor >= 0 )
            {
                const auto center = std::accumulate(face.corners.begin(), face.corners.end(), TVector{},
                                                    [](const auto& p, const auto& c)
                                                    {
                                                        return p + c.position;
                                                    }) *
                                    (1.0 / face.corners.size());
                check(std::abs(center.x) >= 8 * kScale - kEpsilon || std::abs(center.y) >= 4 * kScale - kEpsilon,
                      "portal may not look through the verified solid platform footprint");
            }
        }
    }
    near(top, 128 * kScale * kScale, "whole interior platform becomes native geometry");
    near(checkShells(g), (32 * 16 - 128) * kScale * kScale, "only open area beside the platform is adjoined");
    near(checkSourceCoverage(g, 0, 0) + checkSourceCoverage(g, 1, 0), top, "both floor triangles are retained");
}

void opaqueBrushCrossesAperture()
{
    Rooms r;
    r.fill(r.leaf(true));
    r.bsp.shaders[0].contents = 0x8001;
    r.addSolidBox({-8, -4, 2}, {8, 4, 4});
    r.addSolidBox({-14, -3, 0}, {-12, 3, 8}); // No draw triangle lies on this post at z=4.
    r.addTriangle({-8, -4, 4}, {8, -4, 4}, {-8, 4, 4}, 0);
    r.addTriangle({8, -4, 4}, {8, 4, 4}, {-8, 4, 4}, 0);
    const auto result = r.build(2);
    check(result.cells.size() == 2 && result.portalPairs > 0, "keep the aperture beside the opaque geometry");
    near(checkShells(result), (32 * 16 - 16 * 8 - 2 * 6) * kScale * kScale,
         "solid post crossing a shared plane must occlude its full brush section even without render triangles");
    near(checkSourceCoverage(result, 0, 0) + checkSourceCoverage(result, 1, 0), 16 * 8 * kScale * kScale,
         "the opaque post does not change the platform artwork");
}

void partitionKeepsCrossingDetailIntact()
{
    Rooms r;
    r.fill(r.leaf(true));
    r.bsp.shaders[0].contents = 0x8001;
    r.addSolidBox({-14, -6, 3}, {-2, 6, 4});
    r.addSolidBox({-1, -3, 2}, {0, 3, 6});
    r.addTriangle({-14, -6, 4}, {-2, -6, 4}, {-14, 6, 4}, 0);
    r.addTriangle({-2, -6, 4}, {-2, 6, 4}, {-14, 6, 4}, 0);
    r.addTriangle({0, -3, 2}, {0, 3, 2}, {0, -3, 6}, 0);
    // The larger floor cuts every vertical detail face. The shorter divider
    // separates the two groups while keeping every source polygon intact.
    for ( double x : {-8.0, 8.0} )
    {
        for ( int row = 0; row < 12; ++row )
        {
            const double y = -6 + row;
            r.addTriangle({x, y, 1}, {x + 1, y, 1}, {x, y, 7}, 1);
        }
    }
    const auto g = r.build(2);
    check(g.cells.size() == 2 && g.splitSourceTriangles == 0,
          "a bounded partition must prefer the divider that keeps crossing detail intact");
    near(checkShells(g), (16 * 8 - 6 * 4) * kScale * kScale, "the chosen divider keeps its finite solid aperture");
    near(checkSourceCoverage(g, 2, 0), 12 * kScale * kScale, "the divider retains its authored triangle area");
    near(g.outputArea, g.sourceArea, "partition scoring preserves full source area");
}

void nonstructuralOverlayStaysDetail()
{
    for ( const auto contents : {0x8000u, 0x80008001u} )
    {
        Rooms r;
        r.fill(r.leaf(true));
        r.bsp.shaders[1].contents = contents;
        // The non-solid or translucent overlay is encountered before the actual
        // solid floor on the identical footprint. Both must retain their area.
        r.addTriangle({-8, -4, 0}, {8, -4, 0}, {-8, 4, 0}, 1);
        r.addTriangle({-8, -4, 0}, {8, -4, 0}, {-8, 4, 0}, 0);
        const auto result = r.build(1);
        const auto& faces = result.cells.front().faces;
        check(result.nativeFaces == 1 && std::any_of(faces.begin(), faces.end(),
                                                     [](const auto& face)
                                                     {
                                                         return face.material && *face.material == 0 &&
                                                                face.sourceSurface == 1;
                                                     }),
              "a translucent or non-solid overlay must not replace the actual floor's native material");
        check(result.residual.size() == 1 && result.residual.front().triangle.surface == 0,
              "the original overlay remains render detail");
        near(checkSourceCoverage(result, 0, 1), 64 * kScale * kScale, "preserve overlay area");
        near(checkSourceCoverage(result, 1, 0), 64 * kScale * kScale, "preserve underlying floor area");
    }
}
} // namespace

int main()
{
    const std::array<std::pair<const char*, void (*)()>, 12> cases{
        {{"closed room", closedRoom},
         {"negative-leaf detail", negativeLeafDetail},
         {"partial doorway", partialDoorway},
         {"solid divider", solidDivider},
         {"oblique split", obliqueSplit},
         {"interior solid floor", interiorSolidFloor},
         {"supported outer floor", supportedOuterFloorKeepsRooms},
         {"disjoint floor masks", disjointFloorMasks},
         {"wholly open source leaves", whollyOpenBspSubtrees},
         {"opaque brush crosses aperture", opaqueBrushCrossesAperture},
         {"cost-aware divider", partitionKeepsCrossingDetailIntact},
         {"nonstructural floor overlay", nonstructuralOverlayStaysDetail}}};
    int failures = 0;
    for ( const auto& [name, run] : cases )
    {
        try
        {
            run();
        }
        catch ( const std::exception& error )
        {
            std::cerr << name << ": " << error.what() << '\n';
            ++failures;
        }
    }
    return failures != 0 ? 1 : 0;
}
