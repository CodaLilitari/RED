#include "agent_parity_operations.hpp"

#include "agent_workspace.hpp"

#include "sed/geometry.hpp"
#include "sed/level.hpp"
#include "sed/level/edit.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/recipes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sed_agent
{
namespace
{
constexpr std::size_t kSelectionLimit = 4096;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "parityOperationInvalid" };
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
            invalid("Unknown field '" + name + "'");
        }
    }
}

const Json& field(const Json& operation, const std::string_view name, const Json::Type type)
{
    const auto* value = operation.find(name);
    if ( !value || value->type != type )
    {
        invalid("Field '" + std::string(name) + "' is missing or has the wrong type");
    }
    return *value;
}

std::int32_t indexValue(const Json& value, const std::string_view name, const std::size_t count)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number) || value.number < 0
        || std::floor(value.number) != value.number || value.number >= static_cast<double>(count) )
    {
        invalid("'" + std::string(name) + "' must address an existing record");
    }
    return static_cast<std::int32_t>(value.number);
}

double bounded(const Json& value, const double minimum, const double maximum, const std::string_view name)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number) || value.number < minimum || value.number > maximum )
    {
        invalid("'" + std::string(name) + "' must be a finite number within " + std::to_string(minimum) + ".."
            + std::to_string(maximum));
    }
    return value.number;
}

// A non-empty list of distinct indices into a table of `count` records.
std::vector<std::int32_t> indexList(const Json& operation, const std::string_view name, const std::size_t count)
{
    const auto& list = field(operation, name, Json::Type::Array).array;
    if ( list.empty() || list.size() > kSelectionLimit )
    {
        invalid("'" + std::string(name) + "' must list 1.." + std::to_string(kSelectionLimit) + " records");
    }
    std::vector<std::int32_t> result;
    std::set<std::int32_t> seen;
    for ( const Json& value : list )
    {
        const auto i = indexValue(value, name, count);
        if ( !seen.insert(i).second )
        {
            invalid("'" + std::string(name) + "' must name each record once");
        }
        result.push_back(i);
    }
    return result;
}

std::vector<sed::SurfaceRef> surfaceList(const sed::Level& level, const Json& operation)
{
    const auto& list = field(operation, "surfaces", Json::Type::Array).array;
    if ( list.empty() || list.size() > kSelectionLimit )
    {
        invalid("'surfaces' must list 1.." + std::to_string(kSelectionLimit) + " [sector, surface] pairs");
    }
    std::vector<sed::SurfaceRef> result;
    std::set<std::pair<std::int32_t, std::int32_t>> seen;
    for ( const Json& pair : list )
    {
        if ( pair.type != Json::Type::Array || pair.array.size() != 2 )
        {
            invalid("'surfaces' entries must be [sector, surface]");
        }
        const auto sector = indexValue(pair.array[0], "surfaces sector", level.sectors.size());
        const auto surface = indexValue(pair.array[1], "surfaces surface",
            level.sectors[static_cast<std::size_t>(sector)].surfaces.size());
        if ( !seen.insert({ sector, surface }).second )
        {
            invalid("'surfaces' must name each surface once");
        }
        result.push_back({ sector, surface });
    }
    return result;
}

// A light value: one number (all channels, as JK stores it) or [r, g, b].
sed::ColorF lightValue(const Json& value, const std::string_view name, const double maximum)
{
    if ( value.type == Json::Type::Number )
    {
        const auto v = static_cast<float>(bounded(value, 0.0, maximum, name));
        return sed::ColorF{ v, v, v };
    }
    if ( value.type != Json::Type::Array || value.array.size() != 3 )
    {
        invalid("'" + std::string(name) + "' must be a number or [r, g, b]");
    }
    return sed::ColorF{ static_cast<float>(bounded(value.array[0], 0.0, maximum, name)),
        static_cast<float>(bounded(value.array[1], 0.0, maximum, name)),
        static_cast<float>(bounded(value.array[2], 0.0, maximum, name)) };
}

std::string resourceName(const Json& value, const std::string_view name)
{
    if ( value.type != Json::Type::String || value.str.size() > 63 )
    {
        invalid("'" + std::string(name) + "' must be a file name of at most 63 characters");
    }
    for ( const char c : value.str )
    {
        if ( c <= ' ' || c == '"' || c == '/' || c == '\\' || static_cast<unsigned char>(c) > 126 )
        {
            invalid("'" + std::string(name) + "' must be a plain file name without spaces or paths");
        }
    }
    return value.str;
}

