#include "../agent_semantic_preview.hpp"

#include "sed/level.hpp"

#include <bit>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using sed_agent::Json;
using sed_agent::SemanticPreviewLimits;

int failures{};
std::string currentCase;

bool check(bool condition, std::string_view message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL [" << currentCase << "]: " << message << '\n';
    }
    return condition;
}

const Json& property(const Json& object, std::string_view name)
{
    static const Json absent;
    const auto* result = object.find(name);
    return result ? *result : absent;
}

bool boolean(const Json& object, std::string_view name, bool expected)
{
    const auto& result = property(object, name);
    return result.type == Json::Type::Bool && result.boolean == expected;
}

bool number(const Json& object, std::string_view name, double expected)
{
    const auto& result = property(object, name);
    return result.type == Json::Type::Number && result.number == expected;
}

bool string(const Json& object, std::string_view name, std::string_view expected)
{
    const auto& result = property(object, name);
    return result.type == Json::Type::String && result.str == expected;
}

const Json* view(const Json& preview, std::string_view name = "authored")
{
    const auto* result = preview.find(name);
    if ( !check(result && result->isObject(), "buildSemanticPreview supplies the requested result view") )
    {
        return nullptr;
    }
    return result;
}

const Json* row(const Json& result, std::string_view path)
{
    for ( const auto& change : property(result, "changes").array )
    {
        if ( string(change, "path", path) )
        {
            return &change;
        }
    }
    return nullptr;
}

const Json* requireRow(const Json& result, std::string_view path)
{
    const auto* found = row(result, path);
    check(found != nullptr, std::string("change row exists at ") + std::string(path));
    return found;
}

bool hasReason(const Json& result, std::string_view reason)
{
    for ( const auto& entry : property(result, "reasons").array )
    {
        if ( entry.type == Json::Type::String && entry.str == reason )
        {
            return true;
        }
    }
    return false;
}

void completeCount(const Json& result, double count, bool details = true)
{
    check(boolean(result, "comparisonComplete", true), "comparison finishes");
    check(boolean(result, "detailsComplete", details), "detail completeness is truthful");
    check(boolean(result, "changed", count != 0), "complete changed flag is exact");
    check(number(result, "observedChanges", count) && number(result, "totalChanges", count),
        "complete change count is exact");
}

void scalar(const Json& change, std::string_view side, std::string_view type, double value)
{
    const auto& descriptor = property(change, side);
    check(boolean(descriptor, "present", true) && string(descriptor, "type", type)
            && number(descriptor, "value", value), "scalar descriptor preserves storage type and exact value");
}

void absent(const Json& change, std::string_view side)
{
    const auto& descriptor = property(change, side);
    check(boolean(descriptor, "present", false) && !descriptor.find("value"), "absence has no invented value");
}

Json compare(const sed::Level& source, const sed::Level& candidate, const SemanticPreviewLimits& limits = {})
{
    return sed_agent::buildSemanticPreview(source, candidate, source, "source-bytes", "output-bytes", limits);
}

sed::Level populatedLevel()
{
    sed::Level level;
    level.sectors.resize(1);
    level.sectors[0].vertices.resize(1);
    level.sectors[0].surfaces.resize(1);
    level.sectors[0].surfaces[0].vertices.push_back(0);
    level.sectors[0].surfaces[0].texVertices.resize(1);
    level.cogs.resize(1);
    level.cogs[0].values.resize(1);
    level.templates.resize(1);
    level.templates[0].values.resize(1);
    level.things.resize(1);
    level.things[0].values.resize(1);
    level.motsArchLighting.resize(1);
    level.motsArchLighting[0].meshes.resize(1);
    level.motsArchLighting[0].meshes[0].vertices.resize(1);
    level.motsArchLightingBakeBaselines.resize(1);
    level.motsArchLightingBakeBaselines[0].originalArchLighting.emplace();
    level.motsArchLightingBakeBaselines[0].originalArchLighting->meshes.resize(1);
    level.motsArchLightingBakeBaselines[0].originalArchLighting->meshes[0].vertices.resize(1);
    level.motsArchAuthoring.resize(1);
    level.lights.resize(1);
    level.h3doNodes.resize(1);
    level.colormaps.push_back("old.cmp");
    level.materials.push_back("old.mat");
    level.layers.push_back("old");
    return level;
}

void noOpAndIndependentViews()
{
    const sed::Level source;
    auto candidate = source;
    auto persisted = source;
    candidate.header.gravity = 7.0;
    persisted.ppunit = 64.0;
    const auto preview = sed_agent::buildSemanticPreview(source, candidate, persisted,
        "input-exact-byte-hash", "output-exact-byte-hash");
    const auto* authored = view(preview);
    const auto* output = view(preview, "persisted");
    if ( !authored || !output )
    {
        return;
    }
    check(number(preview, "version", 1) && string(preview, "coverage", "level-model-v1")
            && string(preview, "addressing", "snapshot-storage-slots"), "coverage and address metadata are explicit");
    check(string(preview, "inputSha256", "input-exact-byte-hash")
            && string(preview, "outputSha256", "output-exact-byte-hash"), "byte identities pass through exactly");
    check(string(*authored, "beforeSnapshot", "source") && string(*authored, "afterSnapshot", "candidate")
            && string(*output, "beforeSnapshot", "source") && string(*output, "afterSnapshot", "output"),
        "both views compare directly against the source");
    completeCount(*authored, 1);
    completeCount(*output, 1);
    check(row(*authored, "/header/gravity") && !row(*authored, "/ppunit")
            && row(*output, "/ppunit") && !row(*output, "/header/gravity"), "views do not subtract or contaminate each other");
    const auto unchanged = compare(source, source);
    if ( const auto* same = view(unchanged) )
    {
        completeCount(*same, 0);
        check(property(*same, "changes").array.empty() && property(*same, "reasons").array.empty(),
            "no-op has no fabricated records or truncation reasons");
        check(number(property(*same, "work"), "visitedSlots", 68),
            "an empty Level still admits its records, arrays and fields");
    }
}

