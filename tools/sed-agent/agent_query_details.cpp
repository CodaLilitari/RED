#include "agent_query_details.hpp"

#include "agent_workspace.hpp"
#include "sed/level.hpp"
#include "sed/level/arch_authoring.hpp"
#include "sed/level/render_budget.hpp"
#include "sed/level/spatial_selection.hpp"
#include "sed/level/thing_census.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    // The face's own light, as setSurfaceProperties writes it and the JKL stores it: the
    // classic scalar is the RGB mean (what a JK level writes), and the channels as stored.
    result.set("extraLight", number((static_cast<double>(surface.extraLight.r) + surface.extraLight.g
        + surface.extraLight.b) / 3.0));
    result.set("extraLightRgb", xyz(surface.extraLight.r, surface.extraLight.g, surface.extraLight.b));
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

namespace
{
constexpr std::size_t kDetailTextBudget = 1024 * 1024;
constexpr std::size_t kArchitectureBindingWorkBudget = 1'000'000;
constexpr std::size_t kFilterWorkBudget = 8 * 1024 * 1024;

void consumeRecords(std::size_t& remaining, const std::size_t count)
{
    if ( count > remaining )
    {
        throw AgentError{ kExitRefusal,
            "Detail page exceeds its record/work budget; request a smaller or more focused page",
            "queryDetailsTooLarge" };
    }
    remaining -= count;
}

void consumeText(std::size_t& remaining, const std::size_t bytes, const std::size_t copies = 1)
{
    for ( std::size_t copy = 0; copy < copies; ++copy )
    {
        consumeRecords(remaining, bytes);
    }
}

void consumeProduct(std::size_t& remaining, const std::size_t first, const std::size_t second)
{
    if ( first && second > remaining / first )
    {
        consumeRecords(remaining, remaining + 1);
    }
    consumeRecords(remaining, first * second);
}

std::size_t parameterTextAllowance(const sed::Value& value)
{
    // Check retained text before Value::asString can allocate its copy. Scalar
    // formatting is bounded independently of an unused preferred/raw string.
    switch ( value.type )
    {
    case sed::ValueType::str:
    case sed::ValueType::unknown:
    case sed::ValueType::vect:
    case sed::ValueType::frame:
        return value.stringValue.size();
    case sed::ValueType::integer:
    case sed::ValueType::ptr:
    case sed::ValueType::flag:
    case sed::ValueType::floating:
        return 128;
    default:
        return 0;
    }
}

template <typename Value>
void admitParameterText(const std::vector<Value>& values, std::size_t& text,
    std::size_t& records, const std::size_t copies)
{
    consumeRecords(records, values.size());
    for ( const auto& value : values )
    {
        consumeText(text, value.name.size());
        consumeText(text, parameterTextAllowance(value), copies);
    }
}

void admitPageText(const sed::Level& level, const std::string& collection,
    const std::vector<std::size_t>& selected)
{
    std::size_t text = kDetailTextBudget;
    std::size_t records = 4096;
    for ( const auto index : selected )
    {
        if ( collection == "sectors" )
        {
            const auto& sector = level.sectors[index];
            consumeText(text, sector.sound.size());
            consumeText(text, sector.colormap.size());
        }
        else if ( collection == "templates" )
        {
            const auto& item = level.templates[index];
            consumeText(text, item.name.size());
            consumeText(text, item.parent.size());
            admitParameterText(item.values, text, records, 1);
        }
        else if ( collection == "cogs" )
        {
            const auto& cog = level.cogs[index];
            consumeText(text, cog.name.size());
            // Both sourceParameters and the legacy values array emit a full
            // copy. They share one page budget rather than doubling it.
            admitParameterText(cog.values, text, records, 2);
        }
        else if ( collection == "thingContainment" )
        {
            const auto& thing = level.things[index];
            consumeText(text, thing.name.size());
            consumeText(text, thing.templateName.size());
        }
    }
}

class AsciiSubstringMatcher
{
public:
    AsciiSubstringMatcher(const std::string_view pattern, std::size_t& remaining)
        : pattern_(pattern)
    {
        consumeRecords(remaining, pattern.size());
        fallback_.resize(pattern.size());
        for ( std::size_t i = 1, matched = 0; i < pattern.size(); ++i )
        {
            const auto next = fold(pattern[i]);
            while ( matched && next != fold(pattern[matched]) )
            {
                matched = fallback_[matched - 1];
            }
            if ( next == fold(pattern[matched]) )
            {
                ++matched;
            }
            fallback_[i] = matched;
        }
    }

