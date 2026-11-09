#include "agent_query_details.hpp"

#include "agent_workspace.hpp"
#include "sed/level.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace sed_agent
{
namespace
{
[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "queryDetailsInvalid" };
}

Json number(const double value)
{
    return std::isfinite(value) ? Json::makeNumber(value) : Json{};
}

Json xyz(const double x, const double y, const double z)
{
    Json result = Json::makeArray();
    result.array = { number(x), number(y), number(z) };
    return result;
}

Json optionalNumber(const std::optional<double>& value)
{
    return value ? number(*value) : Json{};
}

std::size_t integer(const Json& request, const char* name, const std::size_t minimum,
    const std::optional<std::size_t> defaultValue = std::nullopt)
{
    const auto* value = request.find(name);
    if ( !value && defaultValue )
    {
        return *defaultValue;
    }
    if ( !value || value->type != Json::Type::Number || !std::isfinite(value->number)
        || value->number < static_cast<double>(minimum)
        || value->number > static_cast<double>(std::numeric_limits<std::int32_t>::max())
        || std::floor(value->number) != value->number )
    {
        invalid(std::string(name) + " must be an integer in the supported range");
    }
    return static_cast<std::size_t>(value->number);
}

const char* valueType(const sed::ValueType type)
{
    switch ( type )
    {
    case sed::ValueType::str: return "string";
    case sed::ValueType::integer: return "integer";
    case sed::ValueType::floating: return "floating";
    case sed::ValueType::flag: return "flags";
    case sed::ValueType::ptr: return "reference";
    case sed::ValueType::vect: return "vector";
    case sed::ValueType::frame: return "frame";
    default: return "unknown";
    }
}

Json describeVertex(const sed::Sector& sector, const std::size_t sectorIndex, const std::size_t vertexIndex)
{
    const auto& vertex = sector.vertices[vertexIndex];
    Json result = Json::makeObject();
    result.set("index", number(static_cast<double>(vertexIndex)));
    result.set("sector", number(static_cast<double>(sectorIndex)));
    result.set("position", xyz(vertex.x, vertex.y, vertex.z));
    return result;
}

Json describeSurface(const sed::Level& level, const std::size_t sectorIndex, const std::size_t surfaceIndex)
{
    const auto& sector = level.sectors[sectorIndex];
    const auto& surface = sector.surfaces[surfaceIndex];
    Json result = Json::makeObject();
    result.set("index", number(static_cast<double>(surfaceIndex)));
    result.set("sector", number(static_cast<double>(sectorIndex)));
    result.set("num", number(surface.num));
    result.set("material", Json::makeString(surface.material));
    result.set("materialIndex", number(surface.nMat));
    result.set("geometryMode", number(surface.geo));
    result.set("lightMode", number(surface.light));
    result.set("textureMode", number(surface.tex));
    result.set("surfaceFlags", number(static_cast<std::uint32_t>(surface.surfaceFlags)));
    result.set("faceFlags", number(surface.faceFlags));
    result.set("adjoinFlags", number(static_cast<std::uint32_t>(surface.adjoinFlags)));
    result.set("normal", xyz(surface.normal.x, surface.normal.y, surface.normal.z));
    const char* space = "unknown";
    if ( level.uvCoordinateSpace == sed::UvCoordinateSpace::TexturePixels )
    {
        space = "texturePixels";
    }
    else if ( level.uvCoordinateSpace == sed::UvCoordinateSpace::Normalized )
    {
        space = "normalized";
    }
    result.set("uvCoordinateSpace", Json::makeString(space));
    Json adjoin;
    if ( surface.adjoin )
    {
        adjoin = Json::makeObject();
        adjoin.set("sector", number(surface.adjoin->sector));
        adjoin.set("surface", number(surface.adjoin->surface));
    }
    result.set("adjoin", std::move(adjoin));
    Json corners = Json::makeArray();
    bool valid = surface.vertices.size() >= 3 && surface.texVertices.size() == surface.vertices.size();
    for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
    {
        Json corner = Json::makeObject();
        corner.set("index", number(static_cast<double>(i)));
        corner.set("vertex", number(surface.vertices[i]));
        Json position;
        if ( surface.vertices[i] >= 0 && static_cast<std::size_t>(surface.vertices[i]) < sector.vertices.size() )
        {
            const auto& vertex = sector.vertices[surface.vertices[i]];
            position = xyz(vertex.x, vertex.y, vertex.z);
            valid = valid && std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z);
        }
        else
        {
            valid = false;
        }
        corner.set("position", std::move(position));
        if ( i < surface.texVertices.size() )
        {
            const auto& tex = surface.texVertices[i];
            Json uv = Json::makeArray();
            uv.array = { number(tex.u), number(tex.v) };
            corner.set("uv", std::move(uv));
            corner.set("mono", number(tex.color.a));
            corner.set("rgb", xyz(tex.color.r, tex.color.g, tex.color.b));
            valid = valid && std::isfinite(tex.u) && std::isfinite(tex.v) && std::isfinite(tex.color.a)
                && std::isfinite(tex.color.r) && std::isfinite(tex.color.g) && std::isfinite(tex.color.b);
        }
        else
        {
            corner.set("uv", Json{});
            corner.set("mono", Json{});
            corner.set("rgb", Json{});
        }
        corners.array.push_back(std::move(corner));
    }
    result.set("corners", std::move(corners));
    result.set("cornerRecordsValid", Json::makeBool(valid));
    return result;
}
} // namespace

