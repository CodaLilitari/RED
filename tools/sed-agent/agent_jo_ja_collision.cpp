#include "agent_jo_ja.hpp"
#include "agent_workspace.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/archive_admission.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/runtime_archive.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>

namespace sed_agent
{
namespace
{
using namespace sed;
using namespace sed::assets;

std::size_t ordinal(std::string_view value)
{
    std::size_t parsed {};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc {} || end != value.data() + value.size())
        throw AgentError { kExitUsage, "Expected a nonnegative decimal index", "invalidCollisionSelection" };
    return parsed;
}

Json point(double x, double y, double z)
{
    auto row = Json::makeArray();
    for (const double value : { x, y, z })
        row.array.push_back(Json::makeNumber(value));
    return row;
}

std::vector<std::byte> member(const GobArchive& archive, const std::string& name)
{
    const auto entry = archive.find(name);
    if (!entry || entry->size > 32u * 1024u * 1024u)
        throw AgentError { kExitRefusal, "Missing or oversized captured entry: " + name,
            "collisionResourceUnavailable" };
    return archive.read(name);
}

Json sectorLayer(const Level& level, std::size_t sectorIndex, std::size_t offset)
{
    const auto& sector = level.sectors.at(sectorIndex);
    auto layer = Json::makeObject();
    layer.set("sector", Json::makeNumber(sectorIndex));
    layer.set("flags", Json::makeNumber(sector.flags));
    layer.set("surfaceCount", Json::makeNumber(sector.surfaces.size()));
    if (offset > sector.surfaces.size())
        throw AgentError { kExitUsage, "Surface offset exceeds selected sector", "invalidCollisionSelection" };
    auto rows = Json::makeArray();
    std::size_t i = offset, corners = 0;
    for (; i < sector.surfaces.size() && rows.array.size() < 128; ++i)
    {
        const auto& surface = sector.surfaces[i];
        if (surface.vertices.size() > 8192)
            throw AgentError { kExitRefusal, "Surface exceeds 8192 overlay corners", "collisionOverlayLimit" };
        if (corners + surface.vertices.size() > 8192)
            break;
        corners += surface.vertices.size();
        auto item = Json::makeObject();
        item.set("surface", Json::makeNumber(i));
        item.set("surfaceFlags", Json::makeNumber(static_cast<std::uint32_t>(surface.surfaceFlags)));
        item.set("adjoinFlags", Json::makeNumber(surface.adjoinFlags));
        item.set("geometryMode", Json::makeNumber(surface.geo));
        item.set("material", Json::makeString(surface.material));
        item.set("boundary", Json::makeString(surface.adjoin ? "portal" : "sectorExterior"));
        if (surface.adjoin)
        {
            auto neighbor = Json::makeObject();
            neighbor.set("sector", Json::makeNumber(surface.adjoin->sector));
            neighbor.set("surface", Json::makeNumber(surface.adjoin->surface));
            const auto& other = level.sectors.at(surface.adjoin->sector).surfaces.at(surface.adjoin->surface);
            neighbor.set("reciprocal",
                Json::makeBool(other.adjoin == SurfaceRef { static_cast<int>(sectorIndex), static_cast<int>(i) }));
            item.set("neighbor", std::move(neighbor));
        }
        auto polygon = Json::makeArray();
        for (const auto index : surface.vertices)
        {
            const auto& v = sector.vertices.at(index);
            polygon.array.push_back(point(v.x, v.y, v.z));
        }
        item.set("polygon", std::move(polygon));
        rows.array.push_back(std::move(item));
    }
    layer.set("surfaces", std::move(rows));
    layer.set("nextOffset", i < sector.surfaces.size() ? Json::makeNumber(i) : Json {});
    return layer;
}

Json thingLayer(const Level& level, const GobArchive& archive, const Thing& thing, std::size_t offset)
{
    auto layer = Json::makeObject();
    layer.set("name", Json::makeString(thing.name));
    layer.set("template", Json::makeString(thing.templateName));
    layer.set("position", point(thing.x, thing.y, thing.z));
    layer.set("pitchYawRoll", point(thing.pch, thing.yaw, thing.rol));
    auto fields = Json::makeObject();
    for (const auto* key : { "type", "move", "collide", "size", "movesize", "height", "model3d", "eyeoffset",
             "jumpspeed", "maxvel", "physflags", "typeflags", "thingflags" })
    {
        if (const auto* value = level::thingFindValue(level, thing, key))
            fields.set(key, Json::makeString(serializeTplValue(*value)));
    }
    const auto* model = fields.find("model3d");
    const auto* mode = fields.find("collide");
    std::string shape = "unresolved";
    if (mode && mode->str == "0")
        shape = "none";
    if (mode && mode->str == "1")
        shape = "sphere";
    if (mode && mode->str == "2")
        shape = "sphereTree";
    if (mode && mode->str == "3")
        shape = "faces";
    layer.set("shape", Json::makeString(shape));
    layer.set("profile", fields);
    if (!model || model->str.empty())
    {
        layer.set("modelGeometry", Json::makeString("noModelDeclared"));
        return layer;
    }
    const auto path = canonicalRuntimeGobEntryName("3do/" + model->str);
    if (!archive.contains(path))
    {
        layer.set("modelGeometry", Json::makeString("externalModelUnresolved"));
        return layer;
    }
    const auto bytes = member(archive, path);
    const auto parsed = loadFrom3DO(bytes);
    layer.set("modelSha256", Json::makeString(archive.entrySha256Hex(path)));
    layer.set("modelGeometry", Json::makeString("capturedRestPose"));
    layer.set("insertOffset", point(parsed.insertOffset.x, parsed.insertOffset.y, parsed.insertOffset.z));
    layer.set("modelRadius", Json::makeNumber(parsed.radius));
    // OpenJKDF2 sithPhysics_FindFloor uses template height, or the larger of
    // insertOffset.z and moveSize + .005. This contact search is not a climb limit.
    if (const auto* type = fields.find("type"); type && type->str == "player")
    {
        const auto* moveSize = fields.find("movesize");
        const auto* height = fields.find("height");
        const auto numeric = [](const std::string& text, double& value)
        {
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            return error == std::errc {} && end == text.data() + text.size() && std::isfinite(value);
        };
        double move = 0, h = 0;
        if (moveSize && numeric(moveSize->str, move) && (!height || numeric(height->str, h)))
        {
            const double base = h != 0 ? h : std::max(parsed.insertOffset.z, move + .005);
            layer.set("attachedFloorSearchHeight", Json::makeNumber(base));
        }
    }
    auto faces = Json::makeArray();
    const auto transform = TMatrix43::Create({ thing.x, thing.y, thing.z }, { thing.pch, thing.yaw, thing.rol });
    std::size_t total = 0, corners = 0;
    bool full = false;
    for (std::size_t meshIndex = 0; meshIndex < parsed.meshes.size(); ++meshIndex)
    {
        const auto& mesh = parsed.meshes[meshIndex];
        for (std::size_t faceIndex = 0; faceIndex < mesh.faces.size(); ++faceIndex, ++total)
        {
            if (total < offset || full)
                continue;
            const auto& face = mesh.faces[faceIndex];
            if (face.vertices.size() > 8192)
                throw AgentError { kExitRefusal, "Model face exceeds 8192 overlay corners",
                    "collisionOverlayLimit" };
            if (faces.array.size() == 128 || corners + face.vertices.size() > 8192)
            {
                full = true;
                continue;
            }
            corners += face.vertices.size();
            auto f = Json::makeObject();
            f.set("mesh", Json::makeNumber(meshIndex));
            f.set("face", Json::makeNumber(faceIndex));
            auto poly = Json::makeArray();
            for (const auto vi : face.vertices)
            {
                const auto& v = mesh.vertices.at(vi);
                const auto world = transform.TransformPoint({ v.x, v.y, v.z });
                poly.array.push_back(point(world.x, world.y, world.z));
            }
            f.set("polygon", std::move(poly));
            faces.array.push_back(std::move(f));
        }
    }
    if (offset > total)
        throw AgentError { kExitUsage, "Face offset exceeds selected model", "invalidCollisionSelection" };
    layer.set("nextOffset", full ? Json::makeNumber(offset + faces.array.size()) : Json {});
    layer.set("faceCount", Json::makeNumber(total));
    layer.set("faces", std::move(faces));
    return layer;
}
} // namespace