void coveredDomainsAndRawScalarTypes()
{
    struct Change
    {
        const char* path;
        const char* type;
        double before;
        double after;
        std::function<void(sed::Level&)> mutate;
    };
    const std::vector<Change> cases{
        { "/header/version", "int32", 0, -2, [](auto& l) { l.header.version = -2; } },
        { "/header/horizonSky/offset/y", "float64", 0, 3, [](auto& l) { l.header.horizonSky.offset.y = 3; } },
        { "/header/ceilingSky/height", "float64", 0, 4, [](auto& l) { l.header.ceilingSky.height = 4; } },
        { "/header/mipmapDistances/3", "float64", 0, 9, [](auto& l) { l.header.mipmapDistances[3] = 9; } },
        { "/header/lodDistances/2", "float64", 0, 8, [](auto& l) { l.header.lodDistances[2] = 8; } },
        { "/header/perspectiveDistance", "float64", 0, 6, [](auto& l) { l.header.perspectiveDistance = 6; } },
        { "/header/gouraudDistance", "float64", 0, 5, [](auto& l) { l.header.gouraudDistance = 5; } },
        { "/header/fog/color/a", "float32", 1, 0.5, [](auto& l) { l.header.fog.color.a = 0.5f; } },
        { "/header/fog/fogStart", "float64", 0, 2, [](auto& l) { l.header.fog.fogStart = 2; } },
        { "/header/fog/fogEnd", "float64", 0, 12, [](auto& l) { l.header.fog.fogEnd = 12; } },
        { "/exportBufferExtras/things", "uint32", 0, 4294967295.0, [](auto& l) { l.exportBufferExtras.things = 0xffffffffu; } },
        { "/kind", "enum", 0, -17, [](auto& l) { l.kind = static_cast<sed::ProjectType>(-17); } },
        { "/uvCoordinateSpace", "enum", 2, 255, [](auto& l) { l.uvCoordinateSpace = static_cast<sed::UvCoordinateSpace>(255); } },
        { "/sectors/0/flags", "int32", 0, -2147483648.0, [](auto& l) { l.sectors[0].flags = std::numeric_limits<std::int32_t>::min(); } },
        { "/sectors/0/ambient/a", "float32", 1, 0.5, [](auto& l) { l.sectors[0].ambient.a = 0.5f; } },
        { "/sectors/0/pointLight/color/a", "float32", 1, 0.25, [](auto& l) { l.sectors[0].pointLight.color.a = 0.25f; } },
        { "/sectors/0/pointLight/position/z", "float64", 0, 4, [](auto& l) { l.sectors[0].pointLight.position.z = 4; } },
        { "/sectors/0/pointLight/maxRange", "float64", 0, 9, [](auto& l) { l.sectors[0].pointLight.maxRange = 9; } },
        { "/sectors/0/colormapIndex", "int32", -1, 7, [](auto& l) { l.sectors[0].colormapIndex = 7; } },
        { "/sectors/0/soundVolume", "float64", 0, 0.5, [](auto& l) { l.sectors[0].soundVolume = 0.5; } },
        { "/sectors/0/thrust/x", "float64", 0, -3, [](auto& l) { l.sectors[0].thrust.x = -3; } },
        { "/sectors/0/collideBox/z2", "float64", 0, 6, [](auto& l) { l.sectors[0].collideBox.z2 = 6; } },
        { "/sectors/0/mark", "int32", 0, 17, [](auto& l) { l.sectors[0].mark = 17; } },
        { "/sectors/0/vertices/0/z", "float64", 0, 7, [](auto& l) { l.sectors[0].vertices[0].z = 7; } },
        { "/sectors/0/vertices/0/mark", "int32", 0, 12, [](auto& l) { l.sectors[0].vertices[0].mark = 12; } },
        { "/sectors/0/surfaces/0/surfaceFlags", "int32", 0, -1, [](auto& l) { l.sectors[0].surfaces[0].surfaceFlags = -1; } },
        { "/sectors/0/surfaces/0/faceFlags", "uint32", 0, 4294967295.0, [](auto& l) { l.sectors[0].surfaces[0].faceFlags = 0xffffffffu; } },
        { "/sectors/0/surfaces/0/adjoinFlags", "int32", 0, -7, [](auto& l) { l.sectors[0].surfaces[0].adjoinFlags = -7; } },
        { "/sectors/0/surfaces/0/nAdj", "int32", 0, -9, [](auto& l) { l.sectors[0].surfaces[0].nAdj = -9; } },
        { "/sectors/0/surfaces/0/nMat", "int32", 0, 2, [](auto& l) { l.sectors[0].surfaces[0].nMat = 2; } },
        { "/sectors/0/surfaces/0/uScale", "float32", 1, 2, [](auto& l) { l.sectors[0].surfaces[0].uScale = 2; } },
        { "/sectors/0/surfaces/0/normal/y", "float64", 0, -1, [](auto& l) { l.sectors[0].surfaces[0].normal.y = -1; } },
        { "/sectors/0/surfaces/0/vertices/0", "int32", 0, 4, [](auto& l) { l.sectors[0].surfaces[0].vertices[0] = 4; } },
        { "/sectors/0/surfaces/0/texVertices/0/u", "float32", 0, 16, [](auto& l) { l.sectors[0].surfaces[0].texVertices[0].u = 16; } },
        { "/sectors/0/surfaces/0/texVertices/0/color/a", "float32", 1, 0.125, [](auto& l) { l.sectors[0].surfaces[0].texVertices[0].color.a = 0.125f; } },
        { "/things/0/x", "float64", 0, 2, [](auto& l) { l.things[0].x = 2; } },
        { "/things/0/rol", "float64", 0, 25, [](auto& l) { l.things[0].rol = 25; } },
        { "/things/0/archLightingIndex", "int32", -1, 4, [](auto& l) { l.things[0].archLightingIndex = 4; } },
        { "/things/0/bbox/x1", "float64", 0, -2, [](auto& l) { l.things[0].bbox.x1 = -2; } },
        { "/things/0/values/0/intValue", "int32", 0, -13, [](auto& l) { l.things[0].values[0].intValue = -13; } },
        { "/things/0/values/0/floatValue", "float64", 0, 0.25, [](auto& l) { l.things[0].values[0].floatValue = 0.25; } },
        { "/things/0/values/0/adjType", "enum", 0, -9, [](auto& l) { l.things[0].values[0].adjType = static_cast<sed::AdjType>(-9); } },
        { "/templates/0/values/0/intValue", "int32", 0, 5, [](auto& l) { l.templates[0].values[0].intValue = 5; } },
        { "/templates/0/bbox/y2", "float64", 0, 3, [](auto& l) { l.templates[0].bbox.y2 = 3; } },
        { "/cogs/0/values/0/mask", "int32", 0, -2147483648.0, [](auto& l) { l.cogs[0].values[0].mask = std::numeric_limits<std::int32_t>::min(); } },
        { "/cogs/0/values/0/cogType", "enum", 0, -3, [](auto& l) { l.cogs[0].values[0].cogType = static_cast<sed::CogType>(-3); } },
        { "/cogs/0/values/0/intValue", "int32", 0, 23, [](auto& l) { l.cogs[0].values[0].intValue = 23; } },
        { "/motsArchLighting/0/wireIndex", "int32", 0, 11, [](auto& l) { l.motsArchLighting[0].wireIndex = 11; } },
        { "/motsArchLighting/0/meshes/0/wireIndex", "int32", 0, 17, [](auto& l) { l.motsArchLighting[0].meshes[0].wireIndex = 17; } },
        { "/motsArchLighting/0/meshes/0/vertices/0/mono", "float32", 0, 0.5, [](auto& l) { l.motsArchLighting[0].meshes[0].vertices[0].mono = 0.5f; } },
        { "/motsArchLighting/0/meshes/0/vertices/0/blue", "float32", 0, 0.25, [](auto& l) { l.motsArchLighting[0].meshes[0].vertices[0].blue = 0.25f; } },
        { "/motsArchLightingBakeBaselines/0/thingId", "int32", 0, 27, [](auto& l) { l.motsArchLightingBakeBaselines[0].thingId = 27; } },
        { "/motsArchLightingBakeBaselines/0/originalArchLightingIndex", "int32", -1, 5, [](auto& l) { l.motsArchLightingBakeBaselines[0].originalArchLightingIndex = 5; } },
        { "/motsArchLightingBakeBaselines/0/originalArchLighting/wireIndex", "int32", 0, 19, [](auto& l) { l.motsArchLightingBakeBaselines[0].originalArchLighting->wireIndex = 19; } },
        { "/motsArchLightingBakeBaselines/0/originalArchLighting/meshes/0/vertices/0/green", "float32", 0, 0.5, [](auto& l) { l.motsArchLightingBakeBaselines[0].originalArchLighting->meshes[0].vertices[0].green = 0.5f; } },
        { "/motsArchAuthoring/0/thingIndex", "int32", -1, 7, [](auto& l) { l.motsArchAuthoring[0].thingIndex = 7; } },
        { "/motsArchAuthoring/0/origin", "enum", 0, 4, [](auto& l) { l.motsArchAuthoring[0].origin = sed::MotsArchSampleOrigin::BakedWorldOccludedV1; } },
        { "/lights/0/flags", "int32", 0, -2, [](auto& l) { l.lights[0].flags = -2; } },
        { "/lights/0/intensity", "float64", 10, 12, [](auto& l) { l.lights[0].intensity = 12; } },
        { "/lights/0/color/a", "float32", 1, 0.25, [](auto& l) { l.lights[0].color.a = 0.25f; } },
        { "/h3doNodes/0/type", "uint32", 1, 4294967295.0, [](auto& l) { l.h3doNodes[0].type = 0xffffffffu; } },
        { "/h3doNodes/0/mesh", "int32", -1, 3, [](auto& l) { l.h3doNodes[0].mesh = 3; } },
        { "/h3doNodes/0/parent", "int32", -1, 4, [](auto& l) { l.h3doNodes[0].parent = 4; } },
        { "/h3doNodes/0/rotation/y", "float64", 0, 9, [](auto& l) { l.h3doNodes[0].rotation.y = 9; } },
        { "/h3doNodes/0/pivot/z", "float64", 0, -3, [](auto& l) { l.h3doNodes[0].pivot.z = -3; } },
        { "/ppunit", "float64", 320, 64, [](auto& l) { l.ppunit = 64; } },
        { "/curId", "int32", 1, 42, [](auto& l) { l.curId = 42; } }
    };
    const auto source = populatedLevel();
    for ( const auto& test : cases )
    {
        auto candidate = source;
        test.mutate(candidate);
        const auto preview = compare(source, candidate);
        const auto* result = view(preview);
        if ( !result )
        {
            return;
        }
        completeCount(*result, 1);
        if ( const auto* changed = requireRow(*result, test.path) )
        {
            check(string(*changed, "kind", "value"), "stored scalar is a value change");
            scalar(*changed, "before", test.type, test.before);
            scalar(*changed, "after", test.type, test.after);
        }
    }
}

