#include "agent_parity_operations.hpp"

#include "agent_workspace.hpp"

#include "sed/geometry.hpp"
#include "sed/level.hpp"
#include "sed/level/shadow_cuts.hpp"
#include "sed/level/arch_authoring.hpp"
#include "sed/level/edit.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/merge.hpp"
#include "sed/level/recipe_requests.hpp"
#include "sed/level/recipes.hpp"
#include "sed/level/spatial_selection.hpp"
#include "sed/level/surface_flag_repair.hpp"
#include "sed/level/thing.hpp"
#include "sed/level/transform.hpp"
#include "sed/level/workbench.hpp"

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


// ------------------------------------------------------------------------------------------- second batch

constexpr double kCoordinateLimit = 1000000.0;

sed::TVector vec3(const Json& operation, const std::string_view name, const bool nonzero = false)
{
    const auto& list = field(operation, name, Json::Type::Array).array;
    if ( list.size() != 3 )
    {
        invalid("'" + std::string(name) + "' must be [x, y, z]");
    }
    const sed::TVector v{ bounded(list[0], -kCoordinateLimit, kCoordinateLimit, name),
        bounded(list[1], -kCoordinateLimit, kCoordinateLimit, name),
        bounded(list[2], -kCoordinateLimit, kCoordinateLimit, name) };
    if ( nonzero && !(v.Length() > 1e-12) )
    {
        invalid("'" + std::string(name) + "' must be a nonzero vector");
    }
    return v;
}

sed::SurfaceRef surfaceRef(const sed::Level& level, const Json& operation, const char* sectorName,
    const char* surfaceName)
{
    const auto sector = indexValue(field(operation, sectorName, Json::Type::Number), sectorName, level.sectors.size());
    const auto surface = indexValue(field(operation, surfaceName, Json::Type::Number), surfaceName,
        level.sectors[static_cast<std::size_t>(sector)].surfaces.size());
    return { sector, surface };
}

bool optionalBool(const Json& operation, const std::string_view name, const bool fallback)
{
    const auto* value = operation.find(name);
    if ( !value ) return fallback;
    if ( value->type != Json::Type::Bool )
    {
        invalid("'" + std::string(name) + "' must be a boolean");
    }
    return value->boolean;
}

// Operations that renumber surfaces or sectors refuse once COGs are placed,
// like the cleaves: a COG initializer may address a surface or sector by number.
void requireNoCogs(const sed::Level& level, const std::string& name)
{
    if ( !level.cogs.empty() )
    {
        refuse("parityCogBindings", name + " renumbers surfaces or sectors that placed COG initializers may address; "
            "do it before placing COGs or use the editor");
    }
}

Json surfaceRows(const std::vector<sed::SurfaceRef>& refs)
{
    Json list = Json::makeArray();
    for ( const auto& ref : refs )
    {
        Json pair = Json::makeArray();
        pair.array.push_back(Json::makeNumber(static_cast<double>(ref.sector)));
        pair.array.push_back(Json::makeNumber(static_cast<double>(ref.surface)));
        list.array.push_back(std::move(pair));
    }
    return list;
}

Json flipSurfaces(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "surfaces" });
    const auto surfaces = surfaceList(level, operation);
    sed::level::levelFlipSurfaces(level, surfaces);
    Json report = Json::makeObject();
    report.set("surfaces", Json::makeNumber(static_cast<double>(surfaces.size())));
    return report;
}

Json mirrorSectors(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sectors", "normal", "point" });
    const auto sectors = indexList(operation, "sectors", level.sectors.size());
    const auto normal = vec3(operation, "normal", true).Normalized();
    const auto point = vec3(operation, "point");
    if ( !sed::level::levelFlipSectorsOverPlaneChecked(level, sectors, normal, point.x, point.y, point.z) )
    {
        refuse("parityTransformRefused", "The shared core refused to mirror these sectors over that plane");
    }
    Json report = Json::makeObject();
    report.set("sectors", numbers(sectors));
    return report;
}

