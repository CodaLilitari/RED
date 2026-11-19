#include "sed/assets/3do.hpp"
#include "sed/assets/spr.hpp"
#include "sed/render/snapshot_things.hpp"
#include "test_common.hpp"
#include <cmath>
#include <limits>
#include <map>
namespace sed::tests
{
namespace
{
TplValue visual(const char* key, const char* name)
{
    TplValue v;
    v.name = key;
    v.stringValue = name;
    return v;
}
std::string triangle(double version = 2.1, bool shared = false, bool hidden = false)
{
    assets::Model3do model;
    model.version = version;
    model.hierarchyTransformsApplied = false;
    model.geosetCount = 1;
    model.materials = {"test.mat"};
    model.insertOffset = {100, 100, 100};
    assets::Model3doMesh mesh;
    mesh.name = "triangle";
    mesh.geo = hidden ? 0 : 4;
    mesh.light = 3;
    mesh.tex = 1;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    mesh.texVertices = {{0, 0}, {8, 0}, {0, 8}};
    assets::Model3doFace face;
    face.geo = 4;
    face.light = 3;
    face.tex = 1;
    face.vertices = {0, 1, 2};
    face.texVertices = {0, 1, 2};
    mesh.faces = {face};
    model.meshes = {mesh};
    assets::Model3doNode node;
    node.name = "root";
    node.mesh = 0;
    node.position = {2, 0, 0};
    node.pivot = {1, 0, 0};
    model.nodes = {node};
    if ( shared )
    {
        node.id = 1;
        node.name = "second";
        node.position = {5, 0, 0};
        model.nodes.push_back(node);
    }
    return assets::saveTo3DO(model);
}
} // namespace
void runSnapshotThingTests()
{
    using namespace render;
    SnapshotCamera camera;
    camera.position = {0, -10, 0};
    camera.target = {0, 0, 0};
    Level level;
    level.sectors.emplace_back();
    level.sectors[0].ambient = ColorWhite;
    Thing thing;
    thing.x = 10;
    thing.sector = 0;
    thing.values = {visual("model3d", "triangle.3do")};
    level.things = {thing, thing};
    std::map<std::string, std::string> resources{{"triangle.3do", triangle()},
                                                 {"normalized.3do", triangle(2.3)}};
    std::size_t reads{};
    auto resolver = [&](std::string_view key) -> std::optional<std::string_view>
    {
        ++reads;
        auto i = resources.find(std::string(key));
        if ( i == resources.end() )
            return {};
        return i->second;
    };
    // A BSP conversion can partition architecture into hundreds of independently
    // named, tiny meshes without approaching any geometry or byte budget.
    {
        Level district;
        district.sectors.emplace_back();
        const auto bytes = triangle();
        std::size_t distinctReads = 0;
        for ( int i = 0; i < 256; ++i )
        {
            auto part = thing;
            const auto name = "part" + std::to_string(i) + ".3do";
            part.values = {visual("model3d", name.c_str())};
            district.things.push_back(part);
        }
        const auto small = [&](std::string_view) -> std::optional<std::string_view>
        {
            ++distinctReads;
            return bytes;
        };
        checkThrowsWithMessage([&] { prepareSnapshotThings(district, camera, small); },
                               "snapshot Things distinct resource budget exceeded",
                               "snapshot Things: arbitrary resolver keeps its 128 resource cap");
        distinctReads = 0;
        const auto profile = SnapshotThingLimits::forIndexedArchives();
        const auto parts = prepareSnapshotThings(district, camera, small, profile);
        check(parts.models == 256 && distinctReads == 256, "snapshot Things: bounded district of 256 small meshes");
        for ( std::size_t i = district.things.size(); i <= profile.maxResources; ++i )
        {
            auto part = thing;
            const auto name = "part" + std::to_string(i) + ".3do";
            part.values = {visual("model3d", name.c_str())};
            district.things.push_back(part);
        }
        distinctReads = 0;
        checkThrowsWithMessage([&] { prepareSnapshotThings(district, camera, small, profile); },
                               "snapshot Things distinct resource budget exceeded",
                               "snapshot Things: indexed resolver remains hard bounded");
        check(distinctReads == profile.maxResources, "resource over limit is never fetched");
    }

    const auto scene = prepareSnapshotThings(level, camera, resolver);
    check(scene.models == 2 && scene.geometry.size() == 2 && scene.diagnostics.size() == 2,
          "snapshot Things: two real model instances and one diagnostic each");
    check(scene.diagnostics.size() == 2 && scene.diagnostics[0].status == "prepared",
          "snapshot Things: prepared does not imply screen visibility");
    check(reads == 1, "snapshot Things: shared model bytes resolved once");
    if ( !scene.geometry.empty() )
    {
        check(std::abs(scene.geometry[0].sector.vertices[0].x - 13) < 1e-8,
              "snapshot Things: hierarchy pivot and placement once, insertOffset not added");
        check(scene.geometry[0].uvCoordinateSpace == UvCoordinateSpace::TexturePixels,
              "snapshot Things: 2.1 keeps pixel UVs");
    }
    check(level.things[0].x == 10 && level.sectors[0].vertices.empty(), "snapshot Things: source unchanged");
    level.things.resize(1);
    level.things[0].values = {visual("model3d", "normalized.3do")};
    const auto normalized = prepareSnapshotThings(level, camera, resolver);
    check(!normalized.geometry.empty() &&
              normalized.geometry[0].uvCoordinateSpace == UvCoordinateSpace::Normalized,
          "snapshot Things: 2.3 keeps normalized UVs");
    level.things[0].values = {visual("model3d", "missing.3do")};
    const auto missing = prepareSnapshotThings(level, camera, resolver);
    check(missing.geometry.empty() && missing.diagnostics.size() == 1 &&
              missing.diagnostics[0].status == "missing-resource",
          "snapshot Things: missing resource explicit without marker");
    level.things[0].values.clear();
    const auto absent = prepareSnapshotThings(level, camera, resolver);
    check(absent.geometry.empty() && absent.diagnostics.size() == 1 &&
              absent.diagnostics[0].status == "no-visual",
          "snapshot Things: no visual explicit");
    ThingTemplate parent;
    parent.name = "parent";
    parent.values = {visual("model3d", "triangle.3do")};
    ThingTemplate child;
    child.name = "child";
    child.parent = "parent";
    level.templates = {parent, child};
    level.things[0].templateName = "child";
    check(prepareSnapshotThings(level, camera, resolver).models == 1,
          "snapshot Things: inherited template resource");
    {
        // A player start (effective type player, here inherited) is a spawn
        // point the game never draws from the player's view.
        Level starts = level;
        ThingTemplate walk;
        walk.name = "walk";
        walk.parent = "parent";
        walk.values = {visual("type", "Player")};
        starts.templates.push_back(walk);
        starts.things[0].templateName = "walk";
        const auto omitted = prepareSnapshotThings(starts, camera, resolver);
        check(omitted.models == 0 && omitted.geometry.empty() && omitted.diagnostics.size() == 1 &&
                  omitted.diagnostics[0].status == "omitted-player-start",
              "snapshot Things: a player start is omitted by default and diagnosed");
        SnapshotThingOptions show;
        show.drawPlayerStarts = true;
        check(prepareSnapshotThings(starts, camera, resolver, {}, show).models == 1,
              "snapshot Things: drawPlayerStarts draws a player start");
        starts.templates.back().values = {visual("type", "actor")};
        check(prepareSnapshotThings(starts, camera, resolver).models == 1,
              "snapshot Things: other Thing types are drawn");
    }
    level.things[0].values = {visual("model3d", "none")};
    check(prepareSnapshotThings(level, camera, resolver).models == 0,
          "snapshot Things: explicit none blocks inherited model");
    level.things[0].values = {visual("model3d", "triangle.3do")};
    SnapshotThingLimits limits;
    limits.maxVertices = 2;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded", "snapshot Things: vertex budget refuses");
    limits = {};
    limits.maxResourceBytes = 1;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded",
                           "snapshot Things: resource budget not swallowed");
    limits = {};
    limits.maxCachedModelBytes = 1;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded", "snapshot Things: cache budget refuses");
    assets::SprFile sprite;
    sprite.material = "test.mat";
    sprite.width = 2;
    sprite.height = 4;
    sprite.type = 2;
    sprite.geometryMode = 4;
    sprite.offsetXYZ = {1, 2, 3};
    resources["sprite.spr"] = assets::saveToSPRText(sprite, assets::SprProfile::classic);
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "sprite.spr")};
    const auto billboard = prepareSnapshotThings(level, camera, resolver);
    check(billboard.sprites == 1 && billboard.geometry.size() == 1,
          "snapshot Things: classic type2 is camera-facing sprite");
    if ( !billboard.geometry.empty() )
    {
        const auto& p = billboard.geometry[0].sector.vertices[0];
        check(std::abs(p.x - 10) < 1e-8 && std::abs(p.y - 2) < 1e-8 && std::abs(p.z - 1) < 1e-8,
              "snapshot Things: sprite camera basis and offset applied");
    }
    resources["zero.spr"] = "test.mat 0 0 4 4 0 0 0 0 0 0";
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "zero.spr")};
    const auto zeroSprite = prepareSnapshotThings(level, camera, resolver);
    check(zeroSprite.geometry.empty() && zeroSprite.diagnostics.size() == 1 &&
              zeroSprite.diagnostics[0].status == "invalid-resource",
          "snapshot Things: zero-width sprite is invalid");
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "sprite.spr")};
    resources["rgba.spr"] = assets::saveToSPRText(sprite, assets::SprProfile::ijim);
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "rgba.spr")};
    const auto rgbaSprite = prepareSnapshotThings(level, camera, resolver);
    check(rgbaSprite.geometry.empty() && rgbaSprite.diagnostics.size() == 1 &&
              rgbaSprite.diagnostics[0].status == "invalid-resource",
          "snapshot Things: IJIM SPR record not silently treated as classic");
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "sprite.spr")};
    level.things[0].rol = 90;
    const auto rolled = prepareSnapshotThings(level, camera, resolver);
    if ( !rolled.geometry.empty() )
    {
        const auto& p = rolled.geometry[0].sector.vertices[0];
        check(std::abs(p.x - 11) < 1e-8 && std::abs(p.z) < 1e-8,
              "snapshot Things: sprite offset rotates with roll");
    }
    level.things[0].rol = 0;
    resources["shared.3do"] = triangle(2.1, true);
    resources["hidden.3do"] = triangle(2.1, false, true);
    level.things[0].values = {visual("model3d", "shared.3do")};
    const auto shared = prepareSnapshotThings(level, camera, resolver);
    check(shared.geometry.size() == 2 && shared.models == 1,
          "snapshot Things: shared source mesh expands once per hierarchy node");
    level.things[0].values = {visual("model3d", "hidden.3do")};
    const auto hidden = prepareSnapshotThings(level, camera, resolver);
    check(hidden.geometry.empty() && hidden.models == 0 && hidden.diagnostics[0].status == "hidden-geometry",
          "snapshot Things: hidden mesh does not render or produce marker");
    level.things[0].values = {visual("model3d", "triangle.3do")};
    level.things[0].pch = 25;
    level.things[0].yaw = 90;
    level.things[0].rol = 15;
    const auto rotated = prepareSnapshotThings(level, camera, resolver);
    const auto expected = TMatrix43::Create({10, 0, 0}, {25, 90, 15}).TransformPoint({3, 0, 0});
    if ( !rotated.geometry.empty() )
    {
        const auto& p = rotated.geometry[0].sector.vertices[0];
        check(std::abs(p.x - expected.x) < 1e-8 && std::abs(p.y - expected.y) < 1e-8 &&
                  std::abs(p.z - expected.z) < 1e-8,
              "snapshot Things: full placement rotation matches core matrix");
    }
    level.things[0].pch = 0;
    level.things[0].yaw = 0;
    level.things[0].rol = 0;
    limits = {};
    limits.maxThings = 0;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded",
                           "snapshot Things: thing admission before reserve");
    limits = {};
    limits.maxCorners = 2;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded", "snapshot Things: aggregate corner budget");
    limits = {};
    limits.maxMeshes = 0;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded", "snapshot Things: instance mesh budget");
    level.things.push_back(level.things[0]);
    limits = {};
    limits.maxVertices = 5;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded",
                           "snapshot Things: instances charged even with cached model");
    level.things.resize(1);
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "sprite.spr")};
    level.things[0].sector = -1;
    const auto invalidSector = prepareSnapshotThings(level, camera, resolver);
    check(invalidSector.geometry.empty() && invalidSector.diagnostics.size() == 1 &&
              invalidSector.diagnostics[0].status == "invalid-sector",
          "snapshot Things: invalid sector explicitly skipped");
    level.things[0].sector = 0;
    level.things[0].values = {visual("model3d", "")};
    level.things[0].values[0].stringValue = std::string(256, 'a');
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver); },
                           "snapshot Things resource name exceeds 255 bytes",
                           "snapshot Things: oversized reference refuses before amplification");
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "sprite.spr")};
    limits = {};
    limits.maxTemplateLookupWork = 1;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded", "snapshot Things: tiny lookup work refuses");
    level.things[0].values.clear();
    level.things[0].templateName = "child";
    level.templates = {parent, child};
    auto duplicate = parent;
    duplicate.values = {visual("model3d", "missing.3do")};
    level.templates.push_back(duplicate);
    check(prepareSnapshotThings(level, camera, resolver).models == 1,
          "snapshot Things: duplicate template uses first raw case-insensitive match");
    level.templates[0].values.push_back(visual("model3d", "normalized.3do"));
    const auto lastField = prepareSnapshotThings(level, camera, resolver);
    check(lastField.models == 1 && lastField.geometry[0].uvCoordinateSpace == UvCoordinateSpace::Normalized,
          "snapshot Things: duplicate scalar uses last field");
    level.things[0].templateName = " child ";
    check(prepareSnapshotThings(level, camera, resolver).models == 0,
          "snapshot Things: lookup does not trim template names");
    level.things[0].templateName = "child";
    level.templates[1].parent = "missing";
    check(prepareSnapshotThings(level, camera, resolver).models == 0,
          "snapshot Things: missing parent stops lookup");
    level.templates[1].parent = "child";
    check(prepareSnapshotThings(level, camera, resolver).models == 0,
          "snapshot Things: cyclic ancestry retains bounded missing-field semantics");
    limits = {};
    limits.maxTemplateLookupWork = 100;
    checkThrowsWithMessage([&] { prepareSnapshotThings(level, camera, resolver, limits); },
                           "snapshot Things budget exceeded",
                           "snapshot Things: cyclic ancestry charged before resolver work");
    level.things[0].values = {visual("model3d", "none"), visual("sprite", "sprite.spr")};
    level.templates.clear();
    level.kind = ProjectType::IJIM;
    const auto unsupported = prepareSnapshotThings(level, camera, resolver);
    check(unsupported.geometry.empty() && unsupported.diagnostics.size() == 1 &&
              unsupported.diagnostics[0].status == "unsupported-sprite",
          "snapshot Things: IJIM explicitly unsupported");
}
} // namespace sed::tests