void stringsAndProvenanceStayExact()
{
    struct Change
    {
        const char* path;
        std::function<void(sed::Level&)> mutate;
    };
    const std::vector<Change> cases{
        { "/sectors/0/colormap", [](auto& l) { l.sectors[0].colormap = "new"; } },
        { "/sectors/0/sound", [](auto& l) { l.sectors[0].sound = "new"; } },
        { "/sectors/0/surfaces/0/material", [](auto& l) { l.sectors[0].surfaces[0].material = "new"; } },
        { "/things/0/templateName", [](auto& l) { l.things[0].templateName = "new"; } },
        { "/things/0/name", [](auto& l) { l.things[0].name = "new"; } },
        { "/things/0/values/0/stringValue", [](auto& l) { l.things[0].values[0].stringValue = "new"; } },
        { "/templates/0/name", [](auto& l) { l.templates[0].name = "new"; } },
        { "/templates/0/parent", [](auto& l) { l.templates[0].parent = "new"; } },
        { "/templates/0/desc", [](auto& l) { l.templates[0].desc = "new"; } },
        { "/cogs/0/name", [](auto& l) { l.cogs[0].name = "new"; } },
        { "/cogs/0/values/0/name", [](auto& l) { l.cogs[0].values[0].name = "new"; } },
        { "/cogs/0/values/0/stringValue", [](auto& l) { l.cogs[0].values[0].stringValue = "new"; } },
        { "/cogs/0/values/0/description", [](auto& l) { l.cogs[0].values[0].description = "new"; } },
        { "/motsArchLightingBakeBaselines/0/modelName", [](auto& l) { l.motsArchLightingBakeBaselines[0].modelName = "new"; } },
        { "/motsArchAuthoring/0/modelName", [](auto& l) { l.motsArchAuthoring[0].modelName = "new"; } },
        { "/motsArchAuthoring/0/modelEntrySha256Hex", [](auto& l) { l.motsArchAuthoring[0].modelEntrySha256Hex = "new"; } },
        { "/motsArchAuthoring/0/bakeInputSha256Hex", [](auto& l) { l.motsArchAuthoring[0].bakeInputSha256Hex = "new"; } },
        { "/motsArchAuthoring/0/sampleSha256Hex", [](auto& l) { l.motsArchAuthoring[0].sampleSha256Hex = "new"; } },
        { "/motsArchAuthoring/0/bakeProfile", [](auto& l) { l.motsArchAuthoring[0].bakeProfile = "new"; } },
        { "/h3doNodes/0/name", [](auto& l) { l.h3doNodes[0].name = "new"; } },
        { "/colormaps/0", [](auto& l) { l.colormaps[0] = "new"; } },
        { "/materials/0", [](auto& l) { l.materials[0] = "new"; } },
        { "/layers/0", [](auto& l) { l.layers[0] = "new"; } },
        { "/masterCMP", [](auto& l) { l.masterCMP = "new"; } },
        { "/lvisstring", [](auto& l) { l.lvisstring = "new"; } }
    };
    const auto source = populatedLevel();
    for ( const auto& test : cases )
    {
        auto candidate = source;
        test.mutate(candidate);
        const auto preview = compare(source, candidate);
        const auto* result = view(preview);
        if ( !result )
        {
            return;
        }
        completeCount(*result, 1);
        if ( const auto* changed = requireRow(*result, test.path) )
        {
            const auto& after = property(*changed, "after");
            check(string(after, "type", "string") && string(after, "value", "new")
                    && string(after, "encoding", "utf8") && number(after, "byteCount", 3),
                "string storage is exact and byte-counted");
        }
    }
}

