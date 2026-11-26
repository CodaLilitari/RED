#include "sed/level/panel_switch.hpp"

#include "sed/assets/cog_compiler.hpp"
#include "sed/format_number.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>

namespace sed::level
{
namespace
{
constexpr std::string_view kButtonTemplate = "red_panelbutton";

PanelSwitchPlacement denied(std::string code, std::string reason)
{
    PanelSwitchPlacement result;
    result.failure = WorkbenchFailure{ std::move(code), std::move(reason) };
    return result;
}

std::string field(const Level& level, const assets::Templates* templates, const Thing& thing, std::string_view key)
{
    const auto* value = thingFindValue(level, templates, thing, key);
    return value ? value->asString() : std::string{};
}

ThingTemplate sphereTemplate()
{
    ThingTemplate tpl;
    tpl.name = kButtonTemplate;
    tpl.parent = "none";
    tpl.values = { deserializeTplValue("type","cog"), deserializeTplValue("collide","1"),
        deserializeTplValue("thingflags","0x400") };
    return tpl;
}
} // namespace

std::string panelOwnerModel(const Level& level, const assets::Templates* templates, std::int32_t owner)
{
    if ( owner < 0 || static_cast<std::size_t>(owner) >= level.things.size() ) return {};
    const Thing& thing = level.things[owner];
    const auto name = field(level, templates, thing, "model3d");
    return name.empty() ? field(level, templates, thing, "model3do") : name;
}

PanelSwitchPlacement locatePanelSwitch(const Level& level, const assets::Templates* templates,
    const assets::Model3do& model, const PanelSwitchRequest& r)
{
    if ( level.kind != ProjectType::JKDF2 && level.kind != ProjectType::MOTS )
        return denied("panelGame", "Panel switches require a JK or MotS level.");
    if ( panelOwnerModel(level, templates, r.owner).empty() )
        return denied("panelOwner", "Choose a placed architecture Thing with a 3DO model.");
    const auto& owner = level.things[r.owner];
    const auto move = field(level, templates, owner, "move");
    if ( field(level, templates, owner, "collide") != "3" || (!move.empty() && !text::iequalsAscii(move,"none"))
            || thingCountFrames(owner) != 0 )
        return denied("panelOwner", "The model must be a stationary Thing with face collision (collide=3).");
    if ( owner.sector < 0 || static_cast<std::size_t>(owner.sector) >= level.sectors.size() )
        return denied("panelSector", "The architecture must belong to a sector.");
    if ( r.mesh >= model.meshes.size() ) return denied("panelFace", "Unknown collision mesh.");
    const auto& mesh = model.meshes[r.mesh];
    if ( r.face >= mesh.faces.size() || r.face >= mesh.faceNormals.size() )
        return denied("panelFace", "Unknown physical face or missing face normal.");
    const auto& indices = mesh.faces[r.face].vertices;
    if ( indices.size() < 3 || indices.size() > assets::kModel3doMaxFaceVertices )
        return denied("panelFace", "The panel face must have 3 to 24 corners.");
    if ( !std::isfinite(r.radius) || r.radius < 0.005 || r.radius > 0.1 || !std::isfinite(r.u) || !std::isfinite(r.v) )
        return denied("panelSize", "Use finite offsets and a button radius from 0.005 to 0.1 JKU.");
    PanelSwitchPlacement plan;
    const auto pose = TMatrix43::Create({ owner.x, owner.y, owner.z }, { owner.pch, owner.yaw, owner.rol });
    TVector centroid{};
    for ( const auto index : indices )
    {
        if ( index < 0 || static_cast<std::size_t>(index) >= mesh.vertices.size() )
            return denied("panelFace", "A face corner is missing.");
        const auto& v = mesh.vertices[index];
        auto p = pose.TransformPoint({v.x,v.y,v.z});
        if ( !std::isfinite(p.Length()) ) return denied("panelFace", "The face pose has a nonfinite corner.");
        plan.boundary.push_back(p);
        centroid = centroid + p;
    }
    centroid = centroid * (1.0 / static_cast<double>(indices.size()));
    TVector normal = pose.TransformVector(mesh.faceNormals[r.face]);
    const double length = normal.Normalize();
    if ( !std::isfinite(length) || length < 1e-8 )
        return denied("panelFace", "The collision face has no usable outward normal.");
    plan.uAxis = (plan.boundary[1] - plan.boundary[0]).Normalized();
    plan.vAxis = normal.Cross(plan.uAxis).Normalized();
    plan.anchor = centroid + r.u * plan.uAxis + r.v * plan.vAxis;
    for ( std::size_t i = 0; i < indices.size(); ++i )
    {
        const auto& p = plan.boundary[i];
        const auto edge = plan.boundary[(i+1) % indices.size()] - p;
        if ( edge.Length() < 1e-8 || std::abs((p-centroid).Dot(normal)) > 1e-5 )
            return denied("panelFace", "Choose a planar face with distinct corners.");
        auto inside = normal.Cross(edge).Normalized();
        if ( (centroid-p).Dot(inside) < 0.0 ) inside = -inside;
        for ( const auto& vertex : plan.boundary )
            if ( (vertex-p).Dot(inside) < -1e-6 )
                return denied("panelFace", "Choose a convex collision face.");
        if ( (plan.anchor-p).Dot(inside) < r.radius )
            return denied("panelFootprint", "The button must fit within every edge of the selected face.");
    }
    plan.center = plan.anchor + normal * (r.radius + 0.001);
    const auto& s = level.sectors[owner.sector];
    const auto& p = plan.center;
    const double d = r.radius;
    if ( s.surfaces.empty() || !sectorContainsBox(s, {p.x-d,p.y-d,p.z-d,p.x+d,p.y+d,p.z+d}) )
        return denied("panelSector", "The button collision volume must fit inside the architecture's sector.");
    return plan;
}

PanelSwitchPlacement placePanelSwitch(Level& level, const assets::Templates* templates, const assets::Model3do& model,
    const PanelSwitchRequest& r, const CogSchemaResolver& resolve)
{
    auto plan = locatePanelSwitch(level, templates, model, r);
    if ( !plan.ok() ) return plan;
    if ( r.cog < 0 || static_cast<std::size_t>(r.cog) >= level.cogs.size() || !resolve )
        return denied("panelHandler", "Choose a placed COG with an activated handler and an unused Thing symbol.");
    if ( const auto problem = validateThingAppend(level, resolve) ) return denied(problem->code, problem->message);
    const auto& cog = level.cogs[r.cog];
    const auto schema = resolve(cog.name);
    if ( !schema || !schema->isComplete() ) return denied("panelHandler", "The selected COG schema is unavailable.");
    const auto compiled = assets::compileCog(cog.name, schema->sourceText, level.kind == ProjectType::MOTS
        ? assets::CogPreflightTarget::mots : assets::CogPreflightTarget::jk);
    if ( !compiled.loads || std::ranges::none_of(compiled.handlers, [](const auto& h) { return h.message == "activated" && h.address >= 0; }) )
        return denied("panelHandler", "The selected script needs an executable activated handler.");
    const auto symbols = schema->levelValues();
    const auto target = std::ranges::find_if(symbols, [&](const auto* row) { return text::iequalsAscii(row->name,r.symbol); });
    if ( target == symbols.end() || (*target)->kind != assets::CogSchemaSymbolKind::thing
            || (*target)->effectiveLinkId < 0 || !((*target)->effectiveMask & 0x400u) )
        return denied("panelLink", "Choose a linked Thing parameter that admits activation from the player.");
    const auto parameter = static_cast<std::size_t>(target-symbols.begin());
    if ( parameter >= cog.values.size() || cog.values[parameter].asJedString() != "-1" )
        return denied("panelLinkOccupied", "The target Thing parameter must be unbound (-1).");
    if ( !level.canAllocateIds(1) )
        return denied("panelCapacity", "The level has no free entity ID for the button.");
    const auto stock = sphereTemplate();
    Level candidate = level;
    auto found = std::ranges::find_if(candidate.templates, [&](const auto& tpl) { return text::iequalsAscii(tpl.name,stock.name); });
    if ( found == candidate.templates.end() ) candidate.templates.push_back(stock);
    else
    {
        if ( !text::iequalsAscii(found->parent,stock.parent) || found->values.size() != stock.values.size() )
            return denied("panelTemplateConflict", "An existing red_panelbutton template has different settings.");
        for ( std::size_t i = 0; i < stock.values.size(); ++i )
            if ( !text::iequalsAscii(found->values[i].name,stock.values[i].name) || found->values[i].asString() != stock.values[i].asString() )
                return denied("panelTemplateConflict", "An existing red_panelbutton template has different settings.");
    }
    const auto sector = level.things[r.owner].sector;
    const auto layer = level.things[r.owner].layer;
    plan.button = static_cast<std::int32_t>(candidate.things.size());
    auto& button = candidate.newThing();
    button.templateName = stock.name; button.name = stock.name;
    button.x = plan.center.x; button.y = plan.center.y; button.z = plan.center.z;
    button.sector = sector; button.layer = layer;
    button.values = { deserializeTplValue("size",format::authoredDouble(r.radius)),
        deserializeTplValue("movesize",format::authoredDouble(r.radius)) };
    const auto bind = prepareRebindCogReference(candidate,r.cog,static_cast<std::int32_t>(parameter),r.symbol,
        assets::CogSchemaSymbolKind::thing,plan.button,&*schema);
    if ( !bind.ok() ) return denied(bind.failure->code,bind.failure->message);
    if ( !bind.apply(candidate) ) return denied("panelStale", "The controller changed before placement.");
    level = std::move(candidate);
    return plan;
}
} // namespace sed::level
