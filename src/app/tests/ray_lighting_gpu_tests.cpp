#include "ui/lighting/ray_lighting_gpu.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"

#include <QGuiApplication>
#include <QOffscreenSurface>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    const bool requireGpu = argc > 1 && std::string_view(argv[1]) == "--require-gpu";
    const auto surface = sed::ui::lighting::createRayLightingGpuSurface();
    return std::async(std::launch::async, [&]()
    {
        using namespace sed::level;
        sed::ui::lighting::RayLightingGpu unavailable(nullptr);
        if ( unavailable.available() || unavailable.description().isEmpty() )
        {
            std::cerr << "FAIL: absent GPU context must expose an explicit CPU fallback reason.\n";
            return 1;
        }
        sed::ui::lighting::RayLightingGpu gpu(surface.get());
        std::cout << gpu.description().toStdString() << '\n';
        if ( !gpu.available() )
        {
            if ( requireGpu )
            {
                std::cerr << "FAIL: OpenGL compute intersections must execute on this test device.\n";
                return 1;
            }
            return 77;
        }
        const std::vector<RayLightTriangle> triangles{
            { { -1, -1, 2 }, { 1, -1, 2 }, { 0, 1, 2 } },
            { { -1, -1, 3 }, { 1, -1, 3 }, { 0, 1, 3 } },
            { { -1, -1, 2 }, { 1, -1, 2 }, { 0, 1, 2 } }
        };
        const std::vector<RayLightNode> nodes{
            { { -1, -1, 2 }, { 1, 1, 3 }, 1, 2, 0, 0 },
            { { -1, -1, 2 }, { 1, 1, 2 }, -1, -1, 0, 1 },
            { { -1, -1, 2 }, { 1, 1, 3 }, -1, -1, 1, 2 }
        };
        const std::vector<RayLightRay> rays{
            { { 0, 0, 0 }, { 0, 0, 1 }, 0, 10, 0 },
            { { 2, 0, 0 }, { 0, 0, 1 }, 0, 10, 0 },
            { { 0, 0, 0 }, { 0, 0, 1 }, 2, 10, 0 },
            { { 0, 0, 4 }, { 0, 0, -1 }, 0, 1, 0 },
            { { 0, 0, 0 }, { 0, 0, 1 }, 0, 1, 0 },
            { { 0, 0, 0 }, { 0, 0, 1 }, 0, 10, -1 }
        };
        std::vector<RayLightHit> hits(rays.size());
        if ( !gpu.intersect(triangles, nodes, rays, hits) )
        {
            std::cerr << "FAIL: compute dispatch/readback: " << gpu.description().toStdString() << '\n';
            return 1;
        }
        constexpr int expected[]{ 0, -1, 1, 1, -1, -1 };
        for ( std::size_t i = 0; i < hits.size(); ++i )
        {
            if ( hits[i].triangle != expected[i] )
            {
                std::cerr << "FAIL: nearest hit, interval, tie, or per-sector root at ray " << i << '\n';
                return 1;
            }
        }
        if ( std::abs(hits[0].distance - 2) > 1e-6f
            || std::abs(hits[0].u - 0.25f) > 1e-6f
            || std::abs(hits[0].v - 0.5f) > 1e-6f )
        {
            std::cerr << "FAIL: compute output must retain distance and barycentric interpolation.\n";
            return 1;
        }
        std::vector<RayLightRay> grid{};
        for ( int y = 0; y < 33; ++y )
        {
            for ( int x = 0; x < 33; ++x )
            {
                grid.push_back({ { (x - 16) * 0.127f, (y - 16) * 0.113f, 0 },
                    { 0.01f, 0.02f, 1 }, 0, 10, 0 });
            }
        }
        std::vector<RayLightHit> expectedHits(grid.size()), computedHits(grid.size());
        if ( !intersectRayLightingCpu(triangles, nodes, grid, expectedHits)
            || !gpu.intersect(triangles, nodes, grid, computedHits) )
        {
            std::cerr << "FAIL: multi-dispatch parity fixture must execute.\n";
            return 1;
        }
        for ( std::size_t i = 0; i < grid.size(); ++i )
        {
            if ( computedHits[i].triangle != expectedHits[i].triangle
                || std::abs(computedHits[i].distance - expectedHits[i].distance) > 5e-5f
                || std::abs(computedHits[i].u - expectedHits[i].u) > 5e-5f
                || std::abs(computedHits[i].v - expectedHits[i].v) > 5e-5f )
            {
                std::cerr << "FAIL: CPU/compute nearest hit agreement at query " << i << '\n';
                return 1;
            }
        }
        {
            sed::ui::lighting::RayLightingGpu tinyGpu(surface.get());
            const std::array<RayLightTriangle, 1> tiny{{ { { 0, 0, 1 }, { 1e-5f, 0, 1 }, { 0, 1e-5f, 1 } } }};
            const std::array<RayLightNode, 1> leaf{{ { { 0, 0, 1 }, { 1e-5f, 1e-5f, 1 }, -1, -1, 0, 1 } }};
            const std::array<RayLightRay, 1> query{{ { { 1e-6f, 1e-6f, 0 }, { 0, 0, 1 }, 0, 10, 0 } }};
            std::array<RayLightHit, 1> tinyHit{};
            if ( !tinyGpu.intersect(tiny, leaf, query, tinyHit) || tinyHit[0].triangle != 0 )
            {
                std::cerr << "FAIL: small opaque triangles must not leak rays.\n";
                return 1;
            }
        }
        {
            sed::Level reference{};
            reference.kind = sed::ProjectType::MOTS;
            levelCreateSectorPrimitiveBox(reference, SectorPrimitive::Cube, { 0, 0, 0 }, { 1, 1, 1 });
            levelCreateSectorPrimitiveBox(reference, SectorPrimitive::Cube, { 1, 0, 0 }, { 2, 1, 1 });
            levelCreateSectorPrimitiveBox(reference, SectorPrimitive::Cube, { 1, 1, 0 }, { 2, 2, 1 });
            if ( !levelAdjoinSectors(reference, 0, 1) || !levelAdjoinSectors(reference, 1, 2) )
            {
                std::cerr << "FAIL: complete bake fixture needs reciprocal room portals.\n";
                return 1;
            }
            for ( auto& face : reference.sectors[0].surfaces )
            {
                if ( face.normal.x > 0.9 )
                {
                    face.surfaceFlags |= sed::SF_SkyHorizon;
                    face.light = 0;
                }
            }
            auto accelerated = reference;
            const std::array<std::int32_t, 3> targets{ 0, 1, 2 };
            RayLightingOptions options{};
            options.samples = 64;
            options.bounces = 2;
            options.skyColor = { 0.4f, 0.6f, 1, 1 };
            const auto cpu = bakeRayLighting(reference, targets, options);
            sed::ui::lighting::RayLightingGpu sceneGpu(surface.get());
            const auto compute = bakeRayLighting(accelerated, targets, options, {}, {},
                [&sceneGpu](auto shapes, auto tree, auto queries, auto answers)
                {
                    return sceneGpu.intersect(shapes, tree, queries, answers);
                });
            if ( !cpu.succeeded || !compute.succeeded || cpu.rays != compute.rays )
            {
                std::cerr << "FAIL: complete CPU/compute portal and bounce bake: " << cpu.message
                    << ' ' << compute.message << '\n';
                return 1;
            }
            float maximumError = 0;
            float maximumLight = 0;
            for ( std::size_t s = 0; s < reference.sectors.size(); ++s )
            {
                for ( std::size_t f = 0; f < reference.sectors[s].surfaces.size(); ++f )
                {
                    for ( std::size_t v = 0; v < reference.sectors[s].surfaces[f].texVertices.size(); ++v )
                    {
                        const auto a = reference.sectors[s].surfaces[f].texVertices[v].color;
                        const auto b = accelerated.sectors[s].surfaces[f].texVertices[v].color;
                        maximumLight = std::max({ maximumLight, a.r, a.g, a.b });
                        maximumError = std::max({ maximumError, std::abs(a.r-b.r), std::abs(a.g-b.g),
                            std::abs(a.b-b.b), std::abs(a.a-b.a) });
                    }
                }
            }
            std::cout << "Complete three-room/two-bounce CPU/compute maximum channel error: " << maximumError << '\n';
            if ( maximumError > 2e-4f || maximumLight <= 0.01f || cpu.rays == 0 )
            {
                std::cerr << "FAIL: complete compute bake must agree with an illuminated CPU reference.\n";
                return 1;
            }
        }
        std::cout << "PASS: native compute, CPU parity, intervals, tiny blockers, portals and multiple bounces.\n";
        return 0;
    }).get();
}