void optionalScalarsAndBooleanStorage()
{
    const auto source = populatedLevel();
    auto candidate = source;
    candidate.lights[0].rgbIntensity = 0.0;
    candidate.lights[0].legacyRgbRange = -4.0;
    candidate.cogs[0].values[0].explicitLinkId = 0;
    candidate.cogs[0].values[0].explicitMask = 0xffffffffu;
    candidate.cogs[0].values[0].noLink = true;
    candidate.cogs[0].values[0].local = true;
    candidate.header.fog.enabled = true;
    candidate.hasSourceLightMetadata = true;
    const auto preview = compare(source, candidate);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    completeCount(*result, 8);
    for ( const auto path : { "/lights/0/rgbIntensity", "/lights/0/legacyRgbRange",
        "/cogs/0/values/0/explicitLinkId", "/cogs/0/values/0/explicitMask" } )
    {
        if ( const auto* change = requireRow(*result, path) )
        {
            check(string(*change, "kind", "presence"), "optional scalar engagement is one presence row");
            absent(*change, "before");
        }
    }
    if ( const auto* change = requireRow(*result, "/lights/0/rgbIntensity") )
    {
        scalar(*change, "after", "float64", 0);
    }
    if ( const auto* change = requireRow(*result, "/cogs/0/values/0/explicitLinkId") )
    {
        scalar(*change, "after", "int32", 0);
    }
    if ( const auto* change = requireRow(*result, "/cogs/0/values/0/explicitMask") )
    {
        scalar(*change, "after", "uint32", 4294967295.0);
    }
    for ( const auto path : { "/cogs/0/values/0/noLink", "/cogs/0/values/0/local",
        "/header/fog/enabled", "/hasSourceLightMetadata" } )
    {
        if ( const auto* change = requireRow(*result, path) )
        {
            check(string(property(*change, "before"), "type", "bool")
                    && boolean(property(*change, "before"), "value", false)
                    && boolean(property(*change, "after"), "value", true), "stored bool is not inferred or integer-coerced");
        }
    }
    const auto engagedSource = candidate;
    candidate.lights[0].rgbIntensity = 10.0;
    const auto changedValue = compare(engagedSource, candidate);
    if ( const auto* changed = view(changedValue) )
    {
        completeCount(*changed, 1);
        if ( const auto* change = requireRow(*changed, "/lights/0/rgbIntensity") )
        {
            check(string(*change, "kind", "value"), "engaged optional scalar uses a value row");
            scalar(*change, "before", "float64", 0);
            scalar(*change, "after", "float64", 10);
        }
    }
}

