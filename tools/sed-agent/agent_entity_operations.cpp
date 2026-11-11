#include "agent_entity_operations.hpp"

#include "agent_workspace.hpp"
#include "sed/level/arch_authoring.hpp"
#include "sed/level/spatial_selection.hpp"
#include "sed/level/transform.hpp"
#include "sed/level/thing.hpp"
#include "sed/level/workbench.hpp"
#include "sed/text.hpp"
#include "sed/values.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sed_agent
{
namespace
{
[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "entityOperationInvalid" };
}

[[noreturn]] void refuse(const std::string& code, const std::string& message)
{
    throw AgentError{ kExitRefusal, message, code };
}

void fields(const Json& operation, const std::initializer_list<std::string_view> allowed)
{
    for ( const auto& [name, value] : operation.object )
    {
        (void)value;
        if ( std::find(allowed.begin(), allowed.end(), name) == allowed.end() )
        {
            invalid("Unknown entity operation field '" + name + "'");
        }
    }
}

const Json& field(const Json& operation, const char* name, const Json::Type type)
{
    const auto* value = operation.find(name);
    if ( !value || value->type != type )
    {
        invalid(std::string("Missing or invalid '") + name + "'");
    }
    return *value;
}

double boundedNumber(const Json& value, const double minimum, const double maximum,
    const std::string& name)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number)
        || value.number < minimum || value.number > maximum )
    {
        invalid(name + " must be finite and within " + std::to_string(minimum)
            + ".." + std::to_string(maximum));
    }
    return value.number;
}

double number(const Json& operation, const char* name, const double minimum, const double maximum)
{
    return boundedNumber(field(operation, name, Json::Type::Number), minimum, maximum, name);
}

std::int32_t index(const Json& operation, const char* name, const std::size_t count)
{
    const double value = number(operation, name, 0, std::numeric_limits<std::int32_t>::max());
    if ( std::floor(value) != value || value >= static_cast<double>(count) )
    {
        invalid(std::string(name) + " is outside the current document");
    }
    return static_cast<std::int32_t>(value);
}

sed::TVector vector(const Json& operation, const char* name)
{
    const auto& components = field(operation, name, Json::Type::Array).array;
    if ( components.size() != 3 )
    {
        invalid(std::string(name) + " must contain exactly three numbers");
    }
    return { boundedNumber(components[0], -1000000, 1000000, name),
        boundedNumber(components[1], -1000000, 1000000, name),
        boundedNumber(components[2], -1000000, 1000000, name) };
}

std::string token(const Json& operation, const char* name)
{
    const auto& value = field(operation, name, Json::Type::String).str;
    if ( value.empty() || value.size() > 127
        || std::any_of(value.begin(), value.end(), [](const unsigned char c)
        {
            return c < 33 || c > 126 || c == '#' || c == '"';
        }) )
    {
        invalid(std::string(name) + " must be a nonempty ASCII token of at most 127 characters without whitespace, quotes, or comments");
    }
    return value;
}

std::optional<double> nullableNumber(const Json& operation, const char* name,
    const double minimum, const double maximum)
{
    const auto* value = operation.find(name);
    if ( value && value->type == Json::Type::Null )
    {
        return std::nullopt;
    }
    return number(operation, name, minimum, maximum);
}

