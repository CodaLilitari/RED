#include "raven_bsp_fixture.hpp"
#include "sed/assets/raven_bsp.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
using namespace sed::assets;
using namespace sed::tests::raven_fixture;
void require(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

template <class F>
void mustFail(F&& action)
{
    try
    {
        action();
    }
    catch ( const std::runtime_error& )
    {
        return;
    }
    throw std::runtime_error("Malformed input was accepted");
}

Fixture topologyFixture()
{
    Fixture f;
    f.lumps[2].resize(16);
    writeFloat(f.lumps[2], 0, 1);
    writeFloat(f.lumps[2], 12, 2.5f);
    f.lumps[3].resize(36);
    write32(f.lumps[3], 4, -2); // Leaf 1, then leaf 2. Leaf zero is a sentinel.
    write32(f.lumps[3], 8, -3);
    write32(f.lumps[3], 12, -4);
    write32(f.lumps[3], 32, 9);
    f.lumps[4].resize(4 * 48); // Include a legal unreferenced submodel leaf.
    write32(f.lumps[4], 48 + 4, 2);
    write32(f.lumps[4], 48 + 8, -4);
    write32(f.lumps[4], 48 + 28, 9);
    write32(f.lumps[4], 48 + 36, 1);
    write32(f.lumps[4], 48 + 44, 1);
    write32(f.lumps[4], 96, -1);
    write32(f.lumps[4], 96 + 4, -1);
    f.lumps[5].resize(4);
    write32(f.lumps[5], 0, 1);
    f.lumps[6].resize(4);
    f.lumps[8].resize(12);
    write32(f.lumps[8], 4, 1);
    write32(f.lumps[7], 36, 1);
    f.lumps[9].resize(12);
    write32(f.lumps[9], 8, -1); // No bound draw surface.
    f.lumps[16].resize(8 + 4);
    write32(f.lumps[16], 0, 1);
    write32(f.lumps[16], 4, 4); // Padded visibility row.
    f.lumps[16][8] = std::byte{1};
    return f;
}

void topologyValidation()
{
    const auto fixture = topologyFixture();
    parseRavenBsp(fixture.bytes());
    for ( const auto& [lump, offset, value] : std::array<std::array<std::uint32_t, 3>, 17>{{
        {3, 4, 0}, {3, 4, INT32_MAX}, {3, 8, UINT32_MAX - 1}, {3, 8, 0x80000000u},
        {3, 0, 1}, {3, 12, 4}, {4, 48 + 8, 11}, {4, 48, 10}, {4, 48, UINT32_MAX - 1},
        {4, 48 + 40, INT32_MAX}, {4, 48 + 36, 2}, {5, 0, 2}, {6, 0, 1}, {2, 0, 0},
        {2, 12, 0x7fc00000u}, {16, 0, INT32_MAX}, {16, 4, 0}
    }} )
    {
        auto broken = fixture;
        write32(broken.lumps[lump], offset, value);
        mustFail([&] { parseRavenBsp(broken.bytes()); });
    }
    const auto bsp = parseRavenBsp(fixture.bytes());
    require(bsp.planes.at(0).normal.x == 1 && bsp.planes.at(0).distance == 2.5, "BSP plane equation");
    const auto& node = bsp.nodes.at(0);
    require(node.plane == 0 && node.children == std::array<std::int32_t, 2>{-2, -3}
            && node.minimum[0] == -4 && node.maximum[2] == 9, "Signed tree and bounds");
    const auto& leaf = bsp.leaves.at(1);
    require(bsp.leaves.size() == 4 && bsp.leaves.at(2).cluster == -1 && leaf.area == 2
            && leaf.minimum[0] == -4 && leaf.maximum[2] == 9 && leaf.leafSurfaceCount == 1
            && bsp.leafSurfaces.at(leaf.firstLeafSurface) == 1, "Visibility leaves and draw links");
    const auto& brush = bsp.brushes.at(bsp.leafBrushes.at(leaf.firstLeafBrush));
    const auto& side = bsp.brushSides.at(brush.firstSide);
    require(leaf.leafBrushCount == 1 && brush.sideCount == 1 && brush.shader == 0
            && side.surface == -1 && side.plane == 0 && side.shader == 0, "Leaf brush and plane links");
    require(bsp.visibility.clusterCount == 1 && bsp.visibility.rowBytes == 4
            && bsp.visibility.bits == std::vector<std::uint8_t>{1, 0, 0, 0}, "PVS row stride and bits");
    auto noVis = fixture;
    noVis.lumps[16].clear();
    require(parseRavenBsp(noVis.bytes()).visibility.bits.empty(), "Missing optional visibility lump");
    auto bad = fixture;
    bad.lumps[16].resize(8 + 9);
    write32(bad.lumps[16], 0, 9);
    write32(bad.lumps[16], 4, 1);
    mustFail([&] { parseRavenBsp(bad.bytes()); }); // Nine clusters need at least two bytes per row.
    bad = fixture;
    // A world-tree leaf may not claim an inline submodel's surfaces.
    auto submodel = bad.lumps[7];
    write32(submodel, 24, 1);
    write32(submodel, 28, 1);
    write32(submodel, 36, 0);
    write32(bad.lumps[7], 28, 1);
    bad.lumps[7].insert(bad.lumps[7].end(), submodel.begin(), submodel.end());
    mustFail([&] { parseRavenBsp(bad.bytes()); });
    bad = fixture;
    bad.lumps[16].pop_back();
    mustFail([&] { parseRavenBsp(bad.bytes()); });
    bad = fixture;
    bad.lumps[3].resize(72);
    write32(bad.lumps[3], 36 + 4, -1);
    write32(bad.lumps[3], 36 + 8, -4);
    mustFail([&] { parseRavenBsp(bad.bytes()); }); // Unreachable interior node.
    write32(bad.lumps[3], 4, 1);
    write32(bad.lumps[3], 8, 1);
    mustFail([&] { parseRavenBsp(bad.bytes()); }); // Shared interior node.
    // A deep, unbalanced source must not consume the C++ call stack.
    constexpr std::size_t depth = 10000;
    auto deep = fixture;
    deep.lumps[3].resize(depth * 36);
    deep.lumps[4].resize((depth + 1) * 48);
    for ( std::size_t i = 0; i < depth; ++i )
    {
        const auto index = static_cast<std::int32_t>(i);
        write32(deep.lumps[3], 36 * i + 4, -1 - index);
        write32(deep.lumps[3], 36 * i + 8, i + 1 < depth ? index + 1 : -2 - index);
    }
    require(parseRavenBsp(deep.bytes()).nodes.size() == depth, "Deep tree parsed iteratively");
    auto shared = fixture;
    shared.lumps[4].resize(4097 * 48);
    shared.lumps[5].resize(4096 * 4);
    for ( std::size_t i = 0; i < 4097; ++i )
    {
        write32(shared.lumps[4], i * 48 + 36, 4096);
    }
    mustFail([&] { parseRavenBsp(shared.bytes()); }); // Small file with 16M+ aliased references.
}

void patchGridDegeneracy()
{
    // The same non-linear control curve repeated in every row sweeps no area.
    // Coordinates are binary32 source values chosen away from the world origin.
    Fixture fixture;
    const std::array<std::array<float, 3>, 3> curve{{
        {1024, -1536, 448}, {1056, -1440, 472}, {1232, -1152, 488}
    }};
    for ( unsigned i = 0; i < 9; ++i )
    {
        for ( unsigned k = 0; k < 3; ++k )
        {
            writeFloat(fixture.lumps[10], (1 + i) * 80 + 4 * k, curve[i % 3][k]);
        }
    }
    const auto line = parseRavenBsp(fixture.bytes());
    for ( unsigned steps = 1; steps <= 16; ++steps )
    {
        require(ravenBspSurfaceTriangles(line, 1, steps).empty(), "Collapsed patch rows manufactured area");
    }
    // Even coincident curves with widely separated coordinate exponents must
    // stay collapsed; summation of nine terms in a different order can drift.
    auto mixed = fixture;
    const std::array<float, 3> rowX{-106264.f, 4.5812339521944523e-7f, 2.282470703125f};
    for ( unsigned i = 0; i < 9; ++i )
    {
        writeFloat(mixed.lumps[10], (1 + i) * 80, rowX[i % 3]);
    }
    require(ravenBspSurfaceTriangles(parseRavenBsp(mixed.bytes()), 1, 5).empty(),
            "Repeated identical curves with mixed exponents manufactured area");
    auto transposed = mixed;
    for ( unsigned y = 0; y < 3; ++y )
    {
        for ( unsigned x = 0; x < 3; ++x )
        {
            const auto from = (1 + x + 3 * y) * 80, to = (1 + y + 3 * x) * 80;
            std::copy_n(mixed.lumps[10].begin() + from, 12, transposed.lumps[10].begin() + to);
        }
    }
    require(ravenBspSurfaceTriangles(parseRavenBsp(transposed.bytes()), 1, 5).empty(),
            "Repeated identical columns manufactured area");
    // One small representable translation per row genuinely opens the curve.
    for ( unsigned i = 0; i < 9; ++i )
    {
        writeFloat(fixture.lumps[10], (1 + i) * 80 + 4, curve[i % 3][1] + (i / 3) / 4096.f);
    }
    const auto ribbon = parseRavenBsp(fixture.bytes());
    for ( unsigned steps : {3u, 5u} )
    {
        require(ravenBspSurfaceTriangles(ribbon, 1, steps).size() == 2 * steps * steps,
                "Narrow real patch lost triangles");
    }
    Fixture sampled;
    writeFloat(sampled.lumps[10], 5 * 80 + 52, 0.5f);
    for ( unsigned i = 0; i < 9; ++i )
    {
        writeFloat(sampled.lumps[10], (1 + i) * 80 + 16, static_cast<float>(i / 3) / 4);
        writeFloat(sampled.lumps[10], (1 + i) * 80 + 20, static_cast<float>(i % 3) / 4);
    }
    const auto samples = ravenBspSurfaceTriangles(parseRavenBsp(sampled.bytes()), 1, 3);
    bool foundSample = false;
    for ( const auto& triangle : samples )
    {
        for ( const auto& point : triangle.corners )
        {
            if ( std::abs(point.u - 1.0 / 3) > 1e-14 || std::abs(point.lightmapV - 2.0 / 3) > 1e-14 )
            {
                continue;
            }
            // At (1/3, 2/3) the central control point's weight is 16/81.
            require(std::abs(point.position.x - 2.0 / 3) < 1e-14
                    && std::abs(point.position.y - 4.0 / 3) < 1e-14
                    && std::abs(point.position.z - 64.0 / 81) < 1e-14
                    && std::abs(point.normal.x - 8.0 / 81) < 1e-14 && point.normal.z == 1
                    && std::abs(point.v - 1.0 / 3) < 1e-14 && std::abs(point.lightmapU - 1.0 / 6) < 1e-14
                    && std::abs(point.color.r - 16.0 / 81) < 1e-7 && point.color.a == 1,
                    "Rational patch positions, normals, UVs and color");
            foundSample = true;
        }
    }
    require(foundSample, "Missing thirds sample");
    // A patch ending in an apex has exactly one zero-area fan triangle for
    // each grid interval along its collapsed edge.
    for ( unsigned i = 0; i < 9; ++i )
    {
        const float x = static_cast<float>(i % 3), y = static_cast<float>(i / 3);
        writeFloat(fixture.lumps[10], (1 + i) * 80, 1891.f + 32.f * x * y);
        writeFloat(fixture.lumps[10], (1 + i) * 80 + 4, -2556.f + 20.f * y);
        writeFloat(fixture.lumps[10], (1 + i) * 80 + 8, 17.f + 0.25f * x * y);
    }
    const auto apex = parseRavenBsp(fixture.bytes());
    for ( unsigned steps = 1; steps <= 16; ++steps )
    {
        require(ravenBspSurfaceTriangles(apex, 1, steps).size() == 2 * steps * steps - steps,
                "Collapsed patch edge manufactured a triangle");
    }
}

void run()
{
    topologyValidation();
    patchGridDegeneracy();
    const Fixture fixture;
    const auto bsp = parseRavenBsp(fixture.bytes());
    require(bsp.models.size() == 1 && bsp.models[0].surfaceCount == 2, "Model range");
    require(bsp.entities.size() == 2 && bsp.entities[0].fields.size() == 3, "Duplicate entity keys");
    require(bsp.entities[0].fields[2].second == "two" && bsp.shaders[0].contents == 1, "Metadata");
    const auto planar = ravenBspSurfaceTriangles(bsp, 0, 2);
    require(planar.size() == 1 && planar[0].corners[1].position.x == 1, "Planar winding and degeneracy");
    const auto patch = ravenBspSurfaceTriangles(bsp, 1, 2);
    require(patch.size() == 8 && patch[0].surface == 1, "Patch triangle count");
    const auto& center = patch[0].corners[2];
    require(center.position.x == 1 && center.position.y == 1 && center.position.z == 1, "Quadratic patch center");
    require(center.u == 0.5 && center.lightmapV == 0.5 && center.color.r == 0.25f, "Interpolated attributes");
    mustFail([&] { ravenBspSurfaceTriangles(bsp, 0, 0); });
    mustFail([&] { ravenBspSurfaceTriangles(bsp, 0, 17); });
    mustFail([&] { ravenBspSurfaceTriangles(bsp, 2, 1); });
    for ( const auto& [lump, offset, value] : std::array<std::array<std::uint32_t, 3>, 8>{{
        {13, 148 + 140, 4}, {13, 36, 1}, {13, 0, 7}, {13, 20, UINT32_MAX}, {11, 0, 9},
        {7, 28, INT32_MAX}, {10, 80, 0x7fc00000u}, {13, 24, 5}
    }} )
    {
        auto broken = fixture;
        write32(broken.lumps[lump], offset, value);
        mustFail([&] { parseRavenBsp(broken.bytes()); });
    }
    // Some shipped RBSPs leave NaN in the unused UVs of vertex-lit surfaces.
    auto vertexLit = fixture;
    write32(vertexLit.lumps[10], 80 + 20, 0x7fc00000u);
    require(parseRavenBsp(vertexLit.bytes()).vertices[1].lightmapU == 0, "Absent lightmap UV padding");
    vertexLit.lumps[14].resize(128 * 128 * 3);
    write32(vertexLit.lumps[13], 36, 0);
    mustFail([&] { parseRavenBsp(vertexLit.bytes()); });
    auto geometryTypes = fixture;
    write32(geometryTypes.lumps[13], 8, 3);
    write32(geometryTypes.lumps[13], 148 + 8, 4);
    const auto typed = parseRavenBsp(geometryTypes.bytes());
    require(ravenBspSurfaceTriangles(typed, 0, 16).size() == 1, "Triangle soup");
    require(ravenBspSurfaceTriangles(typed, 1, 16).empty(), "Unconverted flare");
    auto largePatch = bsp;
    largePatch.surfaces[1].patchWidth = 35;
    largePatch.surfaces[1].patchHeight = 35;
    largePatch.surfaces[1].vertexCount = 35 * 35;
    largePatch.vertices.resize(1 + 35 * 35);
    mustFail([&] { ravenBspSurfaceTriangles(largePatch, 1, 16); });
    auto alias = fixture;
    const auto world = alias.lumps[7];
    alias.lumps[7].insert(alias.lumps[7].end(), world.begin(), world.end());
    mustFail([&] { parseRavenBsp(alias.bytes()); });
    // A separate brush submodel may own an empty surface range.
    write32(alias.lumps[7], 40 + 24, 2);
    write32(alias.lumps[7], 40 + 28, 0);
    require(parseRavenBsp(alias.bytes()).models.size() == 2, "Empty submodel range");
    auto badName = fixture;
    std::fill_n(badName.lumps[1].begin(), 64, std::byte{'x'});
    mustFail([&] { parseRavenBsp(badName.bytes()); });
    auto danglingQuote = fixture;
    danglingQuote.lumps[0][danglingQuote.lumps[0].size() - 3] = std::byte{'x'};
    mustFail([&] { parseRavenBsp(danglingQuote.bytes()); });
    auto invalid = fixture;
    invalid.lumps[0][0] = std::byte{'x'};
    mustFail([&] { parseRavenBsp(invalid.bytes()); });
    auto bytes = fixture.bytes();
    write32(bytes, 8 + 8 * 10, 152);
    mustFail([&] { parseRavenBsp(bytes); });
    bytes = fixture.bytes();
    write32(bytes, 4, 2);
    mustFail([&] { parseRavenBsp(bytes); });
    bytes.resize(151);
    mustFail([&] { parseRavenBsp(bytes); });
}
}

int main()
{
    try
    {
        run();
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