void optionalCompositePresenceAndMissingOwner()
{
    sed::Level source;
    source.sectors.resize(1);
    source.sectors[0].surfaces.resize(1);
    auto candidate = source;
    candidate.sectors[0].surfaces[0].adjoin = sed::SurfaceRef{ 2, 3 };
    const auto preview = compare(source, candidate);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    completeCount(*result, 3);
    if ( const auto* change = requireRow(*result, "/sectors/0/surfaces/0/adjoin") )
    {
        check(string(*change, "kind", "presence") && boolean(property(*change, "before"), "present", true)
                && string(property(*change, "before"), "type", "bool")
                && boolean(property(*change, "before"), "value", false)
                && boolean(property(*change, "after"), "value", true), "composite presence records occupancy");
    }
    if ( const auto* change = requireRow(*result, "/sectors/0/surfaces/0/adjoin/sector") )
    {
        absent(*change, "before");
        scalar(*change, "after", "int32", 2);
    }
    if ( const auto* change = requireRow(*result, "/sectors/0/surfaces/0/adjoin/surface") )
    {
        absent(*change, "before");
        scalar(*change, "after", "int32", 3);
    }
    const sed::Level noOwners;
    sed::Level newBaseline;
    newBaseline.motsArchLightingBakeBaselines.resize(1);
    const auto baselinePreview = compare(noOwners, newBaseline);
    if ( const auto* baselineView = view(baselinePreview) )
    {
        if ( const auto* change = requireRow(*baselineView, "/motsArchLightingBakeBaselines/0/originalArchLighting") )
        {
            absent(*change, "before");
            check(boolean(property(*change, "after"), "present", true)
                    && boolean(property(*change, "after"), "value", false),
                "missing owner differs from existing owner with no optional object");
        }
    }
    newBaseline.motsArchLightingBakeBaselines[0].originalArchLighting.emplace();
    const auto nestedPreview = compare(noOwners, newBaseline);
    if ( const auto* nestedView = view(nestedPreview) )
    {
        if ( const auto* change = requireRow(*nestedView, "/motsArchLightingBakeBaselines/0/originalArchLighting/meshes") )
        {
            check(string(*change, "kind", "arrayLength"), "nullable empty collection retains its structural context");
            absent(*change, "before");
            scalar(*change, "after", "size", 0);
        }
    }
}

void floatBitsAndNonfiniteJson()
{
    auto source = populatedLevel();
    source.header.gravity = std::bit_cast<double>(std::uint64_t{0x8000000000000000});
    source.ppunit = std::bit_cast<double>(std::uint64_t{0x7ff8000000000041});
    source.sectors[0].surfaces[0].uScale = std::bit_cast<float>(std::uint32_t{0x80000000});
    source.sectors[0].surfaces[0].vScale = std::bit_cast<float>(std::uint32_t{0x7fc00001});
    auto candidate = source;
    candidate.header.gravity = 0.0;
    candidate.ppunit = std::bit_cast<double>(std::uint64_t{0x7ff8000000000042});
    candidate.sectors[0].surfaces[0].uScale = 0.0f;
    candidate.sectors[0].surfaces[0].vScale = std::bit_cast<float>(std::uint32_t{0x7fc00002});
    candidate.header.gouraudDistance = std::numeric_limits<double>::infinity();
    candidate.header.perspectiveDistance = -std::numeric_limits<double>::infinity();
    candidate.sectors[0].ambient.r = std::numeric_limits<float>::infinity();
    candidate.sectors[0].ambient.g = -std::numeric_limits<float>::infinity();
    candidate.header.fog.fogStart = std::bit_cast<double>(std::uint64_t{0x7ff0000000000001});
    candidate.sectors[0].ambient.b = std::bit_cast<float>(std::uint32_t{0x7f800001});
    const auto preview = compare(source, candidate);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    completeCount(*result, 10);
    struct Expected
    {
        const char* path;
        const char* side;
        const char* type;
        const char* bits;
        const char* classification;
        bool nullValue;
    };
    const Expected expected[]{
        { "/header/gravity", "before", "float64", "8000000000000000", "negativeZero", false },
        { "/header/gravity", "after", "float64", "0000000000000000", "finite", false },
        { "/ppunit", "before", "float64", "7ff8000000000041", "nan", true },
        { "/ppunit", "after", "float64", "7ff8000000000042", "nan", true },
        { "/sectors/0/surfaces/0/uScale", "before", "float32", "80000000", "negativeZero", false },
        { "/sectors/0/surfaces/0/uScale", "after", "float32", "00000000", "finite", false },
        { "/sectors/0/surfaces/0/vScale", "before", "float32", "7fc00001", "nan", true },
        { "/sectors/0/surfaces/0/vScale", "after", "float32", "7fc00002", "nan", true },
        { "/header/gouraudDistance", "after", "float64", "7ff0000000000000", "positiveInfinity", true },
        { "/header/perspectiveDistance", "after", "float64", "fff0000000000000", "negativeInfinity", true },
        { "/sectors/0/ambient/r", "after", "float32", "7f800000", "positiveInfinity", true },
        { "/sectors/0/ambient/g", "after", "float32", "ff800000", "negativeInfinity", true },
        { "/header/fog/fogStart", "after", "float64", "7ff0000000000001", "nan", true },
        { "/sectors/0/ambient/b", "after", "float32", "7f800001", "nan", true }
    };
    for ( const auto& test : expected )
    {
        if ( const auto* change = requireRow(*result, test.path) )
        {
            const auto& value = property(*change, test.side);
            check(string(value, "type", test.type) && string(value, "bits", test.bits)
                    && string(value, "classification", test.classification), "IEEE storage and classification are explicit");
            check(property(value, "value").type == (test.nullValue ? Json::Type::Null : Json::Type::Number),
                "only nonfinite numeric payloads use JSON null");
        }
    }
    const auto serialized = sed_agent::toJsonString(preview);
    const auto parsed = sed_agent::parseJson(serialized);
    check(parsed.isObject(), "exceptional floats serialize as valid JSON");
    const auto unchanged = compare(source, source);
    if ( const auto* same = view(unchanged) )
    {
        completeCount(*same, 0);
    }
}

void utf8AndInvalidBytes()
{
    sed::Level source;
    source.masterCMP = "old";
    auto candidate = source;
    candidate.masterCMP = std::string("A\0", 2) + "\xc3\xa9\xf0\x9f\x8c\x99";
    const auto preview = compare(source, candidate);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    completeCount(*result, 1);
    if ( const auto* change = requireRow(*result, "/masterCMP") )
    {
        check(string(property(*change, "after"), "encoding", "utf8")
                && string(property(*change, "after"), "value", candidate.masterCMP)
                && number(property(*change, "after"), "byteCount", 8), "valid UTF-8 and embedded NUL round-trip exactly");
    }
    struct Invalid
    {
        std::string bytes;
        const char* hex;
    };
    const std::vector<Invalid> invalid{
        { std::string("\xc0\xaf", 2), "c0af" },
        { std::string("\xed\xa0\x80", 3), "eda080" },
        { std::string("\xf4\x90\x80\x80", 4), "f4908080" },
        { std::string("\xe2\x82", 2), "e282" },
        { std::string("\x80\xff\0", 3), "80ff00" }
    };
    for ( const auto& test : invalid )
    {
        candidate.masterCMP = test.bytes;
        const auto badPreview = compare(source, candidate);
        if ( const auto* bad = view(badPreview) )
        {
            if ( const auto* change = requireRow(*bad, "/masterCMP") )
            {
                check(string(property(*change, "after"), "encoding", "hex")
                        && string(property(*change, "after"), "value", test.hex), "invalid UTF-8 uses exact lowercase byte hex");
            }
            check(sed_agent::parseJson(sed_agent::toJsonString(badPreview)).isObject(), "invalid source bytes never make invalid JSON");
        }
    }
}

