#include "test_common.hpp"
#include "sed/assets/cog_script.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/panel_switch.hpp"
#include "sed/level/thing.hpp"

using namespace sed;

int main()
{
    using sed::tests::check;
    Level level;
    Sector room;
    for ( const TVector p : { TVector{-2,-2,-2}, {2,-2,-2}, {2,2,-2}, {-2,2,-2},
             {-2,-2,2}, {2,-2,2}, {2,2,2}, {-2,2,2} } )
        room.vertices.push_back({ p.x, p.y, p.z, 0 });
    for ( const std::vector<std::int32_t> indices : std::vector<std::vector<std::int32_t>>{
            {0,1,2,3}, {7,6,5,4}, {0,4,5,1}, {1,5,6,2}, {2,6,7,3}, {3,7,4,0} } )
    {
        Surface face; face.vertices = indices; room.surfaces.push_back(face);
    }
    level.sectors.push_back(room);
    ThingTemplate wall;
    wall.name = "wall"; wall.parent = "none";
    wall.values = { deserializeTplValue("type","cog"), deserializeTplValue("collide","3"),
        deserializeTplValue("model3d","panelwall.3do") };
    level.templates.push_back(wall);
    auto& thing = level.newThing(); thing.templateName = "wall"; thing.sector = 0;
    thing.x = 0.25; thing.z = 0.5;
    const std::string script = "symbols\nmessage activated\nthing button=-1 linkid=1\nend\ncode\nactivated:\n PrintInt(GetSenderRef());\n return;\nend\n";
    auto& cog = level.newCog(); cog.name = "usepanel.cog";
    cog.values = assets::loadCogSymbolsFromTextResult(script,false).symbols;
    const auto schema = assets::parseCogSchema(cog.name, script);
    const auto resolve = [&](std::string_view) { return std::optional(schema); };
    assets::Model3do model;
    auto& mesh = model.meshes.emplace_back();
    mesh.vertices = { {-0.6,0,-0.6}, {0.6,0,-0.6}, {0.6,0,0.6}, {-0.6,0,0.6} };
    mesh.faces.emplace_back().vertices = {0,1,2,3};
    mesh.faceNormals.push_back({0,-1,0});
    level::PanelSwitchRequest r;
    r.owner = 0; r.cog = 0; r.symbol = "button"; r.radius = 0.025; r.u = 0.1;
    auto plan = level::locatePanelSwitch(level, nullptr, model, r);
    check(plan.ok() && std::abs(plan.center.x-0.35) < 1e-8 && plan.center.y < -0.025
            && std::abs(plan.center.z-0.5) < 1e-8, "panel anchor is offset from the chosen native face and sits clear of it");
    const auto before = level;
    r.u = 0.591;
    check(!level::placePanelSwitch(level, nullptr, model, r, resolve).ok() && assets::sedLevelsEqual(level,before),
        "button sphere cannot stick out over the panel edge");
    r.u = 0.1;
    check(level::placePanelSwitch(level, nullptr, model, r, resolve).ok() && level.things.size() == 2
            && level.cogs[0].values[0].asJedString() == "1", "placement binds a separate sphere Thing to the existing handler");
    check(level.things[0].templateName == "wall" && level.things[0].values.empty()
            && level::thingFindValue(level, level.things[0], "collide")->asString() == "3"
            && level::thingFindValue(level, level.things[1], "collide")->asString() == "1",
        "wall retains face collision while the new button uses sphere collision");
    const auto applied = level;
    check(!level::placePanelSwitch(level, nullptr, model, r, resolve).ok() && assets::sedLevelsEqual(level,applied),
        "an occupied activation parameter cannot be replaced");
    return sed::tests::failures != 0;
}
