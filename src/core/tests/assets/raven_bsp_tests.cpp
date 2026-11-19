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

void run()
{
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