std::int32_t flags(const Json& operation)
{
    const double value = number(operation, "flags", 0.0, std::numeric_limits<std::uint32_t>::max());
    if ( std::floor(value) != value )
    {
        invalid("flags must be an unsigned 32-bit integer");
    }
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

void applyLight(sed::Level& level, const Json& operation, const bool add)
{
    fields(operation, add
        ? std::initializer_list<std::string_view>{ "op", "position", "rgb", "intensity", "range",
            "rgbIntensity", "legacyRgbRange", "flags", "layer" }
        : std::initializer_list<std::string_view>{ "op", "light", "position", "rgb", "intensity", "range",
            "rgbIntensity", "legacyRgbRange", "flags", "layer" });
    const std::int32_t lightIndex = add ? -1 : index(operation, "light", level.lights.size());
    sed::SedLight light = add ? sed::SedLight{} : level.lights[lightIndex];
    if ( !add && operation.object.size() == 2 )
    {
        invalid("setLight requires at least one source-light field to edit");
    }
    if ( add || operation.find("position") )
    {
        light.position = vector(operation, "position");
    }
    if ( add || operation.find("rgb") )
    {
        const auto& color = field(operation, "rgb", Json::Type::Array).array;
        if ( color.size() != 3 )
        {
            invalid("rgb must contain three components in 0..1");
        }
        light.color.r = static_cast<float>(boundedNumber(color[0], 0, 1, "rgb"));
        light.color.g = static_cast<float>(boundedNumber(color[1], 0, 1, "rgb"));
        light.color.b = static_cast<float>(boundedNumber(color[2], 0, 1, "rgb"));
        if ( level.kind != sed::ProjectType::MOTS
            && (light.color.r != 1.0f || light.color.g != 1.0f || light.color.b != 1.0f) )
        {
            refuse("entityColorRequiresMots", "Colored source-light authoring requires a MotS level");
        }
    }
    if ( add || operation.find("intensity") )
    {
        light.intensity = number(operation, "intensity", 0, 1000);
    }
    if ( add || operation.find("range") )
    {
        light.range = number(operation, "range", 0.0001, 100000);
    }
    if ( operation.find("rgbIntensity") )
    {
        light.rgbIntensity = nullableNumber(operation, "rgbIntensity", 0, 1000);
        if ( level.kind != sed::ProjectType::MOTS && light.rgbIntensity )
        {
            refuse("entityColorRequiresMots", "Independent RGB source-light intensity requires a MotS level");
        }
    }
    if ( operation.find("legacyRgbRange") )
    {
        light.legacyRgbRange = nullableNumber(operation, "legacyRgbRange", -100000, 100000);
    }
    if ( operation.find("flags") )
    {
        light.flags = flags(operation);
    }
    if ( operation.find("layer") )
    {
        light.layer = index(operation, "layer", std::max<std::size_t>(1, level.layers.size()));
    }
    if ( add )
    {
        if ( !level.canAllocateIds(1) )
        {
            refuse("entityOperationUnsupported", "Source-light identity capacity is exhausted");
        }
        auto& created = level.newLight();
        light.id = created.id;
        created = std::move(light);
    }
    else
    {
        level.lights[lightIndex] = std::move(light);
    }
}

void requireUnbound(const sed::Level& level, const std::int32_t thingIndex)
{
    if ( level.things[thingIndex].archLightingIndex >= 0
        || std::any_of(level.motsArchAuthoring.begin(), level.motsArchAuthoring.end(),
            [thingIndex](const auto& entry) { return entry.thingIndex == thingIndex; }) )
    {
        refuse("entityArchBound", "Clear this Thing's ARCH binding explicitly before changing its pose, template, or parameters; stored sample arrays will be retained");
    }
}

void requireNoLocalFrames(const sed::Thing& thing)
{
    if ( sed::level::thingCountFrames(thing) != 0 )
    {
        refuse("entityFramesRequireOperation", "Changing a template or move mode with local frames requires a coordinated frame edit; use setThingFrames and retain the mover contract");
    }
}

void addThing(sed::Level& level, const Json& operation,
    const std::optional<sed::level::CogSchemaResolver>& schemaResolver)
{
    fields(operation, { "op", "sector", "template", "name", "position" });
    const auto sector = index(operation, "sector", level.sectors.size());
    const auto templateName = token(operation, "template");
    const auto name = token(operation, "name");
    const auto position = vector(operation, "position");
    validateEntityAppendSource(level);
    const auto selection = sed::level::selectSectorAtPoint(level, {}, position);
    if ( selection.match != sed::level::SpatialMatch::unique || selection.target.sector != sector )
    {
        refuse("entityPositionOutsideSector",
            "Thing position must be strictly inside the specified uniquely classifiable sector; boundary, overlapping, and unclassifiable positions are refused");
    }
    if ( const auto problem = sed::level::validatePlacementPointInsideSector(level, position, sector) )
    {
        refuse("entityPositionOutsideSector", problem->message);
    }
    const auto prepared = sed::level::prepareThingPlacement(level, position, templateName, name,
        {}, 0, sector, 0, schemaResolver);
    if ( !prepared.ok() )
    {
        refuse(prepared.failure->code, prepared.failure->message);
    }
    if ( !prepared.apply(level) )
    {
        refuse("entityOperationStale", "The prepared Thing placement no longer matches this document");
    }
}

void setThingValue(sed::Level& level, const std::int32_t thingIndex, const Json& operation)
{
    fields(operation, { "op", "thing", "key", "value" });
    const auto key = token(operation, "key");
    const auto text = token(operation, "value");
    if ( std::any_of(key.begin(), key.end(), [](const unsigned char c)
        {
            return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_');
        }) )
    {
        invalid("key must contain only ASCII letters, digits, or underscore");
    }
    if ( sed::text::iequalsAscii(key, "frame") || sed::text::iequalsAscii(key, "numframes") )
    {
        refuse("entityFramesRequireOperation", "Use setThingFrames to edit ordered frame and numframes records together");
    }
    auto value = sed::deserializeTplValue(key, text);
    const auto expectedType = sed::tplValueTypeForName(key);
    if ( value.type != expectedType || (value.type == sed::ValueType::floating && !std::isfinite(value.floatValue)) )
    {
        invalid("value is not valid for the typed Thing parameter '" + key + "'");
    }
    auto& thing = level.things[thingIndex];
    const auto matches = std::count_if(thing.values.begin(), thing.values.end(),
        [&](const auto& existing) { return sed::text::iequalsAscii(existing.name, key); });
    if ( matches > 1 )
    {
        refuse("entityOperationUnsupported", "The target parameter occurs more than once; an ordered multi-record edit is required");
    }
    const auto existing = std::find_if(thing.values.begin(), thing.values.end(),
        [&](const auto& candidate) { return sed::text::iequalsAscii(candidate.name, key); });
    const sed::TplValue* declared = existing != thing.values.end() ? &*existing
        : sed::level::thingFindValue(level, thing, key);
    if ( expectedType == sed::ValueType::str && declared && declared->type != sed::ValueType::str )
    {
        if ( declared->type == sed::ValueType::frame || declared->type == sed::ValueType::vect )
        {
            refuse("entityOperationUnsupported", "This native parameter requires a typed tuple/frame editor");
        }
        // Native files can retain a more precise type than the name-based
        // text importer. Reuse that declaration (including reference category)
        // rather than silently weakening a numeric record into an opaque token.
        value = *declared;
        if ( !value.parse(text)
            || (value.type == sed::ValueType::floating && !std::isfinite(value.floatValue)) )
        {
            invalid("value is not valid for the native Thing parameter's declared type");
        }
    }
    if ( existing != thing.values.end() && sed::serializeTplValue(*existing) == sed::serializeTplValue(value) )
    {
        return;
    }
    requireUnbound(level, thingIndex);
    if ( sed::text::iequalsAscii(key, "move") )
    {
        requireNoLocalFrames(thing);
    }
    if ( existing == thing.values.end() )
    {
        thing.values.push_back(std::move(value));
    }
    else
    {
        // Replace the addressed record in place. Repeated unrelated records,
        // inherited parameters, and every frame retain their original order.
        value.name = existing->name;
        *existing = std::move(value);
    }
}

void applyThing(sed::Level& level, const Json& operation, const std::string& name)
{
    const auto thingIndex = index(operation, "thing", level.things.size());
    auto& thing = level.things[thingIndex];
    if ( name == "setThingName" )
    {
        fields(operation, { "op", "thing", "name" });
        thing.name = token(operation, "name");
    }
    else if ( name == "setThingPosition" )
    {
        fields(operation, { "op", "thing", "position", "sector" });
        const auto position = vector(operation, "position");
        const auto sector = index(operation, "sector", level.sectors.size());
        if ( thing.x == position.x && thing.y == position.y && thing.z == position.z && thing.sector == sector )
        {
            return;
        }
        requireUnbound(level, thingIndex);
        const auto selection = sed::level::selectSectorAtPoint(level, {}, position);
        if ( selection.match != sed::level::SpatialMatch::unique || selection.target.sector != sector )
        {
            refuse("entityPositionOutsideSector", "Thing position must be strictly inside the specified uniquely classifiable sector; boundary, overlapping, and unclassifiable positions are refused");
        }
        if ( const auto problem = sed::level::validatePlacementPointInsideSector(level, position, sector) )
        {
            refuse("entityPositionOutsideSector", problem->message);
        }
        // This is absolute placement, so authored path frames are unchanged.
        thing.x = position.x;
        thing.y = position.y;
        thing.z = position.z;
        thing.sector = sector;
    }
    else if ( name == "setThingOrientation" )
    {
        fields(operation, { "op", "thing", "orientation" });
        const auto orientation = vector(operation, "orientation");
        if ( thing.pch == orientation.x && thing.yaw == orientation.y && thing.rol == orientation.z )
        {
            return;
        }
        requireUnbound(level, thingIndex);
        thing.pch = orientation.x;
        thing.yaw = orientation.y;
        thing.rol = orientation.z;
    }
    else if ( name == "setThingTemplate" )
    {
        fields(operation, { "op", "thing", "template" });
        const auto requested = token(operation, "template");
        const auto found = std::find_if(level.templates.begin(), level.templates.end(),
            [&](const auto& item) { return sed::text::iequalsAscii(item.name, requested); });
        if ( found == level.templates.end() )
        {
            invalid("template must name a template already defined in this document");
        }
        if ( thing.templateName == found->name )
        {
            return;
        }
        requireUnbound(level, thingIndex);
        requireNoLocalFrames(thing);
        thing.templateName = found->name;
    }
    else if ( name == "setThingValue" )
    {
        setThingValue(level, thingIndex, operation);
    }
    else
    {
        fields(operation, { "op", "thing" });
        const auto prepared = sed::level::prepareUnbindArch(level, thingIndex);
        if ( !prepared.ok() )
        {
            refuse(prepared.failure->code, prepared.failure->message);
        }
        if ( !prepared.apply(level) )
        {
            refuse("entityOperationStale", "The prepared ARCH clear no longer matches this document");
        }
    }
}
} // namespace