Json numbers(const std::vector<std::int32_t>& values)
{
    Json list = Json::makeArray();
    for ( const auto v : values ) list.array.push_back(Json::makeNumber(static_cast<double>(v)));
    return list;
}

void requireEditableKind(const sed::Level& level, const std::string& name)
{
    if ( level.kind != sed::ProjectType::JKDF2 && level.kind != sed::ProjectType::MOTS )
    {
        refuse("parityOperationUnsupported", name + " currently supports JK and MotS projects");
    }
}

Json deleteRecords(sed::Level& level, const Json& operation, const std::string& name)
{
    const bool things = name == "deleteThings";
    const bool lights = name == "deleteLights";
    const char* listName = things ? "things" : lights ? "lights" : "cogs";
    fields(operation, { "op", listName });
    const std::size_t count = things ? level.things.size() : lights ? level.lights.size() : level.cogs.size();
    auto doomed = indexList(operation, listName, count);
    std::size_t referencesCleared = 0;
    if ( things )
    {
        for ( const auto t : doomed )
        {
            if ( level.things[static_cast<std::size_t>(t)].archLightingIndex >= 0 )
            {
                refuse("parityThingHasArchitecture", "Thing " + std::to_string(t)
                    + " carries MotS architectural lighting; clearThingArchitecture first");
            }
        }
    }
    // References the core clears to -1: COG values pointing at a deleted record.
    for ( const auto& cog : level.cogs )
    {
        for ( const auto& value : cog.values )
        {
            const bool thingRef = things && value.type == sed::ValueType::ptr && value.cogType == sed::CogType::thing;
            const bool cogRef = !things && !lights && value.type == sed::ValueType::integer && value.cogType == sed::CogType::cog;
            if ( (thingRef || cogRef) && std::find(doomed.begin(), doomed.end(), value.intValue) != doomed.end() )
            {
                ++referencesCleared;
            }
        }
    }
    std::sort(doomed.begin(), doomed.end(), std::greater<>());
    for ( const auto i : doomed )
    {
        const bool ok = things ? sed::level::levelDeleteThing(level, i)
            : lights ? sed::level::levelDeleteLight(level, i) : sed::level::levelDeleteCog(level, i);
        if ( !ok )
        {
            refuse("parityOperationUnsupported", "The shared core could not delete " + std::string(listName) + " "
                + std::to_string(i));
        }
    }
    Json report = Json::makeObject();
    report.set("deleted", Json::makeNumber(static_cast<double>(doomed.size())));
    if ( !lights )
    {
        report.set("cogReferencesCleared", Json::makeNumber(static_cast<double>(referencesCleared)));
    }
    return report;
}

Json setSectorProperties(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sectors", "extraLight", "tint", "sound", "soundVolume" });
    const auto sectors = indexList(operation, "sectors", level.sectors.size());
    const Json* extra = operation.find("extraLight");
    const Json* tint = operation.find("tint");
    const Json* sound = operation.find("sound");
    const Json* volume = operation.find("soundVolume");
    if ( !extra && !tint && !sound && !volume )
    {
        invalid("setSectorProperties needs at least one of extraLight, tint, sound, soundVolume");
    }
    std::optional<sed::ColorF> extraValue, tintValue;
    std::optional<std::string> soundValue;
    std::optional<double> volumeValue;
    if ( extra ) extraValue = lightValue(*extra, "extraLight", 1.0);
    if ( tint ) tintValue = lightValue(*tint, "tint", 1.0);
    if ( sound ) soundValue = sound->type == Json::Type::String && sound->str.empty() ? std::string{} : resourceName(*sound, "sound");
    if ( volume ) volumeValue = bounded(*volume, 0.0, 1.0, "soundVolume");
    for ( const auto s : sectors )
    {
        auto& sector = level.sectors[static_cast<std::size_t>(s)];
        if ( extraValue )
        {
            sector.extraLight.r = extraValue->r;
            sector.extraLight.g = extraValue->g;
            sector.extraLight.b = extraValue->b;
        }
        if ( tintValue )
        {
            sector.tint.r = tintValue->r;
            sector.tint.g = tintValue->g;
            sector.tint.b = tintValue->b;
        }
        if ( soundValue ) sector.sound = *soundValue;
        if ( volumeValue ) sector.soundVolume = *volumeValue;
    }
    Json report = Json::makeObject();
    report.set("sectors", numbers(sectors));
    return report;
}