void orderedDuplicatesAndPositionalArrays()
{
    sed::Level source;
    source.things.resize(2);
    source.things[0].id = 42;
    source.things[1].id = 42;
    source.things[0].name = "first";
    source.things[1].name = "second";
    source.things[0].values.resize(2);
    source.things[0].values[0].name = "health";
    source.things[0].values[1].name = "health";
    source.things[0].values[0].intValue = 10;
    source.things[0].values[1].intValue = 20;
    auto candidate = source;
    std::swap(candidate.things[0].values[0], candidate.things[0].values[1]);
    const auto preview = compare(source, candidate);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    completeCount(*result, 2);
    if ( const auto* change = requireRow(*result, "/things/0/values/0/intValue") )
    {
        scalar(*change, "before", "int32", 10);
        scalar(*change, "after", "int32", 20);
    }
    if ( const auto* change = requireRow(*result, "/things/0/values/1/intValue") )
    {
        scalar(*change, "before", "int32", 20);
        scalar(*change, "after", "int32", 10);
    }
    candidate = source;
    candidate.things.erase(candidate.things.begin());
    const auto shiftedPreview = compare(source, candidate);
    if ( const auto* shifted = view(shiftedPreview) )
    {
        if ( const auto* change = requireRow(*shifted, "/things") )
        {
            check(string(*change, "kind", "arrayLength"), "deletion emits positional array length");
            scalar(*change, "before", "size", 2);
            scalar(*change, "after", "size", 1);
        }
        if ( const auto* change = requireRow(*shifted, "/things/0/name") )
        {
            check(string(property(*change, "before"), "value", "first")
                    && string(property(*change, "after"), "value", "second"), "duplicate IDs do not invent cross-snapshot identity");
        }
        if ( const auto* change = requireRow(*shifted, "/things/1/id") )
        {
            scalar(*change, "before", "int32", 42);
            absent(*change, "after");
        }
    }
    sed::Level empty;
    sed::Level added;
    added.materials.push_back("x.mat");
    const auto addedPreview = compare(empty, added);
    if ( const auto* addedView = view(addedPreview) )
    {
        completeCount(*addedView, 2);
        const auto& changes = property(*addedView, "changes").array;
        if ( check(changes.size() == 2, "array addition has length then scalar records") )
        {
            check(string(changes[0], "path", "/materials") && string(changes[0], "kind", "arrayLength")
                    && string(changes[1], "path", "/materials/0"), "array changes use deterministic collection-first order");
            scalar(changes[0], "before", "size", 0);
            scalar(changes[0], "after", "size", 1);
            absent(changes[1], "before");
        }
    }
    const auto removedPreview = compare(added, empty);
    if ( const auto* removedView = view(removedPreview) )
    {
        completeCount(*removedView, 2);
        if ( const auto* change = requireRow(*removedView, "/materials/0") )
        {
            absent(*change, "after");
        }
    }
    source = {};
    source.sectors.resize(1);
    source.sectors[0].surfaces.resize(1);
    candidate = source;
    candidate.sectors[0].surfaces[0].texVertices.resize(1);
    const auto independent = compare(source, candidate);
    if ( const auto* independentView = view(independent) )
    {
        completeCount(*independentView, 7);
        check(row(*independentView, "/sectors/0/surfaces/0/texVertices")
                && !row(*independentView, "/sectors/0/surfaces/0/vertices"), "corner arrays are separate stored sequences");
    }
}

void exclusionsOnlyAreCoveredNoOp()
{
    auto source = populatedLevel();
    auto candidate = source;
    candidate.sectors[0].surfaces[0].d3dId = 17;
    candidate.importedJklSurfaceRefs.push_back({ 8, 9 });
    candidate.importedJklCogRowsPresent = true;
    candidate.retainedJklSourceText = "source";
    candidate.retainedJklSourceHash = "hash";
    candidate.retainedJklUnknownSections = { "unknown" };
    candidate.retainedJklHeaderUnknownLines = { "header" };
    candidate.retainedJklSectorUnknownLines = { { "sector" } };
    candidate.importedGeoWorldVertices.resize(2);
    candidate.importedGeoWorldTexVertices.resize(2);
    candidate.importedGeoAdjoinFlags = { 0xffffffffu };
    candidate.importedGeoAdjoinMirrors = { 3 };
    candidate.importedGeoVertexIndices = { { 1 } };
    candidate.importedGeoSurfaceIndices = { { 2 } };
    candidate.importedGeoTexVertexIndices = { { { 3 } } };
    candidate.importedGeoAdjoinIndices = { { 4 } };
    candidate.importedGeoMaterialIndices = { { 5 } };
    candidate.importedGeoSurfaceCount = 8;
    candidate.importedGeoAdjoinCount = 9;
    const auto preview = compare(source, candidate);
    if ( const auto* result = view(preview) )
    {
        completeCount(*result, 0);
        check(property(preview, "excludedFields").type == Json::Type::Array
                && property(preview, "excludedFields").array.size() == 19, "all explicit exclusions are disclosed");
    }
}