    bool matches(const std::string_view text, std::size_t& remaining) const
    {
        if ( pattern_.empty() )
        {
            return true;
        }
        // Charge the candidate's full size before scanning, even on an early
        // match. Unmatched/empty pages cannot evade this cumulative allowance.
        consumeRecords(remaining, text.size());
        std::size_t matched = 0;
        for ( const auto character : text )
        {
            const auto next = fold(character);
            while ( matched && next != fold(pattern_[matched]) )
            {
                matched = fallback_[matched - 1];
            }
            if ( next == fold(pattern_[matched]) )
            {
                ++matched;
            }
            if ( matched == pattern_.size() )
            {
                return true;
            }
        }
        return false;
    }

private:
    static unsigned char fold(const unsigned char character)
    {
        return character >= 'A' && character <= 'Z'
            ? static_cast<unsigned char>(character + ('a' - 'A')) : character;
    }

    std::string_view pattern_;
    std::vector<std::size_t> fallback_;
};

template <typename Value>
Json parameterRows(const std::vector<Value>& values, std::size_t& remaining)
{
    consumeRecords(remaining, values.size());
    Json rows = Json::makeArray();
    for ( std::size_t i = 0; i < values.size(); ++i )
    {
        const auto& value = values[i];
        Json row = Json::makeObject();
        row.set("index", number(static_cast<double>(i)));
        row.set("name", Json::makeString(value.name));
        row.set("type", Json::makeString(valueType(value.type)));
        row.set("value", Json::makeString(value.asString()));
        rows.array.push_back(std::move(row));
    }
    return rows;
}

Json describeSectorRecord(const sed::Level& level, const std::size_t index, std::size_t& remaining)
{
    const auto& sector = level.sectors[index];
    consumeRecords(remaining, sector.vertices.size());
    consumeRecords(remaining, sector.surfaces.size());
    Json row = Json::makeObject();
    row.set("index", number(static_cast<double>(index)));
    row.set("num", number(sector.num));
    row.set("id", number(sector.id));
    // Preserve the legacy query field's signed representation, including the
    // high bit; richer readback must not silently change an existing value.
    row.set("flags", number(sector.flags));
    row.set("layer", number(sector.layer));
    row.set("ambient", number(sector.ambient.r));
    row.set("ambientRgb", xyz(sector.ambient.r, sector.ambient.g, sector.ambient.b));
    row.set("extraLight", number(sector.extraLight.r));
    row.set("extraLightRgb", xyz(sector.extraLight.r, sector.extraLight.g, sector.extraLight.b));
    row.set("tint", xyz(sector.tint.r, sector.tint.g, sector.tint.b));
    row.set("sound", Json::makeString(sector.sound));
    row.set("soundVolume", number(sector.soundVolume));
    row.set("thrust", xyz(sector.thrust.x, sector.thrust.y, sector.thrust.z));
    row.set("vertices", number(static_cast<double>(sector.vertices.size())));
    row.set("surfaces", number(static_cast<double>(sector.surfaces.size())));
    row.set("colormap", Json::makeString(sector.colormap));
    row.set("colormapIndex", number(sector.colormapIndex));
    bool finite = !sector.vertices.empty();
    sed::TVector low{}, high{};
    for ( std::size_t v = 0; v < sector.vertices.size(); ++v )
    {
        const auto& vertex = sector.vertices[v];
        finite = finite && std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z);
        if ( v == 0 )
        {
            low = high = { vertex.x, vertex.y, vertex.z };
        }
        else
        {
            low = { std::min(low.x, vertex.x), std::min(low.y, vertex.y), std::min(low.z, vertex.z) };
            high = { std::max(high.x, vertex.x), std::max(high.y, vertex.y), std::max(high.z, vertex.z) };
        }
    }
    Json bounds;
    if ( finite )
    {
        bounds = Json::makeObject();
        bounds.set("min", xyz(low.x, low.y, low.z));
        bounds.set("max", xyz(high.x, high.y, high.z));
    }
    row.set("geometryBounds", std::move(bounds));
    std::set<std::int32_t> neighbors;
    bool valid = true;
    for ( const auto& surface : sector.surfaces )
    {
        if ( !surface.adjoin )
        {
            continue;
        }
        const auto ref = *surface.adjoin;
        if ( ref.sector < 0 || static_cast<std::size_t>(ref.sector) >= level.sectors.size()
            || ref.surface < 0 || static_cast<std::size_t>(ref.surface) >= level.sectors[ref.sector].surfaces.size() )
        {
            valid = false;
            continue;
        }
        neighbors.insert(ref.sector);
    }
    Json adjacent = Json::makeArray();
    for ( const auto neighbor : neighbors )
    {
        adjacent.array.push_back(number(neighbor));
    }
    row.set("neighbors", std::move(adjacent));
    row.set("neighborReferencesValid", Json::makeBool(valid));
    return row;
}

