#include "sed/assets/jo_ja_structural_planes.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace sed;
using namespace sed::assets;

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

struct Fixture
{
    RavenBsp bsp;
    std::vector<JoJaHybridTriangle> triangles;
    Fixture()
    {
        bsp.shaders = {{"world", 0, 0x8001}, {"also-solid", 0, 0x8001}};
        bsp.surfaces.resize(2);
        bsp.surfaces.front().type = bsp.surfaces.back().type = 1;
        bsp.models = {{{-2, -2, -2}, {2, 2, 2}, 0, 1, 0, 1}, {{-2, -2, -2}, {2, 2, 2}, 1, 1, 1, 0}};
        bsp.planes = {{{1, 0, 0}, 2},  {{-1, 0, 0}, 2}, {{0, 1, 0}, 2},
                      {{0, -1, 0}, 2}, {{0, 0, 1}, 2},  {{0, 0, -1}, 2}};
        bsp.brushes.push_back({0, 6, 0});
        for ( int i = 0; i < 6; ++i )
        {
            bsp.brushSides.push_back({i, 0, 1}); // Wrong draw-surface reference on purpose.
        }
        triangles.push_back(make({0, 0, 2}, {2, 0, 2}, {0, 2, 2}, {0, 0, 1}));
    }

    static JoJaHybridTriangle make(TVector a, TVector b, TVector c, TVector normal)
    {
        JoJaHybridTriangle t;
        t.triangle.corners = {RavenBspVertex{a, normal}, RavenBspVertex{b, normal}, RavenBspVertex{c, normal}};
        return t;
    }

    auto planes() const
    {
        return findJoJaStructuralPlanes(bsp, triangles);
    }
};

void run()
{
    Fixture f;
    f.triangles.push_back(Fixture::make({0, 0, 2}, {-2, 0, 2}, {0, -2, 2}, {0, 0, 1}));
    f.triangles.push_back(Fixture::make({-2, 0, 0}, {-2, 0, 1}, {-2, 1, 0}, {-1, 0, 0}));
    const auto supported = f.planes();
    check(supported.size() == 2 && supported[0].area == 4 && supported[0].triangles == std::vector<std::size_t>{0, 1} &&
              supported[1].area == 0.5 && supported[1].plane.normal.x == 1 && supported[1].plane.distance == -2,
          "rank source planes by area and canonicalize negative-facing orientation");
    Fixture rejected;
    rejected.bsp.brushSides[4].shader = 1;
    check(rejected.planes().empty(), "a different side shader cannot certify a triangle");
    rejected = Fixture{};
    rejected.bsp.brushSides[0].surface = 0;
    rejected.triangles[0].triangle.corners[0].position.z -= 0.25;
    check(rejected.planes().empty(), "a matching side.surface cannot certify noncoplanar geometry");
    rejected = Fixture{};
    rejected.triangles[0].triangle.corners[1].position.x = 2.25;
    check(rejected.planes().empty(), "a centroid inside the brush is insufficient");
    rejected = Fixture{};
    rejected.bsp.planes.push_back({{1, 1, 0}, 1});
    rejected.bsp.brushSides.push_back({6, 0, 0});
    ++rejected.bsp.brushes[0].sideCount;
    check(rejected.planes().empty(), "all oblique brush halfspaces must contain every corner");
    for ( auto contents : {0x8000u, 1u, 0x80008001u} )
    {
        rejected = Fixture{};
        rejected.bsp.brushes[0].shader = 1;
        rejected.bsp.shaders[1].contents = contents;
        check(rejected.planes().empty(), "only a solid opaque nontranslucent brush supplies support");
        rejected = Fixture{};
        rejected.bsp.brushes[0].shader = 1;
        rejected.bsp.shaders[0].contents = contents;
        check(rejected.planes().empty(), "draw shader must also certify solid opaque nontranslucent content");
    }
    rejected = Fixture{};
    rejected.bsp.models[0].brushCount = 0;
    rejected.bsp.models[1].firstBrush = 0;
    rejected.bsp.models[1].brushCount = 1;
    check(rejected.planes().empty(), "submodel collision cannot support a world triangle");
    rejected = Fixture{};
    rejected.triangles[0].triangle.surface = 1;
    check(rejected.planes().empty(), "submodel render geometry cannot become a structural plane");
    rejected = Fixture{};
    rejected.bsp.surfaces[0].type = 2;
    check(rejected.planes().empty(), "a locally flat patch stays model detail");
    rejected = Fixture{};
    for ( auto& v : rejected.triangles[0].triangle.corners )
    {
        v.normal = {0, 0, -1};
    }
    std::swap(rejected.triangles[0].triangle.corners[1], rejected.triangles[0].triangle.corners[2]);
    check(rejected.planes().empty(), "an inward-facing triangle cannot certify a brush's outward side");
    Fixture offset;
    for ( auto& v : offset.triangles[0].triangle.corners )
    {
        v.position.z += 0.0008;
    }
    const auto drawPlanes = offset.planes();
    check(drawPlanes.size() == 1 && std::abs(drawPlanes[0].plane.distance - 2.0008) < 1e-12,
          "partition plane comes from render geometry even when its supporting plane differs by source precision");
    offset.triangles.push_back(Fixture{}.triangles[0]);
    check(offset.planes().size() == 2, "distinct parallel draw planes remain separate even with the same support");
    Fixture angular;
    angular.triangles[0].triangle.corners[1].position.z += 0.0001;
    check(angular.planes().empty(), "distance tolerance alone does not certify a tilted triangle");
    Fixture oblique;
    oblique.bsp.planes.push_back({{1, 1, 0}, 1});
    oblique.bsp.brushSides.push_back({6, 0, -1});
    ++oblique.bsp.brushes[0].sideCount;
    oblique.triangles[0] = Fixture::make({1, 0, 0}, {0, 1, 0}, {0, 1, 1}, {1, 1, 0});
    const auto ramp = oblique.planes();
    check(ramp.size() == 1 && std::abs(ramp[0].plane.normal.x - std::sqrt(0.5)) < 1e-12,
          "verified oblique sides support real geometry independently of axial bevels and plane normalization");
}