Json inspectTargetCollision(const std::vector<std::string>& args)
{
    if (args.size() < 5 || args.size() > 7)
        throw AgentError { kExitUsage,
            "jo-ja-inspect <episode.gob|goo> --target <jkl-entry> <sector> <thing|@template> [surface-offset "
            "[face-offset]]",
            "invalidCollisionSelection" };
    const auto sector = ordinal(args[3]);
    const auto surfaceOffset = args.size() >= 6 ? ordinal(args[5]) : 0;
    const auto faceOffset = args.size() >= 7 ? ordinal(args[6]) : 0;
    const auto capture = readFileBytes(args[0], kRuntimeArchiveMaxBytes);
    try
    {
        if (admitCapturedArchiveIndex(capture, kRuntimeArchiveMaxEntries).status
            != ArchiveIndexAdmissionStatus::Accepted)
            throw std::runtime_error("Malformed or oversized GOB directory");
        const auto* data = reinterpret_cast<const std::byte*>(capture.data());
        const auto archive = GobArchive::fromBytes({ data, data + capture.size() });
        std::set<std::string> entries;
        for (const auto& entry : archive.entries())
            if (!entries.insert(canonicalRuntimeGobEntryName(entry.name)).second)
                throw std::runtime_error("Duplicate physical archive entry");
        const auto name = canonicalRuntimeGobEntryName(args[2]);
        const auto payload = member(archive, name);
        if (payload.empty())
            throw std::runtime_error("Empty level entry");
        const std::string bytes(reinterpret_cast<const char*>(payload.data()), payload.size());
        std::istringstream input(bytes);
        const auto extension = std::filesystem::path(args[0]).extension().string();
        const auto projectType = text::iequalsAscii(extension, ".goo") ? ProjectType::MOTS : ProjectType::JKDF2;
        const auto level = loadFromJKL(input, { .projectType = projectType });
        if (sector >= level.sectors.size())
            throw AgentError { kExitUsage, "Unknown target sector index", "invalidCollisionSelection" };
        Thing selection;
        const bool templateOnly = args[4].starts_with("@");
        if (templateOnly)
        {
            selection.templateName = args[4].substr(1);
            if (!std::any_of(level.templates.begin(), level.templates.end(),
                    [&](const ThingTemplate& t) { return t.name == selection.templateName; }))
                throw AgentError { kExitUsage, "Unknown target template", "invalidCollisionSelection" };
            selection.name = selection.templateName;
        }
        else
        {
            const auto i = ordinal(args[4]);
            if (i >= level.things.size())
                throw AgentError { kExitUsage, "Unknown target Thing index", "invalidCollisionSelection" };
            selection = level.things[i];
        }
        auto result = Json::makeObject();
        result.set("command", Json::makeString("jo-ja-inspect"));
        result.set("archiveSha256", Json::makeString(sha256Hex(capture)));
        result.set("levelSha256", Json::makeString(sha256Hex(bytes)));
        result.set("levelEntry", Json::makeString(name));
        result.set("runtimeVerified", Json::makeBool(false));
        result.set("templateOnly", Json::makeBool(templateOnly));
        result.set("native", sectorLayer(level, sector, surfaceOffset));
        result.set("model", thingLayer(level, archive, selection, faceOffset));
        return result;
    }
    catch (const std::exception& error)
    {
        throw AgentError { kExitInput, error.what(), "invalidCollisionCapture" };
    }
}
} // namespace sed_agent