Json setSurfaceProperties(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "surfaces", "extraLight", "uvScale" });
    const auto surfaces = surfaceList(level, operation);
    const Json* extra = operation.find("extraLight");
    const Json* scale = operation.find("uvScale");
    if ( !extra && !scale )
    {
        invalid("setSurfaceProperties needs extraLight or uvScale");
    }
    std::optional<sed::ColorF> extraValue;
    std::optional<std::array<float, 2>> scaleValue;
    if ( extra ) extraValue = lightValue(*extra, "extraLight", 1.0);
    if ( scale )
    {
        if ( scale->type != Json::Type::Array || scale->array.size() != 2 )
        {
            invalid("'uvScale' must be [u, v]");
        }
        scaleValue = std::array<float, 2>{ static_cast<float>(bounded(scale->array[0], 0.001, 1000.0, "uvScale")),
            static_cast<float>(bounded(scale->array[1], 0.001, 1000.0, "uvScale")) };
    }
    for ( const auto ref : surfaces )
    {
        auto& surface = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)];
        if ( extraValue )
        {
            surface.extraLight.r = extraValue->r;
            surface.extraLight.g = extraValue->g;
            surface.extraLight.b = extraValue->b;
        }
        if ( scaleValue )
        {
            surface.uScale = (*scaleValue)[0];
            surface.vScale = (*scaleValue)[1];
        }
    }
    Json report = Json::makeObject();
    report.set("surfaces", Json::makeNumber(static_cast<double>(surfaces.size())));
    return report;
}

// Unadjoining leaves both faces solid walls. A side that was an undrawn or
// material-less portal would become a hole in the world (the core falls back
// to dflt.mat, which the level may not even list), so each such side takes
// materialIndex, drawn textured and lit, as deleteSectors seals a face.
Json unadjoinSurfaces(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "surfaces", "materialIndex" });
    const auto surfaces = surfaceList(level, operation);
    std::optional<std::int32_t> material;
    if ( const Json* value = operation.find("materialIndex") )
    {
        material = indexValue(*value, "materialIndex", level.materials.size());
    }
    std::vector<sed::SurfaceRef> needsWall;
    for ( const auto ref : surfaces )
    {
        const auto& surface = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)];
        if ( !surface.adjoin )
        {
            refuse("parityNotAdjoined", "Surface " + std::to_string(ref.sector) + ":" + std::to_string(ref.surface)
                + " is not a portal");
        }
        for ( const sed::SurfaceRef side : { ref, *surface.adjoin } )
        {
            if ( side.sector < 0 || static_cast<std::size_t>(side.sector) >= level.sectors.size() || side.surface < 0
                || static_cast<std::size_t>(side.surface) >= level.sectors[static_cast<std::size_t>(side.sector)].surfaces.size() )
            {
                continue;
            }
            const auto& face = level.sectors[static_cast<std::size_t>(side.sector)].surfaces[static_cast<std::size_t>(side.surface)];
            if ( face.material.empty() || face.geo == 0 )
            {
                needsWall.push_back(side);
            }
        }
    }
    if ( !needsWall.empty() && !material )
    {
        refuse("parityUnadjoinNeedsMaterial", "Surface " + std::to_string(needsWall.front().sector) + ":"
            + std::to_string(needsWall.front().surface) + " would be a wall with no drawn material; give materialIndex");
    }
    for ( const auto ref : surfaces )
    {
        if ( level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)].adjoin
            && !sed::level::levelUnadjoinSurface(level, ref) )
        {
            refuse("parityOperationUnsupported", "The shared core could not unadjoin the surface");
        }
    }
    Json sealed = Json::makeArray();
    for ( const auto ref : needsWall )
    {
        auto& face = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)];
        face.material = level.materials[static_cast<std::size_t>(*material)];
        face.nMat = *material;
        face.geo = 4;
        face.light = 3;
        face.tex = 1;
        face.surfaceFlags |= sed::SF_Collision;
        Json row = Json::makeArray();
        row.array = { Json::makeNumber(ref.sector), Json::makeNumber(ref.surface) };
        sealed.array.push_back(std::move(row));
    }
    Json report = Json::makeObject();
    report.set("unadjoined", Json::makeNumber(static_cast<double>(surfaces.size())));
    report.set("sealedFaces", std::move(sealed));
    return report;
}