void slotBudgetsChargeBeforeWork()
{
    const sed::Level source;
    auto candidate = source;
    candidate.header.gravity = 1.0;
    SemanticPreviewLimits limits;
    for ( const std::size_t cap : { 0u, 3u, 4u } )
    {
        limits.maxVisitedSlots = cap;
        const auto preview = compare(source, candidate, limits);
        const auto* result = view(preview);
        if ( !result )
        {
            return;
        }
        check(boolean(*result, "comparisonComplete", false) && boolean(*result, "detailsComplete", false)
                && property(*result, "totalChanges").type == Json::Type::Null, "slot exhaustion is explicitly incomplete");
        check(number(property(*result, "work"), "visitedSlots", static_cast<double>(cap)), "no slot work occurs beyond admission");
        check(hasReason(*result, "comparisonSlots"), "slot reason is stable");
        if ( cap < 4 )
        {
            check(number(*result, "observedChanges", 0) && property(*result, "changed").type == Json::Type::Null,
                "zero observed before exhaustion is unknown, not unchanged");
        }
        else
        {
            check(number(*result, "observedChanges", 1) && boolean(*result, "changed", true),
                "an observed difference remains true after exhaustion");
            requireRow(*result, "/header/gravity");
        }
    }
    limits.maxVisitedSlots = 68;
    const auto exact = compare(source, source, limits);
    if ( const auto* result = view(exact) )
    {
        completeCount(*result, 0);
        check(number(property(*result, "work"), "visitedSlots", 68), "exact slot cap completes without spurious exhaustion");
    }
    limits.maxVisitedSlots = 67;
    const auto shortPreview = compare(source, source, limits);
    if ( const auto* result = view(shortPreview) )
    {
        check(boolean(*result, "comparisonComplete", false) && number(property(*result, "work"), "visitedSlots", 67),
            "one slot below a complete empty model stops exactly");
    }
}

void largeUnchangedModelsStillHitWorkLimits()
{
    sed::Level source;
    source.motsArchLighting.resize(10000);
    SemanticPreviewLimits limits;
    limits.maxVisitedSlots = 100;
    const auto preview = compare(source, source, limits);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    check(boolean(*result, "comparisonComplete", false) && hasReason(*result, "comparisonSlots")
            && number(property(*result, "work"), "visitedSlots", 100), "empty ARCH records consume bounded traversal work");
    check(property(*result, "changed").type == Json::Type::Null, "unvisited equal tails cannot be declared unchanged");
    source = {};
    source.templates.resize(10000);
    const auto emptyRecords = compare(source, source, limits);
    if ( const auto* emptyView = view(emptyRecords) )
    {
        check(boolean(*emptyView, "comparisonComplete", false) && hasReason(*emptyView, "comparisonSlots"),
            "unchanged empty templates are charged");
    }
    source = {};
    source.masterCMP.assign(2 * 1024 * 1024 + 1, 'x');
    const auto hugeText = compare(source, source);
    if ( const auto* textView = view(hugeText) )
    {
        check(boolean(*textView, "comparisonComplete", false) && hasReason(*textView, "comparisonStringBytes")
                && number(property(*textView, "work"), "comparedStringBytes", 0),
            "both sides of unchanged text are admitted before comparison");
    }
}

void textBudgetExactBoundaryAndIndependentViews()
{
    sed::Level source;
    source.masterCMP = "ab";
    auto candidate = source;
    candidate.masterCMP = "cd";
    auto persisted = source;
    persisted.masterCMP = "efg";
    SemanticPreviewLimits limits;
    limits.maxComparedStringBytes = 4;
    const auto preview = sed_agent::buildSemanticPreview(source, candidate, persisted, "in", "out", limits);
    const auto* authored = view(preview);
    const auto* output = view(preview, "persisted");
    if ( !authored || !output )
    {
        return;
    }
    completeCount(*authored, 1);
    check(number(property(*authored, "work"), "comparedStringBytes", 4), "text equality admits exactly both source lengths");
    check(boolean(*output, "comparisonComplete", false) && number(*output, "observedChanges", 0)
            && property(*output, "changed").type == Json::Type::Null
            && number(property(*output, "work"), "comparedStringBytes", 0), "second view has its own before-work string budget");
    check(hasReason(*output, "comparisonStringBytes"), "unadmitted text has no inferred comparison result");
    limits.maxComparedStringBytes = 3;
    const auto shortPreview = compare(source, candidate, limits);
    if ( const auto* result = view(shortPreview) )
    {
        check(number(property(*result, "work"), "comparedStringBytes", 0)
                && property(*result, "changes").array.empty(), "one byte short emits no unadmitted text difference");
    }
}

void changeCapKeepsExactCountsWithoutDiscardedValues()
{
    sed::Level source;
    source.masterCMP = "abc";
    auto candidate = source;
    candidate.header.version = 1;
    candidate.header.gravity = 2;
    candidate.masterCMP = "def";
    SemanticPreviewLimits limits;
    limits.maxChanges = 2;
    limits.maxInlineStringBytes = 0;
    const auto preview = compare(source, candidate, limits);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    completeCount(*result, 3, false);
    const auto& changes = property(*result, "changes").array;
    check(changes.size() == 2 && hasReason(*result, "changeLimit"), "change cap limits rows while counting all differences");
    check(number(property(*result, "work"), "inlineStringBytes", 0)
            && !hasReason(*result, "inlineStringLength"), "discarded rows do not construct or charge value descriptors");
    if ( changes.size() == 2 )
    {
        check(string(changes[0], "path", "/header/version") && string(changes[1], "path", "/header/gravity"),
            "detail cap preserves deterministic prefix");
    }
    limits.maxChanges = 3;
    limits.maxInlineStringBytes = 512;
    const auto exact = compare(source, candidate, limits);
    if ( const auto* exactView = view(exact) )
    {
        completeCount(*exactView, 3);
        check(!hasReason(*exactView, "changeLimit"), "exact row cap is complete");
    }
    limits.maxChanges = 0;
    const auto zero = compare(source, candidate, limits);
    if ( const auto* zeroView = view(zero) )
    {
        completeCount(*zeroView, 3, false);
        check(property(*zeroView, "changes").array.empty(), "zero detail budget still compares all fields");
    }
}