Json duplicateSector(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sector", "offset" });
    const auto sector = indexValue(field(operation, "sector", Json::Type::Number), "sector", level.sectors.size());
    const auto offset = vec3(operation, "offset");
    const std::int32_t copy = sed::level::levelDuplicateSector(level, sector, 0.0, 0.0, 0.0,
        sed::TVector{ 1, 0, 0 }, sed::TVector{ 0, 1, 0 }, sed::TVector{ 0, 0, 1 }, offset.x, offset.y, offset.z);
    if ( copy < 0 )
    {
        refuse("parityTransformRefused", "The shared core refused to duplicate the sector");
    }
    Json report = Json::makeObject();
    report.set("sector", Json::makeNumber(static_cast<double>(sector)));
    report.set("newSector", Json::makeNumber(static_cast<double>(copy)));
    return report;
}

Json surfacePair(sed::Level& level, const Json& operation, const std::string& name)
{
    fields(operation, { "op", "sector", "surface", "otherSector", "otherSurface" });
    const auto first = surfaceRef(level, operation, "sector", "surface");
    const auto second = surfaceRef(level, operation, "otherSector", "otherSurface");
    if ( first == second )
    {
        invalid(name + " needs two different surfaces");
    }
    const bool done = name == "fitAndAdjoin" ? sed::level::levelFitAndAdjoinSurfaces(level, first, second)
                                            : sed::level::levelStitchSurfaces(level, first, second);
    if ( !done )
    {
        refuse(name == "fitAndAdjoin" ? "parityFitRefused" : "parityStitchRefused",
            name == "fitAndAdjoin"
                ? "The faces cannot be fitted and adjoined: the moved sector must stay planar and convex, and no moved "
                  "corner may belong to another adjoined surface"
                : "The target face cannot take the source face's texture mapping across their shared edge");
    }
    Json report = Json::makeObject();
    report.set("surface", surfaceRows({ first }).array[0]);
    report.set("otherSurface", surfaceRows({ second }).array[0]);
    return report;
}

Json surfaceAddress(const sed::SurfaceRef ref)
{
    Json address = Json::makeArray();
    address.array = {Json::makeNumber(ref.sector), Json::makeNumber(ref.surface)};
    return address;
}

Json mergeReferenceMap(const sed::level::SurfaceMergeReferenceMap& mapping)
{
    const auto alias = [](const sed::level::SurfaceMergeAlias& pair)
    {
        Json row = Json::makeObject();
        row.set("removed", surfaceAddress(pair.source));
        row.set("survivor", surfaceAddress(pair.survivor));
        row.set("removedGlobalBefore", Json::makeNumber(pair.sourceGlobal));
        row.set("survivorGlobalBefore", Json::makeNumber(pair.survivorGlobalBefore));
        row.set("survivorGlobalAfter", Json::makeNumber(pair.survivorGlobalAfter));
        return row;
    };
    Json steps = Json::makeArray();
    for (const auto& step : mapping.steps)
    {
        Json entry = Json::makeObject();
        entry.set("primary", alias(step.primary));
        if (step.reciprocal) entry.set("reciprocal", alias(*step.reciprocal));
        steps.array.push_back(std::move(entry));
    }
    Json bindings = Json::makeArray();
    for (const auto& binding : mapping.surfaceBindings)
    {
        Json row = Json::makeObject();
        row.set("cog", Json::makeNumber(static_cast<double>(binding.cog)));
        row.set("argument", Json::makeNumber(static_cast<double>(binding.argument)));
        row.set("oldSurface", surfaceAddress(binding.before));
        row.set("newSurface", surfaceAddress(binding.after));
        row.set("oldGlobal", Json::makeNumber(binding.globalBefore));
        row.set("newGlobal", Json::makeNumber(binding.globalAfter));
        bindings.array.push_back(std::move(row));
    }
    Json result = Json::makeObject();
    result.set("kind", Json::makeString("surfaceMergeV1"));
    result.set("steps", std::move(steps));
    result.set("surfaceCogBindings", std::move(bindings));
    return result;
}

Json mergeSurfaces(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "surfaces" });
    const auto surfaces = surfaceList(level, operation);
    std::string blocker;
    sed::level::SurfaceMergeReferenceMap mapping;
    const auto result = sed::level::levelMergeSelectedSurfaces(level, surfaces,
        sed::level::SurfaceMergePolicy::keepSurvivorMapping, {}, mapping, &blocker);
    if (!blocker.empty()) refuse("parityOpaqueSurfaceReference", blocker);
    if ( !result || result->mergeCount == 0 )
    {
        refuse("parityMergeRefused", "No two of these faces share an edge in one sector and can merge into one convex face");
    }
    Json report = Json::makeObject();
    report.set("merges", Json::makeNumber(static_cast<double>(result->mergeCount)));
    report.set("surfaces", surfaceRows(result->selection));
    report.set("referenceMap", mergeReferenceMap(mapping));
    return report;
}