Json describeContainment(const sed::Level& level, const std::size_t index,
    const std::string& geometryProblem)
{
    const auto& thing = level.things[index];
    const sed::TVector point{ thing.x, thing.y, thing.z };
    sed::level::SpatialSelection selected;
    if ( geometryProblem.empty() )
    {
        selected = sed::level::selectSectorAtPoint(level, {}, point);
    }
    else
    {
        // A NaN in a Sector's bounds can make every AABB comparison false.
        // Such a sector cannot prove that the point lies outside its volume.
        selected.match = sed::level::SpatialMatch::cannotClassify;
        selected.diagnostic = geometryProblem;
    }
    const bool unique = selected.match == sed::level::SpatialMatch::unique;
    const bool unknown = selected.match == sed::level::SpatialMatch::cannotClassify;
    const bool ambiguous = selected.match == sed::level::SpatialMatch::ambiguous;
    bool boundary = false;
    if ( unique )
    {
        // Selection has already verified finite convex topology. A unique
        // closed-volume match can still lie on its boundary; do not promote
        // that into the strict-interior placement guarantee.
        const auto& sector = level.sectors[selected.target.sector];
        for ( const auto& surface : sector.surfaces )
        {
            const auto& anchor = sector.vertices[surface.vertices.front()];
            const auto& normal = surface.normal;
            const double distance = ((point.x - anchor.x) * normal.x + (point.y - anchor.y) * normal.y
                + (point.z - anchor.z) * normal.z) / std::sqrt(normal.lengthSqr());
            boundary = boundary || std::abs(distance) <= sed::level::kSpatialDistanceTolerance;
        }
    }
    Json row = Json::makeObject();
    row.set("index", number(static_cast<double>(index)));
    row.set("id", number(thing.id));
    row.set("name", Json::makeString(thing.name));
    row.set("template", Json::makeString(thing.templateName));
    row.set("position", xyz(point.x, point.y, point.z));
    row.set("status", Json::makeString(unknown ? "unknown" : ambiguous ? "ambiguous" : !unique ? "outside"
        : boundary ? "boundary" : "inside"));
    row.set("assignedSector", number(thing.sector));
    row.set("containingSector", unique ? number(selected.target.sector) : Json{});
    row.set("assignedSectorMatches", unknown || ambiguous ? Json{}
        : Json::makeBool(unique && thing.sector == selected.target.sector));
    row.set("onBoundary", unknown || ambiguous ? Json{} : Json::makeBool(boundary));
    row.set("candidates", unknown ? Json{} : number(static_cast<double>(selected.candidateCount)));
    row.set("tolerance", number(sed::level::kSpatialDistanceTolerance));
    row.set("detail", Json::makeString(selected.diagnostic));
    row.set("scope", Json::makeString("Point containment in classifiable convex sectors; no collision-volume or gameplay claim."));
    return row;
}

constexpr std::array kDifficultyNames{ "easy", "medium", "hard" };

Json difficultyCounts(const sed::level::DifficultyCounts& counts)
{
    Json out = Json::makeObject();
    for ( std::size_t i = 0; i < counts.size(); ++i )
    {
        out.set(kDifficultyNames[i], number(static_cast<double>(counts[i])));
    }
    return out;
}

Json placementCounts(const sed::level::PlacementCounts& counts)
{
    Json out = Json::makeObject();
    out.set("placements", number(static_cast<double>(counts.placements)));
    out.set("invalidSector", number(static_cast<double>(counts.invalidSector)));
    out.set("unresolvedTemplate", number(static_cast<double>(counts.unresolvedTemplate)));
    out.set("unknownEligibility", number(static_cast<double>(counts.unknownEligibility)));
    out.set("singlePlayer", difficultyCounts(counts.singlePlayer));
    out.set("multiplayer", difficultyCounts(counts.multiplayer));
    return out;
}