Json makeUnderwater(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sectors", "tint", "sound", "soundVolume", "waterSurfaces", "waterMaterialIndex" });
    const auto sectors = indexList(operation, "sectors", level.sectors.size());
    sed::level::UnderwaterSettings settings{};
    if ( const Json* tint = operation.find("tint") )
    {
        const auto c = lightValue(*tint, "tint", 1.0);
        settings.tint = sed::ColorF{ c.r, c.g, c.b, settings.tint.a };
    }
    if ( const Json* sound = operation.find("sound") ) settings.sound = resourceName(*sound, "sound");
    if ( const Json* volume = operation.find("soundVolume") ) settings.soundVolume = bounded(*volume, 0.0, 1.0, "soundVolume");
    if ( const Json* water = operation.find("waterSurfaces") )
    {
        if ( water->type != Json::Type::Bool ) invalid("'waterSurfaces' must be a boolean");
        settings.configureWaterSurfaces = water->boolean;
    }
    if ( const Json* materialIndex = operation.find("waterMaterialIndex") )
    {
        settings.waterSurfaceMaterial = level.materials[static_cast<std::size_t>(
            indexValue(*materialIndex, "waterMaterialIndex", level.materials.size()))];
    }
    const auto result = sed::level::levelMakeUnderwaterSectors(level, sectors, settings);
    Json report = Json::makeObject();
    report.set("sectors", Json::makeNumber(static_cast<double>(result.sectors)));
    report.set("boundarySurfaces", Json::makeNumber(static_cast<double>(result.boundarySurfaces)));
    return report;
}

Json placeSectorLights(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sectors", "intensity" });
    const auto sectors = indexList(operation, "sectors", level.sectors.size());
    const double intensity = bounded(field(operation, "intensity", Json::Type::Number), 0.01, 1000.0, "intensity");
    const auto lights = sed::level::levelPlaceSectorLights(level, sectors, intensity);
    Json report = Json::makeObject();
    report.set("lights", numbers(lights));
    report.set("skippedSectors", Json::makeNumber(static_cast<double>(sectors.size() - lights.size())));
    return report;
}

Json removeRedundantVertices(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sectors" });
    const auto sectors = indexList(operation, "sectors", level.sectors.size());
    const auto result = sed::level::levelRemoveRedundantVertices(level, sectors);
    Json report = Json::makeObject();
    report.set("mergedDuplicates", Json::makeNumber(static_cast<double>(result.mergedDuplicates)));
    report.set("removedUnused", Json::makeNumber(static_cast<double>(result.removedUnused)));
    report.set("removedCollinear", Json::makeNumber(static_cast<double>(result.removedCollinear)));
    report.set("skippedSectors", Json::makeNumber(static_cast<double>(result.skippedSectors)));
    return report;
}

} // namespace

bool tryApplyParityOperation(sed::Level& level, const Json& operation, Json& report)
{
    const auto* nameValue = operation.find("op");
    if ( !nameValue || nameValue->type != Json::Type::String )
    {
        return false;
    }
    const std::string& name = nameValue->str;
    constexpr std::string_view names[] = { "deleteThings", "deleteLights", "deleteCogs", "setSectorProperties",
        "setSurfaceProperties", "unadjoinSurfaces", "makeUnderwater", "placeSectorLights", "removeRedundantVertices" };
    if ( std::find(std::begin(names), std::end(names), name) == std::end(names) )
    {
        return false;
    }
    requireEditableKind(level, name);
    if ( name == "deleteThings" || name == "deleteLights" || name == "deleteCogs" ) report = deleteRecords(level, operation, name);
    else if ( name == "setSectorProperties" ) report = setSectorProperties(level, operation);
    else if ( name == "setSurfaceProperties" ) report = setSurfaceProperties(level, operation);
    else if ( name == "unadjoinSurfaces" ) report = unadjoinSurfaces(level, operation);
    else if ( name == "makeUnderwater" ) report = makeUnderwater(level, operation);
    else if ( name == "placeSectorLights" ) report = placeSectorLights(level, operation);
    else report = removeRedundantVertices(level, operation);
    report.set("op", Json::makeString(name));
    return true;
}

} // namespace sed_agent