Json mergeSectors(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sector", "otherSector" });
    requireNoCogs(level, "mergeSectors");
    const auto sector = indexValue(field(operation, "sector", Json::Type::Number), "sector", level.sectors.size());
    const auto other = indexValue(field(operation, "otherSector", Json::Type::Number), "otherSector", level.sectors.size());
    if ( sector == other )
    {
        invalid("mergeSectors needs two different sectors");
    }
    const auto merged = sed::level::levelMergeSectors(level, sector, other);
    if ( !merged )
    {
        refuse("parityMergeRefused", "The sectors must be adjoined and merge into one closed convex sector");
    }
    Json report = Json::makeObject();
    report.set("sector", Json::makeNumber(static_cast<double>(*merged)));
    return report;
}

Json subdivideSurfaces(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "surfaces" });
    requireNoCogs(level, "subdivideSurfaces");
    const auto surfaces = surfaceList(level, operation);
    const std::size_t done = sed::level::levelSubdivideSurfaces(level, surfaces);
    Json report = Json::makeObject();
    report.set("subdivided", Json::makeNumber(static_cast<double>(done)));
    report.set("skipped", Json::makeNumber(static_cast<double>(surfaces.size() - done)));
    return report;
}

Json shadowCuts(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "mode", "lights", "maxEdge", "minPiece", "maxNewSurfaces", "maxDepth" });
    requireNoCogs(level, "shadowCuts");
    sed::level::ShadowCutOptions options;
    if ( const auto* mode = operation.find("mode") )
    {
        if ( mode->type != Json::Type::String || (mode->str != "shadowEdges" && mode->str != "sizeLimit") )
        {
            invalid("shadowCuts mode is shadowEdges or sizeLimit");
        }
        options.mode = mode->str == "sizeLimit" ? sed::level::ShadowCutMode::sizeLimit : sed::level::ShadowCutMode::shadowEdges;
    }
    if ( const auto* lights = operation.find("lights") )
    {
        if ( lights->type != Json::Type::Array || lights->array.empty() ) invalid("shadowCuts lights must be a non-empty array");
        for ( const Json& light : lights->array )
        {
            options.lights.push_back(static_cast<std::int32_t>(indexValue(light, "lights", level.lights.size())));
        }
    }
    if ( const auto* value = operation.find("maxEdge") ) options.maxEdge = bounded(*value, 0.001, 1000.0, "maxEdge");
    if ( const auto* value = operation.find("minPiece") ) options.minPiece = bounded(*value, 0.0, 10.0, "minPiece");
    if ( const auto* value = operation.find("maxNewSurfaces") )
    {
        options.maxNewSurfaces = static_cast<std::int32_t>(bounded(*value, 0.0, 100000.0, "maxNewSurfaces"));
    }
    if ( const auto* value = operation.find("maxDepth") )
    {
        options.maxDepth = static_cast<std::int32_t>(bounded(*value, 0.0, 64.0, "maxDepth"));
    }
    const sed::level::ShadowCutResult result = sed::level::levelShadowCuts(level, options);
    if ( result.refusal == "overBudget" )
    {
        refuse("shadowCutsOverBudget", "The cuts would add " + std::to_string(result.newSurfaces)
            + " surfaces or more, over maxNewSurfaces (" + std::to_string(options.maxNewSurfaces)
            + "); raise it, pick fewer lights, or use a larger maxEdge");
    }
    if ( !result.ok ) invalid("shadowCuts options are out of range");
    Json report = Json::makeObject();
    report.set("mode", Json::makeString(options.mode == sed::level::ShadowCutMode::sizeLimit ? "sizeLimit" : "shadowEdges"));
    report.set("plannedCuts", Json::makeNumber(result.plannedCuts));
    report.set("surfacesCut", Json::makeNumber(result.surfacesCut));
    report.set("newSurfaces", Json::makeNumber(result.newSurfaces));
    report.set("refusedCuts", Json::makeNumber(result.refusedCuts));
    report.set("cornerLimitSkips", Json::makeNumber(result.cornerLimitSkips));
    report.set("lightsOutside", Json::makeNumber(result.lightsOutside));
    report.set("walkTruncated", Json::makeBool(result.walkTruncated));
    report.set("pieces", surfaceRows(result.pieces));
    return report;
}