void validateEntityAppendSource(const sed::Level& level)
{
    const bool unknownSectorLine = std::any_of(level.retainedJklSectorUnknownLines.begin(),
        level.retainedJklSectorUnknownLines.end(), [](const auto& lines) { return !lines.empty(); });
    if ( !level.retainedJklUnknownSections.empty() || !level.retainedJklHeaderUnknownLines.empty()
        || unknownSectorLine )
    {
        refuse("entityAppendUnknownState",
            "Thing creation cannot safely preserve unmodeled JKL records that may contain entity indices; use a typed importer before appending Things");
    }
}

void validateAppendIdentitySource(const sed::Level& level)
{
    std::int32_t maximumId = 0;
    const auto inspect = [&](const auto& entities)
    {
        for ( const auto& entity : entities )
        {
            maximumId = std::max(maximumId, entity.id);
        }
    };
    inspect(level.sectors);
    inspect(level.things);
    inspect(level.lights);
    inspect(level.cogs);
    inspect(level.h3doNodes);
    if ( level.curId <= 0 || level.curId <= maximumId )
    {
        refuse("entityAppendInvalidIdentity", "Cannot allocate new editor identities: next ID "
            + std::to_string(level.curId) + " must be positive and greater than the highest live ID "
            + std::to_string(maximumId)
            + ". Inspect and explicitly repair the source identity state before appending; no IDs were renumbered");
    }
}