void inlineStringLimitsNeverClip()
{
    sed::Level source;
    source.masterCMP = "abc";
    auto candidate = source;
    candidate.masterCMP = "defg";
    SemanticPreviewLimits limits;
    limits.maxInlineStringBytes = 3;
    const auto preview = compare(source, candidate, limits);
    const auto* result = view(preview);
    if ( !result )
    {
        return;
    }
    completeCount(*result, 1, false);
    if ( const auto* change = requireRow(*result, "/masterCMP") )
    {
        check(string(property(*change, "before"), "value", "abc"), "exact per-string limit emits full string");
        const auto& after = property(*change, "after");
        check(boolean(after, "omitted", true) && number(after, "byteCount", 4)
                && !after.find("value") && !after.find("encoding"), "oversize text is omitted with truthful byte count");
    }
    check(hasReason(*result, "inlineStringLength") && number(property(*result, "work"), "inlineStringBytes", 3),
        "only emitted raw bytes consume inline allowance");
    limits.maxInlineStringBytes = 4;
    limits.maxTotalInlineStringBytes = 6;
    const auto aggregate = compare(source, candidate, limits);
    if ( const auto* aggregateView = view(aggregate) )
    {
        completeCount(*aggregateView, 1, false);
        check(hasReason(*aggregateView, "inlineStringBytes") && !hasReason(*aggregateView, "inlineStringLength")
                && number(property(*aggregateView, "work"), "inlineStringBytes", 3), "aggregate admission occurs before copying");
        if ( const auto* change = requireRow(*aggregateView, "/masterCMP") )
        {
            check(boolean(property(*change, "after"), "omitted", true) && !property(*change, "after").find("value"),
                "aggregate overflow does not clip a value");
        }
    }
    limits.maxTotalInlineStringBytes = 7;
    const auto exact = sed_agent::buildSemanticPreview(source, candidate, candidate, "in", "out", limits);
    for ( const auto name : { "authored", "persisted" } )
    {
        if ( const auto* exactView = view(exact, name) )
        {
            completeCount(*exactView, 1);
            check(number(property(*exactView, "work"), "inlineStringBytes", 7), "each view owns its exact inline allowance");
        }
    }
    source.masterCMP.clear();
    candidate.masterCMP = std::string("\xff\xfe", 2);
    limits.maxInlineStringBytes = 2;
    limits.maxTotalInlineStringBytes = 2;
    const auto hex = compare(source, candidate, limits);
    if ( const auto* hexView = view(hex) )
    {
        completeCount(*hexView, 1);
        check(number(property(*hexView, "work"), "inlineStringBytes", 2), "hex output charges raw bytes, not doubled encoded length");
    }
}

void capabilitiesAndDeterminism()
{
    const auto capabilities = sed_agent::semanticPreviewCapabilities();
    if ( !check(capabilities.isObject(), "capabilities returns metadata") )
    {
        return;
    }
    const auto& defaults = property(capabilities, "limits");
    check(number(defaults, "maxVisitedSlots", 1000000) && number(defaults, "maxComparedStringBytes", 4194304)
            && number(defaults, "maxChanges", 128) && number(defaults, "maxInlineStringBytes", 512)
            && number(defaults, "maxTotalInlineStringBytes", 32768), "discovery publishes the actual default limits");
    const auto& commands = property(capabilities, "commands").array;
    check(commands.size() == 2 && commands[0].str == "plan" && commands[1].str == "apply", "discovery scope is generic plan/apply");
    const auto source = populatedLevel();
    auto candidate = source;
    candidate.header.gravity = 3;
    candidate.cogs[0].values[0].explicitMask = 7;
    candidate.motsArchLighting[0].meshes[0].vertices[0].mono = -0.0f;
    SemanticPreviewLimits limits;
    limits.maxChanges = 1;
    const auto first = compare(source, candidate, limits);
    const auto second = compare(source, candidate, limits);
    check(sed_agent::toJsonString(first) == sed_agent::toJsonString(second), "identical immutable snapshots yield byte-identical preview JSON");
    const auto* result = view(first);
    if ( !result )
    {
        return;
    }
    check(number(property(first, "limits"), "maxChanges", 1), "preview reports effective internal test limits");
    check(source.header.gravity == 0 && !source.cogs[0].values[0].explicitMask
            && std::bit_cast<std::uint32_t>(source.motsArchLighting[0].meshes[0].vertices[0].mono) == 0,
        "preview never writes source fields");
    check(candidate.header.gravity == 3 && candidate.cogs[0].values[0].explicitMask == 7
            && std::bit_cast<std::uint32_t>(candidate.motsArchLighting[0].meshes[0].vertices[0].mono) == 0x80000000u,
        "preview never rewrites candidate bits or optional storage");
    completeCount(*result, 3, false);
}

void run(std::string_view name, void (*test)())
{
    currentCase = name;
    try
    {
        test();
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected exception: ") + error.what());
    }
}
} // namespace

int main()
{
    run("no-op and independent views", noOpAndIndependentViews);
    run("covered domains and raw scalar types", coveredDomainsAndRawScalarTypes);
    run("strings and ARCH provenance", stringsAndProvenanceStayExact);
    run("optional scalars and booleans", optionalScalarsAndBooleanStorage);
    run("optional composites and missing owners", optionalCompositePresenceAndMissingOwner);
    run("IEEE float bits and valid JSON", floatBitsAndNonfiniteJson);
    run("UTF-8 and invalid byte strings", utf8AndInvalidBytes);
    run("ordered duplicates and positional arrays", orderedDuplicatesAndPositionalArrays);
    run("explicit exclusions", exclusionsOnlyAreCoveredNoOp);
    run("before-work slot boundaries", slotBudgetsChargeBeforeWork);
    run("huge unchanged records and text", largeUnchangedModelsStillHitWorkLimits);
    run("per-view string comparison limits", textBudgetExactBoundaryAndIndependentViews);
    run("detail cap and exact counts", changeCapKeepsExactCountsWithoutDiscardedValues);
    run("inline string admission", inlineStringLimitsNeverClip);
    run("capabilities determinism and immutability", capabilitiesAndDeterminism);
    std::cout << "semantic preview model failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