Json removeShadowCuts(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sectors" });
    requireNoCogs(level, "removeShadowCuts");
    std::vector<std::int32_t> sectors;
    if ( const auto* list = operation.find("sectors") )
    {
        if ( list->type != Json::Type::Array || list->array.empty() ) invalid("removeShadowCuts sectors must be a non-empty array");
        for ( const Json& sector : list->array )
        {
            sectors.push_back(static_cast<std::int32_t>(indexValue(sector, "sectors", level.sectors.size())));
        }
    }
    const sed::level::ShadowCutRemoval removal = sed::level::levelRemoveShadowCuts(level, sectors);
    Json report = Json::makeObject();
    report.set("merges", Json::makeNumber(removal.merges));
    report.set("sectorsChanged", Json::makeNumber(removal.sectorsChanged));
    return report;
}

Json insetSurface(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sector", "surface", "factor", "adjoinPair" });
    const auto ref = surfaceRef(level, operation, "sector", "surface");
    const double factor = bounded(field(operation, "factor", Json::Type::Number), 1e-6, 1.0 - 1e-6, "factor");
    const bool pair = optionalBool(operation, "adjoinPair", false);
    const bool done = pair ? sed::level::levelInsetAdjoinPair(level, ref, factor)
                           : sed::level::levelInsetSurface(level, ref, factor);
    if ( !done )
    {
        refuse("parityInsetRefused", pair
            ? "The adjoin pair cannot be inset: another adjoined face would change, or the two sides would stop matching"
            : "The face cannot be inset: it is adjoined (use adjoinPair), or the result would change another adjoined "
              "face or break planarity or convexity");
    }
    Json report = Json::makeObject();
    report.set("surface", surfaceRows({ ref }).array[0]);
    report.set("factor", Json::makeNumber(factor));
    return report;
}

Json planarizeSurfaces(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "surfaces" });
    const auto surfaces = surfaceList(level, operation);
    const auto result = sed::level::levelPlanarizeSurfaces(level, surfaces);
    Json report = Json::makeObject();
    report.set("changed", Json::makeBool(result.changed));
    report.set("stillNonPlanar", Json::makeNumber(static_cast<double>(result.stillNonPlanar)));
    return report;
}

Json repairSurfaces(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "surfaces", "solid", "passable" });
    const auto surfaces = surfaceList(level, operation);
    const bool solid = optionalBool(operation, "solid", true);
    const bool passable = optionalBool(operation, "passable", true);
    if ( !solid && !passable )
    {
        invalid("repairSurfaces needs solid, passable or both");
    }
    Json report = Json::makeObject();
    report.set("solidRepaired", Json::makeNumber(static_cast<double>(
        solid ? sed::level::levelFixSolidSurfaces(level, surfaces) : 0u)));
    report.set("passableRepaired", Json::makeNumber(static_cast<double>(
        passable ? sed::level::levelFixPassableAdjoins(level, surfaces) : 0u)));
    return report;
}

// After a rotation or a mirror each Thing must lie strictly inside exactly one sector, which becomes its sector.
void rehomeThings(sed::Level& level, const std::vector<std::int32_t>& things)
{
    for ( const std::int32_t index : things )
    {
        auto& thing = level.things[static_cast<std::size_t>(index)];
        const sed::TVector position{ thing.x, thing.y, thing.z };
        const auto selection = sed::level::selectSectorAtPoint(level, {}, position);
        if ( selection.match != sed::level::SpatialMatch::unique )
        {
            refuse("parityThingOutsideLevel", "Thing " + std::to_string(index)
                + " would not lie strictly inside exactly one sector after the transform");
        }
        thing.sector = selection.target.sector;
        if ( const auto problem = sed::level::validatePlacementPointInsideSector(level, position, thing.sector) )
        {
            refuse("parityThingOutsideLevel", problem->message);
        }
    }
}

std::vector<std::int32_t> movableThings(const sed::Level& level, const Json& operation)
{
    const auto things = indexList(operation, "things", level.things.size());
    for ( const std::int32_t index : things )
    {
        const auto& thing = level.things[static_cast<std::size_t>(index)];
        if ( thing.archLightingIndex >= 0
            || std::any_of(level.motsArchAuthoring.begin(), level.motsArchAuthoring.end(),
                [index](const auto& entry) { return entry.thingIndex == index; }) )
        {
            refuse("parityThingHasArchitecture", "Clear Thing " + std::to_string(index)
                + "'s ARCH binding before moving it");
        }
        if ( sed::level::thingCountFrames(thing) != 0 )
        {
            refuse("parityThingHasFrames", "Thing " + std::to_string(index)
                + " has path frames; move it with translateThings, which moves its frames too");
        }
    }
    return things;
}