Json queryThingCensus(const sed::Level& level, const Json& request)
{
    for ( const auto& [key, value] : request.object )
    {
        // Every other query collection accepts the request envelope's schemaVersion.
        if ( key == "schemaVersion" )
        {
            if ( value.type != Json::Type::Number || value.number != 1 )
            {
                invalid("unsupported schemaVersion (expected 1)");
            }
            continue;
        }
        if ( key != "collection" && key != "offset" && key != "limit" && key != "intendedPlayers"
            && key != "inputSha256" && key != "sourceRevision" )
        {
            invalid("Unknown thingCensus field: " + key);
        }
    }
    const auto offset = integer(request, "offset", 0, 0);
    const auto limit = std::min<std::size_t>(200, integer(request, "limit", 1, 50));
    const auto intended = integer(request, "intendedPlayers", 1, 0);
    const auto census = sed::level::countThingPlacements(level);
    if ( const auto* error = std::get_if<sed::level::ThingCensusError>(&census) )
    {
        if ( *error == sed::level::ThingCensusError::unsupportedTarget )
        {
            throw AgentError{ kExitRefusal, "thingCensus requires a JK or MotS level", "thingCensusUnsupported" };
        }
        throw AgentError{ kExitRefusal, "Level exceeds the whole-document Thing census limits", "queryDetailsTooLarge" };
    }
    const auto& result = std::get<sed::level::ThingCensus>(census);
    Json starts = Json::makeObject();
    for ( std::size_t i = 0; i < kDifficultyNames.size(); ++i )
    {
        const auto confirmed = result.confirmedPlayerStarts[i];
        const auto possible = result.possibleAdditionalPlayerStarts[i];
        Json cell = Json::makeObject();
        cell.set("confirmed", number(static_cast<double>(confirmed)));
        cell.set("possibleAdditional", number(static_cast<double>(possible)));
        if ( intended )
        {
            const char* status = "indeterminate";
            if ( confirmed >= intended )
            {
                status = "meetsIntent";
            }
            else if ( confirmed + possible < intended )
            {
                status = "belowIntent";
            }
            cell.set("comparison", Json::makeString(status));
        }
        starts.set(kDifficultyNames[i], std::move(cell));
    }
    Json items = Json::makeArray();
    std::size_t index{};
    for ( const auto& [group, counts] : result.groups )
    {
        if ( index++ < offset )
        {
            continue;
        }
        if ( items.array.size() == limit )
        {
            break;
        }
        Json row = placementCounts(counts);
        row.set("sector", number(group.sector));
        row.set("layer", number(group.layer));
        row.set("template", Json::makeString(group.templateName));
        row.set("type", group.type.empty() ? Json{} : Json::makeString(group.type));
        items.array.push_back(std::move(row));
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("query"));
    out.set("scope", Json::makeString("authored-sector-and-load-flags"));
    out.set("summary", placementCounts(result.summary));
    out.set("multiplayerPlayerStarts", std::move(starts));
    if ( intended )
    {
        out.set("intendedPlayers", number(static_cast<double>(intended)));
    }
    out.set("total", number(static_cast<double>(result.groups.size())));
    out.set("offset", number(static_cast<double>(offset)));
    const auto end = offset + items.array.size();
    out.set("nextOffset", end < result.groups.size() ? number(static_cast<double>(end)) : Json{});
    out.set("items", std::move(items));
    return out;
}