void crossSections()
{
    Fixture f;
    const std::array<TVector, 4> wide{{{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}}};
    const RavenBspPlane xy{{0, 0, 1}, 0};
    const auto area = [](const auto& ring)
    {
        TVector sum{};
        for ( std::size_t i = 1; i + 1 < ring.size(); ++i )
        {
            sum = sum + (ring[i] - ring.front()).cross(ring[i + 1] - ring.front());
        }
        return sum.z / 2;
    };
    JoJaStructuralBrushIndex index(f.bsp);
    const auto cuts = index.crossSections(xy, wide, 16);
    check(cuts.size() == 1 && std::abs(area(cuts.front()) - 16) < 1e-12,
          "intersect the actual solid box with the middle of the interface");
    const std::array<TVector, 4> inside{{{-1, -1, 0}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}}};
    const auto blocked = index.crossSections(xy, inside, 16);
    check(blocked.size() == 1 && blocked.front() == std::vector<TVector>(inside.begin(), inside.end()),
          "a completely blocked aperture preserves its exact original ring and reversed winding");
    const std::array<TVector, 4> edgeOnly{{{2, -1, 0}, {3, -1, 0}, {3, 1, 0}, {2, 1, 0}}};
    check(index.crossSections(xy, edgeOnly, 16).empty(), "line contact cannot close a portal");
    f.bsp.planes.push_back({{1, 1, 0}, 0});
    f.bsp.brushSides.push_back({6, 0, -1});
    ++f.bsp.brushes[0].sideCount;
    const auto ramp = JoJaStructuralBrushIndex(f.bsp).crossSections(xy, wide, 16);
    check(ramp.size() == 1 && std::abs(area(ramp.front()) - 8) < 1e-12,
          "clip the section against the actual oblique side");
    f.bsp.planes.push_back({{-1, -1, 0}, 0});
    f.bsp.brushSides.push_back({7, 0, -1});
    ++f.bsp.brushes[0].sideCount;
    const std::array<TVector, 4> diagonal{{{-1, 1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, 1, 1}}};
    check(JoJaStructuralBrushIndex(f.bsp).crossSections({{1, 1, 0}, 0}, diagonal, 16).empty(),
          "a zero-thickness oblique brush cannot block its apparent coplanar aperture");
    Fixture translucent;
    translucent.bsp.shaders[0].contents |= 0x80000000u;
    check(JoJaStructuralBrushIndex(translucent.bsp).crossSections(xy, wide, 16).empty(),
          "translucent brush is not a solid occlusion mask");
}
} // namespace

int main()
{
    try
    {
        run();
        crossSections();
    }
    catch ( const std::exception& e )
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