Json rotateRecords(sed::Level& level, const Json& operation, const std::string& name)
{
    const bool things = name == "rotateThings";
    if ( things ) fields(operation, { "op", "things", "pivot", "angleDegrees", "axis", "orientation" });
    else fields(operation, { "op", "lights", "pivot", "angleDegrees", "axis" });
    const auto indices = things ? movableThings(level, operation) : indexList(operation, "lights", level.lights.size());
    const auto pivot = vec3(operation, "pivot");
    const double angle = bounded(field(operation, "angleDegrees", Json::Type::Number), -36000.0, 36000.0, "angleDegrees");
    const auto axis = operation.find("axis") ? vec3(operation, "axis", true).Normalized() : sed::TVector{ 0, 0, 1 };
    if ( things )
    {
        sed::level::levelRotateThings(level, indices, axis, pivot.x, pivot.y, pivot.z, angle,
            optionalBool(operation, "orientation", true));
        rehomeThings(level, indices);
    }
    else
    {
        sed::level::levelRotateLights(level, indices, axis, pivot.x, pivot.y, pivot.z, angle);
    }
    Json report = Json::makeObject();
    report.set(things ? "things" : "lights", numbers(indices));
    return report;
}

Json mirrorRecords(sed::Level& level, const Json& operation, const std::string& name)
{
    const bool things = name == "mirrorThings";
    fields(operation, { "op", things ? "things" : "lights", "normal", "point" });
    const auto indices = things ? movableThings(level, operation) : indexList(operation, "lights", level.lights.size());
    const auto normal = vec3(operation, "normal", true).Normalized();
    const auto point = vec3(operation, "point");
    if ( things )
    {
        sed::level::levelFlipThingsOverPlane(level, indices, normal, point.x, point.y, point.z);
        rehomeThings(level, indices);
    }
    else
    {
        sed::level::levelFlipLightsOverPlane(level, indices, normal, point.x, point.y, point.z);
    }
    Json report = Json::makeObject();
    report.set(things ? "things" : "lights", numbers(indices));
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
        "setSurfaceProperties", "unadjoinSurfaces", "makeUnderwater", "placeSectorLights", "removeRedundantVertices",
        "flipSurfaces", "mirrorSectors", "duplicateSector", "fitAndAdjoin", "stitchSurfaces", "mergeSurfaces",
        "mergeSectors", "subdivideSurfaces", "insetSurface", "planarizeSurfaces", "repairSurfaces", "rotateThings",
        "rotateLights", "mirrorThings", "mirrorLights", "shadowCuts", "removeShadowCuts" };
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
    else if ( name == "removeRedundantVertices" ) report = removeRedundantVertices(level, operation);
    else if ( name == "flipSurfaces" ) report = flipSurfaces(level, operation);
    else if ( name == "mirrorSectors" ) report = mirrorSectors(level, operation);
    else if ( name == "duplicateSector" ) report = duplicateSector(level, operation);
    else if ( name == "fitAndAdjoin" || name == "stitchSurfaces" ) report = surfacePair(level, operation, name);
    else if ( name == "mergeSurfaces" ) report = mergeSurfaces(level, operation);
    else if ( name == "mergeSectors" ) report = mergeSectors(level, operation);
    else if ( name == "subdivideSurfaces" ) report = subdivideSurfaces(level, operation);
    else if ( name == "insetSurface" ) report = insetSurface(level, operation);
    else if ( name == "shadowCuts" ) report = shadowCuts(level, operation);
    else if ( name == "removeShadowCuts" ) report = removeShadowCuts(level, operation);
    else if ( name == "planarizeSurfaces" ) report = planarizeSurfaces(level, operation);
    else if ( name == "repairSurfaces" ) report = repairSurfaces(level, operation);
    else if ( name == "rotateThings" || name == "rotateLights" ) report = rotateRecords(level, operation, name);
    else report = mirrorRecords(level, operation, name);
    report.set("op", Json::makeString(name));
    return true;
}

} // namespace sed_agent