// Shared index-set parse for the translate operations: non-empty,
// in-document indices, sorted with duplicates removed (each named entity
// is translated once).
std::vector<std::int32_t> indexList(const Json& operation, const char* name, const std::size_t count)
{
    const auto& entries = field(operation, name, Json::Type::Array).array;
    if ( entries.empty() )
    {
        invalid(std::string(name) + " must be a non-empty array of indices");
    }
    std::vector<std::int32_t> indices{};
    for ( const Json& entry : entries )
    {
        const double value = boundedNumber(entry, 0.0,
            static_cast<double>(std::numeric_limits<std::int32_t>::max()), name);
        if ( std::floor(value) != value || value >= static_cast<double>(count) )
        {
            invalid(std::string(name) + " entries must be indices in the current document");
        }
        indices.push_back(static_cast<std::int32_t>(value));
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

void applyTranslateThings(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "things", "delta", "moveFrames" });
    const auto things = indexList(operation, "things", level.things.size());
    const auto delta = vector(operation, "delta");
    bool moveFrames = true;
    if ( const auto* frames = operation.find("moveFrames") )
    {
        if ( frames->type != Json::Type::Bool )
        {
            invalid("'moveFrames' must be a boolean");
        }
        moveFrames = frames->boolean;
    }
    for ( const std::int32_t thingIndex : things )
    {
        requireUnbound(level, thingIndex);
    }
    sed::Level candidate = level;
    const bool changed = sed::level::levelTranslateThings(candidate, things,
        delta.x, delta.y, delta.z, moveFrames, true);
    const bool moves = delta.x != 0.0 || delta.y != 0.0 || delta.z != 0.0;
    if ( !changed && moves )
    {
        refuse("entityTransformRefused", "The shared core refused the requested Thing translation");
    }
    if ( moves )
    {
        for ( const std::int32_t thingIndex : things )
        {
            const auto& thing = candidate.things[static_cast<std::size_t>(thingIndex)];
            const sed::TVector position{ thing.x, thing.y, thing.z };
            const auto selection = sed::level::selectSectorAtPoint(candidate, {}, position);
            if ( selection.match != sed::level::SpatialMatch::unique || selection.target.sector != thing.sector )
            {
                refuse("entityPositionOutsideSector", "Thing position must be strictly inside the specified uniquely classifiable sector; boundary, overlapping, and unclassifiable positions are refused");
            }
            if ( const auto problem = sed::level::validatePlacementPointInsideSector(candidate, position, thing.sector) )
            {
                refuse("entityPositionOutsideSector", problem->message);
            }
        }
    }
    level = std::move(candidate);
}