// The retail renderer lists at most 128 sectors and 32 translucent portal
// faces a frame, and does not check the first limit. This walks frames the
// way it does, from the middle of every sector or from given points.
Json queryRenderBudget(const sed::Level& level, const Json& request)
{
    for ( const auto& [key, value] : request.object )
    {
        if ( key == "schemaVersion" )
        {
            if ( value.type != Json::Type::Number || value.number != 1 )
            {
                invalid("unsupported schemaVersion (expected 1)");
            }
            continue;
        }
        if ( key != "collection" && key != "offset" && key != "limit" && key != "viewpoints" && key != "sectors"
            && key != "yawSteps" && key != "pitchDegrees" && key != "fieldOfViewDegrees" && key != "listSectors"
            && key != "inputSha256" && key != "sourceRevision" )
        {
            invalid("Unknown renderBudget field: " + key);
        }
    }
    if ( level.kind != sed::ProjectType::JKDF2 && level.kind != sed::ProjectType::MOTS )
    {
        throw AgentError{ kExitRefusal, "renderBudget describes the JK and MotS renderer", "renderBudgetUnsupported" };
    }
    const auto offset = integer(request, "offset", 0, 0);
    const auto limit = std::min<std::size_t>(200, integer(request, "limit", 1, 20));
    sed::level::RenderBudgetOptions options;
    options.yawSteps = integer(request, "yawSteps", 1, 16);
    if ( options.yawSteps > 72 )
    {
        invalid("yawSteps must be at most 72");
    }
    if ( const auto* value = request.find("fieldOfViewDegrees") )
    {
        if ( value->type != Json::Type::Number || !std::isfinite(value->number) || value->number < 5
            || value->number > 179 )
        {
            invalid("fieldOfViewDegrees must be a number in 5..179");
        }
        options.camera.horizontalFovDegrees = value->number;
    }
    if ( const auto* value = request.find("pitchDegrees") )
    {
        if ( value->type != Json::Type::Array || value->array.empty() || value->array.size() > 9 )
        {
            invalid("pitchDegrees must list 1..9 angles");
        }
        options.pitchDegrees.clear();
        for ( const Json& pitch : value->array )
        {
            if ( pitch.type != Json::Type::Number || !std::isfinite(pitch.number) || pitch.number < -89
                || pitch.number > 89 )
            {
                invalid("pitchDegrees angles must be numbers in -89..89");
            }
            options.pitchDegrees.push_back(pitch.number);
        }
    }
    const auto sectorIndex = [&level](const Json& value, const char* name)
    {
        if ( value.type != Json::Type::Number || !std::isfinite(value.number) || value.number < 0
            || std::floor(value.number) != value.number || value.number >= static_cast<double>(level.sectors.size()) )
        {
            invalid(std::string(name) + " must name existing sectors");
        }
        return static_cast<std::int32_t>(value.number);
    };
    bool listSectors = false;
    if ( const auto* value = request.find("listSectors") )
    {
        if ( value->type != Json::Type::Bool )
        {
            invalid("listSectors must be true or false");
        }
        listSectors = value->boolean;
    }
    // A viewpoint with its own yawDegrees is one frame at that heading.
    struct FixedPose
    {
        double yaw{};
        double pitch{};
    };
    std::vector<std::optional<FixedPose>> poses;
    std::vector<sed::level::RenderViewpoint> viewpoints;
    const auto* given = request.find("viewpoints");
    const auto* chosen = request.find("sectors");
    if ( given && chosen )
    {
        invalid("Give viewpoints or sectors, not both");
    }
    if ( given )
    {
        if ( given->type != Json::Type::Array || given->array.empty() || given->array.size() > 4096 )
        {
            invalid("viewpoints must list 1..4096 positions");
        }
        for ( const Json& entry : given->array )
        {
            const auto* position = entry.isObject() ? entry.find("position") : nullptr;
            if ( !position || position->type != Json::Type::Array || position->array.size() != 3 )
            {
                invalid("Each viewpoint needs position [x, y, z]");
            }
            for ( const auto& [key, value] : entry.object )
            {
                (void)value;
                if ( key != "position" && key != "sector" && key != "yawDegrees" && key != "pitchDegrees" )
                {
                    invalid("Unknown viewpoint field: " + key);
                }
            }
            double xyzValues[3]{};
            for ( std::size_t axis = 0; axis < 3; ++axis )
            {
                const Json& coordinate = position->array[axis];
                if ( coordinate.type != Json::Type::Number || !std::isfinite(coordinate.number)
                    || std::abs(coordinate.number) > 1000000.0 )
                {
                    invalid("Viewpoint coordinates must be finite numbers within +/-1000000");
                }
                xyzValues[axis] = coordinate.number;
            }
            sed::level::RenderViewpoint viewpoint;
            viewpoint.position = sed::TVector{ xyzValues[0], xyzValues[1], xyzValues[2] };
            if ( const auto* sector = entry.find("sector") )
            {
                viewpoint.sector = sectorIndex(*sector, "viewpoint sector");
            }
            else if ( const auto found = sed::level::sectorContainingPoint(level, viewpoint.position) )
            {
                viewpoint.sector = *found;
            }
            else
            {
                throw AgentError{ kExitRefusal, "A viewpoint is not inside any sector; give its sector",
                    "renderBudgetViewpointOutside" };
            }
            std::optional<FixedPose> pose;
            const auto* yaw = entry.find("yawDegrees");
            const auto* pitch = entry.find("pitchDegrees");
            if ( pitch && !yaw )
            {
                invalid("A viewpoint's pitchDegrees needs its yawDegrees");
            }
            if ( yaw )
            {
                if ( yaw->type != Json::Type::Number || !std::isfinite(yaw->number) || std::abs(yaw->number) > 360
                    || (pitch && (pitch->type != Json::Type::Number || !std::isfinite(pitch->number)
                            || std::abs(pitch->number) > 89)) )
                {
                    invalid("A viewpoint's yawDegrees must be within +/-360 and its pitchDegrees within +/-89");
                }
                pose = FixedPose{ yaw->number, pitch ? pitch->number : 0.0 };
            }
            poses.push_back(pose);
            viewpoints.push_back(viewpoint);
        }
    }
    else
    {
        viewpoints = sed::level::sectorCentreViewpoints(level);
        if ( chosen )
        {
            if ( chosen->type != Json::Type::Array || chosen->array.empty() || chosen->array.size() > 4096 )
            {
                invalid("sectors must list 1..4096 sectors");
            }
            std::set<std::int32_t> wanted;
            for ( const Json& value : chosen->array )
            {
                wanted.insert(sectorIndex(value, "sectors"));
            }
            std::erase_if(viewpoints, [&wanted](const sed::level::RenderViewpoint& viewpoint)
                { return !wanted.contains(viewpoint.sector); });
        }
    }
    // Swept viewpoints go through the audit; fixed poses are single frames.
    std::vector<sed::level::RenderViewpoint> swept;
    for ( std::size_t index = 0; index < viewpoints.size(); ++index )
    {
        if ( index >= poses.size() || !poses[index] )
        {
            swept.push_back(viewpoints[index]);
        }
    }
    auto report = sed::level::auditRenderBudget(level, swept, options);
    for ( std::size_t index = 0; index < poses.size(); ++index )
    {
        if ( !poses[index] )
        {
            continue;
        }
        const auto frame = sed::level::traceRenderFrame(level, viewpoints[index].sector, viewpoints[index].position,
            poses[index]->yaw, poses[index]->pitch, options.camera, options.frameWorkLimit);
        sed::level::RenderBudgetView view;
        view.sector = viewpoints[index].sector;
        view.position = viewpoints[index].position;
        view.yawDegrees = poses[index]->yaw;
        view.pitchDegrees = poses[index]->pitch;
        view.visibleSectors = frame.sectors.size();
        view.alphaSurfaces = frame.alphaSurfaces;
        view.complete = frame.complete;
        ++report.viewpoints;
        ++report.frames;
        report.complete = report.complete && frame.complete;
        report.overSectorLimit += view.visibleSectors > sed::level::kRetailVisibleSectors ? 1 : 0;
        report.overAlphaLimit += view.alphaSurfaces > sed::level::kRetailAlphaSurfaces ? 1 : 0;
        report.views.push_back(view);
    }
    std::stable_sort(report.views.begin(), report.views.end(),
        [](const sed::level::RenderBudgetView& a, const sed::level::RenderBudgetView& b)
        { return a.visibleSectors != b.visibleSectors ? a.visibleSectors > b.visibleSectors : a.sector < b.sector; });
    const auto row = [&](const sed::level::RenderBudgetView& view)
    {
        Json item = Json::makeObject();
        item.set("sector", number(view.sector));
        item.set("position", xyz(view.position.x, view.position.y, view.position.z));
        item.set("yawDegrees", number(view.yawDegrees));
        item.set("pitchDegrees", number(view.pitchDegrees));
        item.set("visibleSectors", number(static_cast<double>(view.visibleSectors)));
        item.set("alphaSurfaces", number(static_cast<double>(view.alphaSurfaces)));
        item.set("overLimit", Json::makeBool(view.visibleSectors > sed::level::kRetailVisibleSectors
            || view.alphaSurfaces > sed::level::kRetailAlphaSurfaces));
        if ( !view.complete )
        {
            item.set("complete", Json::makeBool(false));
        }
        if ( listSectors )
        {
            // The engine's own order: it draws the first 128 and drops the rest.
            const auto frame = sed::level::traceRenderFrame(level, view.sector, view.position, view.yawDegrees,
                view.pitchDegrees, options.camera, options.frameWorkLimit);
            Json listed = Json::makeArray();
            for ( const auto sector : frame.sectors )
            {
                listed.array.push_back(number(sector));
            }
            item.set("sectors", std::move(listed));
        }
        return item;
    };
    Json items = Json::makeArray();
    for ( std::size_t index = offset; index < report.views.size() && items.array.size() < limit; ++index )
    {
        items.array.push_back(row(report.views[index]));
    }
    Json limits = Json::makeObject();
    limits.set("visibleSectors", number(static_cast<double>(sed::level::kRetailVisibleSectors)));
    limits.set("alphaSurfaces", number(static_cast<double>(sed::level::kRetailAlphaSurfaces)));
    Json camera = Json::makeObject();
    camera.set("fieldOfViewDegrees", number(options.camera.horizontalFovDegrees));
    camera.set("width", number(options.camera.width));
    camera.set("height", number(options.camera.height));
    camera.set("yawSteps", number(static_cast<double>(options.yawSteps)));
    Json pitches = Json::makeArray();
    for ( const double pitch : options.pitchDegrees )
    {
        pitches.array.push_back(number(pitch));
    }
    camera.set("pitchDegrees", std::move(pitches));
    std::size_t maxAlpha = 0;
    for ( const auto& view : report.views )
    {
        maxAlpha = std::max(maxAlpha, view.alphaSurfaces);
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("query"));
    out.set("scope", Json::makeString("retail-render-frame-capacity"));
    out.set("limits", std::move(limits));
    out.set("camera", std::move(camera));
    out.set("viewpoints", number(static_cast<double>(report.viewpoints)));
    out.set("frames", number(static_cast<double>(report.frames)));
    out.set("complete", Json::makeBool(report.complete));
    out.set("maxVisibleSectors", number(report.views.empty() ? 0.0 : static_cast<double>(report.views.front().visibleSectors)));
    out.set("maxAlphaSurfaces", number(static_cast<double>(maxAlpha)));
    out.set("overSectorLimit", number(static_cast<double>(report.overSectorLimit)));
    out.set("overAlphaLimit", number(static_cast<double>(report.overAlphaLimit)));
    out.set("total", number(static_cast<double>(report.views.size())));
    out.set("offset", number(static_cast<double>(offset)));
    const auto end = offset + items.array.size();
    out.set("nextOffset", end < report.views.size() ? number(static_cast<double>(end)) : Json{});
    out.set("items", std::move(items));
    return out;
}
} // namespace

