#include "sed/assets/png.hpp"
#include "sed/render/software_snapshot.hpp"
#include "test_common.hpp"
#include <span>

namespace sed::tests
{
namespace
{
std::array<std::uint8_t, 3> center(const render::SoftwareSnapshotResult &result)
{
    const auto image = assets::decodePng(std::as_bytes(std::span(result.png)));
    const auto i = (32u * 64u + 32u) * 4u;
    return {image.rgba[i], image.rgba[i + 1], image.rgba[i + 2]};
}
Sector quad(double z, const char *material)
{
    Sector result;
    result.ambient = ColorWhite;
    result.vertices = {{-1, -1, z}, {1, -1, z}, {1, 1, z}, {-1, 1, z}};
    Surface face;
    face.geo = 4;
    face.material = material;
    face.vertices = {0, 1, 2, 3};
    face.texVertices = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    face.normal = {0, 0, 1};
    result.surfaces = {face};
    return result;
}
} // namespace
void runSnapshotGeometryTests()
{
    using namespace render;
    Level world;
    world.sectors = {quad(0, "red")};
    SnapshotGeometry model;
    model.sector = quad(1, "green");
    SnapshotMaterial red, green;
    red.image = {1, 1, {255, 0, 0, 255}};
    green.image = {1, 1, {0, 255, 0, 255}};
    red.trueColor = green.trueColor = true;
    const SnapshotMaterialResolver resolve = [&](std::string_view key)
    { return key == "red" ? &red : &green; };
    SoftwareSnapshotOptions options;
    options.width = options.height = 64;
    options.fullbright = true;
    options.camera.position = {0, 0, 5};
    options.camera.target = {0, 0, 0};
    options.camera.orthographicHeight = 4;
    for ( const bool ortho : {false, true} )
    {
        options.camera.orthographic = ortho;
        model.sector = quad(1, "green");
        const auto foreground = renderSoftwareSnapshot(world, resolve, options, {&model, 1});
        check(center(foreground) == std::array<std::uint8_t, 3>{0, 255, 0},
              "Thing geometry shares depth and covers farther world");
        check(foreground.surfacesDrawn == 1 && foreground.thingSurfacesSubmitted == 1,
              "world and Thing geometry have separate submission counters");
        model.sector = quad(-1, "green");
        check(center(renderSoftwareSnapshot(world, resolve, options, {&model, 1})) ==
                  std::array<std::uint8_t, 3>{255, 0, 0},
              "nearer world occludes Thing geometry");
        model.sector = quad(1, "green");
        green.image.rgba[3] = 0;
        check(center(renderSoftwareSnapshot(world, resolve, options, {&model, 1})) ==
                  std::array<std::uint8_t, 3>{255, 0, 0},
              "transparent model texels leave world color and depth");
        green.image.rgba[3] = 255;
        model.sector.surfaces[0].geo = 0;
        check(center(renderSoftwareSnapshot(world, resolve, options, {&model, 1})) ==
                  std::array<std::uint8_t, 3>{255, 0, 0},
              "hidden Thing face writes no depth");
    }
    model.sector = quad(1, "green");
    model.sector.surfaces[0].normal = {0, 0, -1};
    options.cullBackFaces = true;
    check(center(renderSoftwareSnapshot(world, resolve, options, {&model, 1})) ==
              std::array<std::uint8_t, 3>{255, 0, 0},
          "back-facing ordinary model respects cull option");
    model.sector.surfaces[0].faceFlags = FF_DoubleSided;
    check(center(renderSoftwareSnapshot(world, resolve, options, {&model, 1})) ==
              std::array<std::uint8_t, 3>{0, 255, 0},
          "double-sided model face remains visible");
    model.sector.surfaces[0].faceFlags = 0;
    model.doubleSided = true;
    check(center(renderSoftwareSnapshot(world, resolve, options, {&model, 1})) ==
              std::array<std::uint8_t, 3>{0, 255, 0},
          "sprite card double-sided mode remains visible");
    // Pixel-space 2.1 meshes and normalized 2.3 meshes must sample identically
    // even when the enclosing world has the opposite UV convention.
    options.cullBackFaces = false;
    green.image = {2, 2, {0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 0, 255, 255}};
    model.sector = quad(1, "green");
    model.uvCoordinateSpace = UvCoordinateSpace::Normalized;
    const auto normalized = renderSoftwareSnapshot(world, resolve, options, {&model, 1});
    for ( auto &uv : model.sector.surfaces[0].texVertices )
    {
        uv.u *= 2;
        uv.v *= 2;
    }
    model.uvCoordinateSpace = UvCoordinateSpace::TexturePixels;
    check(renderSoftwareSnapshot(world, resolve, options, {&model, 1}).png == normalized.png,
          "asymmetric texture proves model pixel/normalized UV equivalence");
    // A model face is shaded by its sector's ambient light unless it is in
    // light mode 0 (fully lit), like a lamp's lens.
    {
        green.image = {1, 1, {0, 255, 0, 255}};
        model.uvCoordinateSpace = UvCoordinateSpace::TexturePixels;
        model.sector = quad(1, "green");
        model.sector.ambient = ColorF{ 0.25f, 0.25f, 0.25f, 0.0f };
        options.fullbright = false;
        model.sector.surfaces[0].light = 3;
        const auto shaded = center(renderSoftwareSnapshot(world, resolve, options, {&model, 1}));
        model.sector.surfaces[0].light = 0;
        const auto lit = center(renderSoftwareSnapshot(world, resolve, options, {&model, 1}));
        check(shaded[1] < 80 && lit == std::array<std::uint8_t, 3>{0, 255, 0},
              "a fully lit model face ignores its sector's ambient light");
        options.fullbright = true;
    }
    // Keep the original three-argument callable type, not just default arguments.
    SoftwareSnapshotResult (*legacyCall)(const Level &, const SnapshotMaterialResolver &,
                                         const SoftwareSnapshotOptions &) = &renderSoftwareSnapshot;
    check(legacyCall(world, resolve, options).thingSurfacesSubmitted == 0,
          "original typed renderer function remains callable");
    options.maxRasterSamples = 1;
    checkThrowsWithMessage([&] { (void)renderSoftwareSnapshot(world, resolve, options, {&model, 1}); },
                           "snapshot: raster sample budget exceeded",
                           "raster work cap refuses before candidate pixel loops");
}
} // namespace sed::tests