void applyTranslateLights(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "lights", "delta" });
    const auto lights = indexList(operation, "lights", level.lights.size());
    const auto delta = vector(operation, "delta");
    sed::Level candidate = level;
    sed::level::levelTranslateLights(candidate, lights, delta.x, delta.y, delta.z);
    level = std::move(candidate);
}

bool tryApplyEntityOperation(sed::Level& level, const Json& operation,
    const std::optional<sed::level::CogSchemaResolver>& schemaResolver)
{
    const auto* nameValue = operation.find("op");
    if ( !nameValue || nameValue->type != Json::Type::String )
    {
        return false;
    }
    const auto& name = nameValue->str;
    constexpr std::string_view names[] = { "addLight", "setLight", "addThing", "setThingPosition", "setThingOrientation",
        "setThingName", "setThingTemplate", "setThingValue", "clearThingArchitecture",
        "translateThings", "translateLights" };
    if ( std::find(std::begin(names), std::end(names), name) == std::end(names) )
    {
        return false;
    }
    if ( level.kind != sed::ProjectType::JKDF2 && level.kind != sed::ProjectType::MOTS )
    {
        refuse("entityOperationUnsupported", "Entity design operations currently support JK and MotS projects");
    }
    if ( name == "addLight" || name == "setLight" )
    {
        applyLight(level, operation, name == "addLight");
    }
    else if ( name == "addThing" )
    {
        addThing(level, operation, schemaResolver);
    }
    else if ( name == "translateThings" )
    {
        applyTranslateThings(level, operation);
    }
    else if ( name == "translateLights" )
    {
        applyTranslateLights(level, operation);
    }
    else
    {
        applyThing(level, operation, name);
    }
    return true;
}
} // namespace sed_agent
