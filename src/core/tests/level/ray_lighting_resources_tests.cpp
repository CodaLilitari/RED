#include "../assets/resource_locator_test_fixtures.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/tpl.hpp"
#include "sed/level/ray_lighting_resources.hpp"

#include <sstream>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool ok, const char* message)
{
    if ( !ok )
    {
        throw std::runtime_error(message);
    }
}
template <class Function> void refused(Function&& work, const char* expected)
{
    try
    {
        work();
    }
    catch ( const std::runtime_error& error )
    {
        if ( std::string_view(error.what()).find(expected) != std::string_view::npos )
        {
            return;
        }
        throw;
    }
    throw std::runtime_error(std::string("expected refusal: ") + expected);
}

void staticModel()
{
    using namespace sed;
    using namespace sed::level;
    Level level;
    level.sectors.resize(3);
    for ( auto& sector : level.sectors )
    {
        sector.vertices.resize(2);
        sector.vertices[0].x = -5;
        sector.vertices[0].y = -5;
        sector.vertices[0].z = -5;
        sector.vertices[1].x = 5;
        sector.vertices[1].y = 5;
        sector.vertices[1].z = 5;
    }
    // Sector 2 overlaps geometrically but is disconnected.
    level.sectors[0].surfaces.emplace_back().adjoin = SurfaceRef{1, 0};
    level.sectors[1].surfaces.emplace_back().adjoin = SurfaceRef{0, 0};
    ThingTemplate tpl;
    tpl.name = "architecture";
    TplValue flags;
    flags.name = "thingflags";
    flags.type = ValueType::flag;
    flags.intValue = 8;
    TplValue modelValue;
    modelValue.name = "model3d";
    modelValue.type = ValueType::str;
    modelValue.stringValue = "blocker.3do";
    tpl.values = {flags, modelValue};
    assets::Templates templates;
    templates.addOrUpdate(tpl);
    auto& thing = level.things.emplace_back();
    thing.templateName = tpl.name;
    thing.sector = 0;
    thing.x = 2;
    thing.yaw = 90;
    assets::Model3do model;
    model.version = 2.1;
    model.hierarchyTransformsApplied = false;
    model.materials.push_back("wall.mat");
    auto& mesh = model.meshes.emplace_back();
    mesh.name = "box";
    mesh.geo = 4;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.texVertices.resize(3);
    auto& poly = mesh.faces.emplace_back();
    poly.geo = 4;
    poly.vertices = {0, 1, 2};
    poly.texVertices = {0, 1, 2};
    auto& node = model.nodes.emplace_back();
    node.name = "root";
    node.mesh = 0;
    node.position.x = 1;
    assets::RgbaImage image{8, 8, std::vector<std::uint8_t>(256, 255)};
    std::string mat = assets::buildRgb565Mat({image}).bytes;
    std::string wire;
    auto update = [&]
    {
        std::ostringstream out;
        assets::saveTo3DO(model, out);
        wire = out.str();
    };
    update();
    const EmissiveResourceReader reader = [&](std::string_view name,
                                              std::string_view) -> std::optional<std::vector<std::byte>>
    {
        const auto& source = name == "wall.mat" ? mat : wire;
        const auto bytes = std::as_bytes(std::span(source));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    auto resolve = [&]
    {
        return resolveRayLightingResources(level, reader, {}, &templates);
    };
    auto resources = resolve();
    check(resources.modelInstances == 1 && resources.blockers.size() == 2 && resources.blockers[0].sector == 0 &&
              resources.blockers[1].sector == 1,
          "hierarchy model reaches only connected overlapping sectors");
    const auto expected = TMatrix43::Create({2, 0, 0}, {0, 90, 0}).TransformPoint({1, 0, 0});
    const auto& a = resources.blockers[0].triangle.a;
    check(std::abs(a.x - expected.x) < 1.e-5 && std::abs(a.y - expected.y) < 1.e-5,
          "hierarchy bind transform followed by Thing placement");
    const auto hash = rayLightingResourceInputsFingerprint(level, &templates);
    thing.yaw = 45;
    check(hash != rayLightingResourceInputsFingerprint(level, &templates), "pose binds publication");
    thing.yaw = 90;
    poly.faceFlags = FF_Transluent;
    update();
    refused(resolve, "translucent");
    poly.faceFlags = 0;
    update();
    // Single-cel texture-data header alphaFlags is independent of ColorInfo.
    mat.at(76 + 40 + 8) = 1;
    refused(resolve, "alpha-keyed");
    mat = assets::buildRgb565Mat({image, image}).bytes;
    refused(resolve, "animated");
    tpl.values[0].intValue = 0;
    templates.addOrUpdate(tpl);
    check(hash != rayLightingResourceInputsFingerprint(level, &templates) && resolve().blockers.empty(),
          "effective external template edits invalidate the receipt and dynamic models are excluded");
}
void sourceReceipts()
{
    using namespace sed;
    using namespace sed::level;
    tests::ResourceLocatorFixture fixture;
    auto archive = [&](const char* name)
    {
        std::ofstream file(fixture.firstGob, std::ios::binary | std::ios::trunc);
        const auto data = assets::buildGobBytes({{name, "same bytes at the same position"}});
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
    };
    archive("mat/a.mat");
    assets::ResourceLocator locator;
    locator.mountGob(fixture.firstGob);
    auto first = makeRayLightingResourceReader(locator)("a", ".mat");
    check(first.has_value(), "path-backed archive lookup and extension fallback");
    RayLightingResources snapshot;
    snapshot.dependencies.push_back({"a", ".mat", std::make_shared<const std::vector<std::byte>>(*first)});
    archive("mat/b.mat");
    check(!rayLightingResourcesCurrent(snapshot, makeRayLightingResourceReader(locator)),
          "replaced archive directory detected even when old entry payload remains at same offset");
    Level level;
    level.kind = ProjectType::MOTS;
    auto& corner = level.sectors.emplace_back().surfaces.emplace_back().texVertices.emplace_back();
    auto original = rayLightingResourceInputsFingerprint(level);
    corner.color.a = 0.25f;
    check(original != rayLightingResourceInputsFingerprint(level), "preserved MotS white slot binds publication");
    original = rayLightingResourceInputsFingerprint(level);
    level.colormaps.emplace_back("new.cmp");
    check(original != rayLightingResourceInputsFingerprint(level), "global CMP table binds publication");
    level.things.emplace_back().templateName = "unresolved";
    refused(
        [&]
        {
            resolveRayLightingResources(level, {});
        },
        "missing Thing template");
}

void indexedPalette()
{
    using namespace sed;
    using namespace sed::level;
    Level level;
    auto& sector = level.sectors.emplace_back();
    sector.colormap = "warm.cmp";
    sector.surfaces.emplace_back().material = "wall.mat";
    auto palette = assets::makeGrayscalePalette();
    assets::RgbaImage image{8, 8, std::vector<std::uint8_t>(256, 255)};
    const auto encoded = assets::buildIndexedMat({image}, palette).bytes;
    const auto bytes = std::as_bytes(std::span(encoded));
    std::vector<std::byte> cmp(64 + 256 * 3);
    std::memcpy(cmp.data(), "CMP ", 4);
    cmp[64 + 255 * 3] = std::byte{255};
    EmissiveResourceReader reader = [&](std::string_view name,
                                        std::string_view) -> std::optional<std::vector<std::byte>>
    {
        if ( name == "warm.cmp" )
        {
            return cmp;
        }
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    auto result = resolveRayLightingResources(level, reader);
    check(result.dependencies.size() == 2 && result.materials[0].albedo.r == 1 && result.materials[0].albedo.g == 0,
          "indexed albedo uses selected sector CMP");
    cmp.clear();
    refused(
        [&]
        {
            resolveRayLightingResources(level, reader);
        },
        "invalid CMP");
}

void run()
{
    using namespace sed;
    using namespace sed::level;
    Level level;
    auto& sector = level.sectors.emplace_back();
    Surface face;
    face.geo = 4;
    face.material = "sky_named_but_ordinary.mat";
    sector.surfaces.push_back(face);
    face.surfaceFlags = SF_SkyCeiling;
    sector.surfaces.push_back(face);
    assets::RgbaImage image{8, 8, std::vector<std::uint8_t>(256, 255)};
    for ( std::size_t i = 0; i < image.rgba.size(); i += 4 )
    {
        image.rgba[i] = 128;
    }
    auto wire = assets::buildRgb565Mat({image}).bytes;
    unsigned reads = 0;
    EmissiveResourceReader reader = [&](std::string_view name,
                                        std::string_view) -> std::optional<std::vector<std::byte>>
    {
        check(name == face.material, "true color must not require CMP");
        ++reads;
        std::vector<std::byte> bytes(wire.size());
        std::memcpy(bytes.data(), wire.data(), wire.size());
        return bytes;
    };
    auto resolved = resolveRayLightingResources(level, reader);
    check(reads == 1 && resolved.dependencies.size() == 1, "unique captured MAT");
    check(resolved.materials.size() == 2 && !resolved.materials[0].sky && resolved.materials[1].sky,
          "only flagged sky");
    auto red = resolved.materials[0].albedo.r;
    check(red > 0.21 && red < 0.25 && resolved.materials[0].emission.r == red, "linear FULLYLIT radiance");
    check(resolved.materials[1].emission.r == 0, "sky never duplicates emission");
    check(rayLightingResourcesCurrent(resolved, reader), "immutable lookup matches");
    refused(
        [&]
        {
            resolveRayLightingResources(level, reader,
                                        []
                                        {
                                            return true;
                                        });
        },
        "cancelled");
    refused(
        [&]
        {
            resolveRayLightingResources(level,
                                        [](auto, auto)
                                        {
                                            return std::nullopt;
                                        });
        },
        "missing");
    wire.back() ^= 1;
    check(!rayLightingResourcesCurrent(resolved, reader), "changed bytes invalidate receipt");
}
} // namespace
int main()
{
    try
    {
        run();
        staticModel();
        sourceReceipts();
        indexedPalette();
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