std::optional<Json> queryRecordDetails(const sed::Level& level, const Json& request)
{
    const auto* collection = request.find("collection");
    if ( !collection || collection->type != Json::Type::String )
    {
        return std::nullopt;
    }
    const auto& name = collection->str;
    if ( name == "thingCensus" )
    {
        return queryThingCensus(level, request);
    }
    if ( name == "renderBudget" )
    {
        return queryRenderBudget(level, request);
    }
    if ( name != "sectors" && name != "templates" && name != "cogs" && name != "architecture"
        && name != "architecturalVertices" && name != "thingContainment" )
    {
        return std::nullopt;
    }
    const auto offset = integer(request, "offset", 0, 0);
    const auto limit = std::min<std::size_t>(200, integer(request, "limit", 1, 50));
    std::string_view search;
    if ( const auto* value = request.find("search") )
    {
        if ( value->type != Json::Type::String )
        {
            invalid("search must be a string");
        }
        search = value->str;
    }
    const sed::MotsArchLightMesh* mesh = nullptr;
    std::size_t lightingIndex{}, meshIndex{};
    if ( name == "architecturalVertices" )
    {
        lightingIndex = integer(request, "lightingIndex", 0);
        meshIndex = integer(request, "mesh", 0);
        if ( lightingIndex >= level.motsArchLighting.size()
            || meshIndex >= level.motsArchLighting[lightingIndex].meshes.size() )
        {
            invalid("lightingIndex or mesh is outside the stored architecture collection");
        }
        mesh = &level.motsArchLighting[lightingIndex].meshes[meshIndex];
    }
    else if ( request.find("lightingIndex") || request.find("mesh") )
    {
        invalid("lightingIndex and mesh apply only to architecturalVertices");
    }
    const auto count = name == "sectors" ? level.sectors.size()
        : name == "templates" ? level.templates.size() : name == "cogs" ? level.cogs.size()
        : name == "architecture" ? level.motsArchLighting.size()
        : mesh ? mesh->vertices.size() : level.things.size();
    std::vector<std::size_t> selected;
    std::size_t total{};
    const bool filtered = !search.empty()
        && (name == "templates" || name == "cogs" || name == "thingContainment");
    std::size_t filterWork = kFilterWorkBudget;
    const AsciiSubstringMatcher matcher(filtered ? search : std::string_view{}, filterWork);
    if ( !filtered )
    {
        total = count;
        for ( auto i = offset; i < count && selected.size() < limit; ++i )
        {
            selected.push_back(i);
        }
    }
    else
    {
        for ( std::size_t i = 0; i < count; ++i )
        {
            consumeRecords(filterWork, 1); // Empty names still cost one record visit.
            const bool matches = name == "templates" ? matcher.matches(level.templates[i].name, filterWork)
                : name == "cogs" ? matcher.matches(level.cogs[i].name, filterWork)
                : matcher.matches(level.things[i].name, filterWork)
                    || matcher.matches(level.things[i].templateName, filterWork);
            if ( !matches )
            {
                continue;
            }
            if ( total >= offset && selected.size() < limit )
            {
                selected.push_back(i);
            }
            ++total;
        }
    }
    // Admit emitted authored strings before any asString or Json string copy.
    // JSON escaping can expand these bytes by at most six; fixed row syntax is
    // separately bounded by the page and nested-record limits.
    admitPageText(level, name, selected);
    if ( name == "architecture" )
    {
        std::size_t bindingWork = kArchitectureBindingWorkBudget;
        for ( std::size_t row = 0; row < selected.size(); ++row )
        {
            consumeRecords(bindingWork, level.things.size());
        }
    }
    std::string geometryProblem;
    if ( name == "thingContainment" && !selected.empty() )
    {
        std::size_t geometryWork = 20'000'000 / selected.size();
        for ( std::size_t s = 0; s < level.sectors.size(); ++s )
        {
            const auto& sector = level.sectors[s];
            // Classification visits empty sectors too. Precision plane checks
            // traverse face corners for each vertex; counting faces alone
            // understates that work, while individual face checks are quadratic.
            consumeRecords(geometryWork, 1);
            consumeRecords(geometryWork, sector.vertices.size());
            consumeRecords(geometryWork, sector.surfaces.size());
            std::size_t corners = 0;
            for ( const auto& surface : sector.surfaces )
            {
                consumeRecords(geometryWork, surface.vertices.size());
                consumeProduct(geometryWork, surface.vertices.size(), surface.vertices.size());
                corners += surface.vertices.size(); // Already admitted against the finite page allowance.
            }
            consumeProduct(geometryWork, sector.vertices.size(), corners);
            if ( geometryProblem.empty() && std::any_of(sector.vertices.begin(), sector.vertices.end(),
                    [](const sed::Vertex& vertex)
                    {
                        return !std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z);
                    }) )
            {
                geometryProblem = "Sector " + std::to_string(s)
                    + " has nonfinite geometry and no trustworthy containment bounds";
            }
        }
    }
    std::size_t records = 4096;
    std::size_t samples = 1'000'000;
    Json items = Json::makeArray();
    for ( const auto i : selected )
    {
        Json row = Json::makeObject();
        row.set("index", number(static_cast<double>(i)));
        if ( name == "sectors" )
        {
            row = describeSectorRecord(level, i, records);
        }
        else if ( name == "templates" )
        {
            const auto& item = level.templates[i];
            row.set("name", Json::makeString(item.name));
            row.set("parent", Json::makeString(item.parent));
            row.set("parameterScope", Json::makeString("declared"));
            row.set("sourceParameters", parameterRows(item.values, records));
        }
        else if ( name == "cogs" )
        {
            const auto& cog = level.cogs[i];
            row.set("num", number(cog.num));
            row.set("id", number(cog.id));
            row.set("name", Json::makeString(cog.name));
            row.set("sourceParameters", parameterRows(cog.values, records));
            Json values = Json::makeArray();
            for ( const auto& value : cog.values )
            {
                values.array.push_back(Json::makeString(value.asString()));
            }
            row.set("values", std::move(values));
            row.set("bindingStatus", Json::makeString("unverified-use-bindings-with-explicit-resources"));
        }
        else if ( name == "architecture" )
        {
            const auto& object = level.motsArchLighting[i];
            consumeRecords(records, object.meshes.size());
            Json counts = Json::makeArray();
            for ( const auto& item : object.meshes )
            {
                consumeRecords(samples, item.vertices.size());
                counts.array.push_back(number(static_cast<double>(item.vertices.size())));
            }
            Json bound = Json::makeArray();
            for ( std::size_t t = 0; t < level.things.size(); ++t )
            {
                if ( level.things[t].archLightingIndex >= 0
                    && static_cast<std::size_t>(level.things[t].archLightingIndex) == i )
                {
                    consumeRecords(records, 1);
                    bound.array.push_back(number(static_cast<double>(t)));
                }
            }
            row.set("meshVertexCounts", std::move(counts));
            row.set("boundThings", std::move(bound));
            row.set("samplesSha256", Json::makeString(sed::level::archSampleDigestHex(object)));
            row.set("modelCompatibility", Json::makeString("unverified-use-arch-validate-with-explicit-resources"));
        }
        else if ( name == "architecturalVertices" )
        {
            const auto& vertex = mesh->vertices[i];
            row.set("lightingIndex", number(static_cast<double>(lightingIndex)));
            row.set("mesh", number(static_cast<double>(meshIndex)));
            row.set("mono", number(vertex.mono));
            row.set("rgb", xyz(vertex.red, vertex.green, vertex.blue));
        }
        else
        {
            row = describeContainment(level, i, geometryProblem);
        }
        items.array.push_back(std::move(row));
    }
    Json result = Json::makeObject();
    result.set("ok", Json::makeBool(true));
    result.set("command", Json::makeString("query"));
    result.set("items", std::move(items));
    result.set("total", number(static_cast<double>(total)));
    result.set("offset", number(static_cast<double>(offset)));
    const auto end = offset >= total ? total : offset + selected.size();
    result.set("nextOffset", end < total ? number(static_cast<double>(end)) : Json{});
    return result;
}
} // namespace sed_agent