Json queryGeometryDetails(const sed::Level& level, const Json& request)
{
    const auto* collection = request.find("collection");
    if ( !collection || collection->type != Json::Type::String
        || (collection->str != "surfaces" && collection->str != "vertices") )
    {
        invalid("Geometry details require collection surfaces or vertices");
    }
    const auto sectorIndex = integer(request, "sector", 0);
    if ( sectorIndex >= level.sectors.size() )
    {
        invalid("sector is outside the current document");
    }
    const auto offset = integer(request, "offset", 0, 0);
    const auto limit = std::min<std::size_t>(200, integer(request, "limit", 1, 50));
    const auto& sector = level.sectors[sectorIndex];
    const bool surfaces = collection->str == "surfaces";
    const std::size_t total = surfaces ? sector.surfaces.size() : sector.vertices.size();
    const auto end = offset >= total ? total : offset + std::min(limit, total - offset);
    Json items = Json::makeArray();
    std::size_t cornerCount = 0;
    for ( std::size_t i = offset; i < end; ++i )
    {
        if ( surfaces )
        {
            constexpr std::size_t kMaxPageCorners = 4096;
            if ( sector.surfaces[i].vertices.size() > kMaxPageCorners - cornerCount )
            {
                throw AgentError{ kExitRefusal,
                    "Surface detail page exceeds 4096 corners; request a smaller page",
                    "queryDetailsTooLarge" };
            }
            cornerCount += sector.surfaces[i].vertices.size();
            items.array.push_back(describeSurface(level, sectorIndex, i));
        }
        else
        {
            items.array.push_back(describeVertex(sector, sectorIndex, i));
        }
    }
    Json result = Json::makeObject();
    result.set("ok", Json::makeBool(true));
    result.set("command", Json::makeString("query"));
    result.set("items", std::move(items));
    result.set("total", number(static_cast<double>(total)));
    result.set("offset", number(static_cast<double>(offset)));
    result.set("nextOffset", end < total ? number(static_cast<double>(end)) : Json{});
    return result;
}

Json describeThingDetails(const sed::Level& level, const std::size_t index)
{
    if ( index >= level.things.size() )
    {
        invalid("Thing detail index is outside the current document");
    }
    const auto& thing = level.things[index];
    Json result = Json::makeObject();
    result.set("index", number(static_cast<double>(index)));
    result.set("num", number(thing.num));
    result.set("id", number(thing.id));
    result.set("name", Json::makeString(thing.name));
    result.set("template", Json::makeString(thing.templateName));
    result.set("sector", number(thing.sector));
    result.set("position", xyz(thing.x, thing.y, thing.z));
    result.set("orientation", xyz(thing.pch, thing.yaw, thing.rol));
    result.set("layer", number(thing.layer));
    result.set("archLightingIndex", number(thing.archLightingIndex));
    result.set("parameterScope", Json::makeString("instance"));
    Json parameters = Json::makeArray();
    for ( std::size_t i = 0; i < thing.values.size(); ++i )
    {
        const auto& value = thing.values[i];
        Json record = Json::makeObject();
        record.set("index", number(static_cast<double>(i)));
        record.set("name", Json::makeString(value.name));
        record.set("value", Json::makeString(value.asString()));
        record.set("type", Json::makeString(valueType(value.type)));
        parameters.array.push_back(std::move(record));
    }
    result.set("sourceParameters", std::move(parameters));
    return result;
}

Json describeLightDetails(const sed::Level& level, const std::size_t index)
{
    if ( index >= level.lights.size() )
    {
        invalid("Light detail index is outside the current document");
    }
    const auto& light = level.lights[index];
    Json result = Json::makeObject();
    result.set("index", number(static_cast<double>(index)));
    result.set("id", number(light.id));
    result.set("position", xyz(light.position.x, light.position.y, light.position.z));
    result.set("rgb", xyz(light.color.r, light.color.g, light.color.b));
    result.set("intensity", number(light.intensity));
    result.set("range", number(light.range));
    result.set("rgbIntensity", optionalNumber(light.rgbIntensity));
    result.set("effectiveRgbIntensity", number(light.effectiveRgbIntensity()));
    result.set("rgbIntensityInherited", Json::makeBool(!light.rgbIntensity.has_value()));
    result.set("legacyRgbRange", optionalNumber(light.legacyRgbRange));
    result.set("flags", number(static_cast<std::uint32_t>(light.flags)));
    result.set("layer", number(light.layer));
    return result;
}
} // namespace sed_agent
