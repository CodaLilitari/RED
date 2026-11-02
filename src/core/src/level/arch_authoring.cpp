#include "sed/level/arch_authoring.hpp"

#include "sed/assets/source_lights.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sed::level
{

namespace
{

constexpr std::int32_t kArchFlag = 0x8;
constexpr std::int32_t kNoBlockFlag = 0x1;
constexpr std::size_t kMaxAuthoringItems = 1000000;

PreparedArchAuthoring refused(
    const char* code, const std::string& message, const std::int32_t thingIndex)
{
    PreparedArchAuthoring prepared{};
    prepared.thingIndex = thingIndex;
    prepared.failure = ArchAuthoringFailure{ std::string(code), message };
    return prepared;
}

const MotsArchAuthoringEntry* entryFor(
    const Level& level, const std::int32_t thingIndex) noexcept
{
    for (const MotsArchAuthoringEntry& entry : level.motsArchAuthoring)
    {
        if (entry.thingIndex == thingIndex)
        {
            return &entry;
        }
    }
    return nullptr;
}

std::optional<PreparedArchAuthoring> checkThing(
    const Level& level, const std::int32_t thingIndex,
    const assets::Templates* templates)
{
    if (level.kind != ProjectType::MOTS)
    {
        return refused("archRequiresMots",
            "ARCH authoring requires a MotS level.", thingIndex);
    }
    if (thingIndex < 0
        || static_cast<std::size_t>(thingIndex) >= level.things.size())
    {
        return refused("archThingIndexInvalid",
            "ARCH authoring thing index is invalid.", thingIndex);
    }
    const Thing& thing = level.things[static_cast<std::size_t>(thingIndex)];
    if (!isMotsArchThing(level, thing, templates))
    {
        return refused("archThingNotEligible",
            "ARCH authoring requires an Arch thing (thingflags 0x8).", thingIndex);
    }
    if (thing.archLightingIndex < -1
        || (thing.archLightingIndex >= 0
            && static_cast<std::size_t>(thing.archLightingIndex)
                >= level.motsArchLighting.size()))
    {
        return refused("archIndexInvalid",
            "ARCH lighting index is outside the stored arrays.", thingIndex);
    }
    return std::nullopt;
}

PreparedArchAuthoring staged(
    const Level& level, const std::int32_t thingIndex,
    MotsArchLightObject object, MotsArchAuthoringEntry entry)
{
    // Assign the next free wire index, as the edit engine does.
    std::int64_t next = 0;
    for (const MotsArchLightObject& existing : level.motsArchLighting)
    {
        next = std::max(next, static_cast<std::int64_t>(existing.wireIndex) + 1);
    }
    if (next > std::numeric_limits<std::int32_t>::max())
    {
        return refused("archCapacityExceeded",
            "ARCH authoring exceeds the bounded record budget.", thingIndex);
    }
    object.wireIndex = static_cast<std::int32_t>(next);
    for (MotsArchLightMesh& mesh : object.meshes)
    {
        // Mesh wire indices follow stored order.
    }
    for (std::size_t m = 0; m < object.meshes.size(); ++m)
    {
        object.meshes[m].wireIndex = static_cast<std::int32_t>(m);
    }
    entry.sampleSha256Hex = archSampleDigestHex(object);

    PreparedArchAuthoring prepared{};
    prepared.thingIndex = thingIndex;
    prepared.expectedArchIndex =
        level.things[static_cast<std::size_t>(thingIndex)].archLightingIndex;
    prepared.expectedObjectCount = level.motsArchLighting.size();
    prepared.appendObject = true;
    prepared.newObject = std::move(object);
    prepared.newEntry = std::move(entry);
    return prepared;
}

std::vector<std::size_t> maxCounts(const MotsArchLightingTopology& topology)
{
    std::vector<std::size_t> counts;
    for (const std::vector<std::size_t>& geoset : topology.geosets)
    {
        if (geoset.size() > counts.size())
        {
            counts.resize(geoset.size(), 0);
        }
        for (std::size_t m = 0; m < geoset.size(); ++m)
        {
            counts[m] = std::max(counts[m], geoset[m]);
        }
    }
    return counts;
}

MotsArchLightObject zeroedObject(const std::vector<std::size_t>& counts)
{
    MotsArchLightObject object{};
    object.meshes.reserve(counts.size());
    for (std::size_t m = 0; m < counts.size(); ++m)
    {
        MotsArchLightMesh mesh{};
        mesh.wireIndex = static_cast<std::int32_t>(m);
        mesh.vertices.assign(counts[m], MotsArchLightVertex{});
        object.meshes.push_back(std::move(mesh));
    }
    return object;
}

bool finiteVertex(const MotsArchLightVertex& vertex) noexcept
{
    return std::isfinite(vertex.mono) && std::isfinite(vertex.red)
        && std::isfinite(vertex.green) && std::isfinite(vertex.blue);
}

double segmentTriangleEpsilon() noexcept
{
    return 1e-9;
}

bool segmentBlocked(
    const TVector& from, const TVector& to,
    const ArchOccluderTriangle& tri) noexcept
{
    const TVector edge1{ tri.b.x - tri.a.x, tri.b.y - tri.a.y, tri.b.z - tri.a.z };
    const TVector edge2{ tri.c.x - tri.a.x, tri.c.y - tri.a.y, tri.c.z - tri.a.z };
    const TVector dir{ to.x - from.x, to.y - from.y, to.z - from.z };
    const TVector pvec{
        dir.y * edge2.z - dir.z * edge2.y,
        dir.z * edge2.x - dir.x * edge2.z,
        dir.x * edge2.y - dir.y * edge2.x };
    const double det = edge1.x * pvec.x + edge1.y * pvec.y + edge1.z * pvec.z;
    if (std::abs(det) < segmentTriangleEpsilon())
    {
        return false;
    }
    const double invDet = 1.0 / det;
    const TVector tvec{ from.x - tri.a.x, from.y - tri.a.y, from.z - tri.a.z };
    const double u = (tvec.x * pvec.x + tvec.y * pvec.y + tvec.z * pvec.z) * invDet;
    if (u < -segmentTriangleEpsilon() || u > 1.0 + segmentTriangleEpsilon())
    {
        return false;
    }
    const TVector qvec{
        tvec.y * edge1.z - tvec.z * edge1.y,
        tvec.z * edge1.x - tvec.x * edge1.z,
        tvec.x * edge1.y - tvec.y * edge1.x };
    const double v = (dir.x * qvec.x + dir.y * qvec.y + dir.z * qvec.z) * invDet;
    if (v < -segmentTriangleEpsilon() || u + v > 1.0 + segmentTriangleEpsilon())
    {
        return false;
    }
    const double t = (edge2.x * qvec.x + edge2.y * qvec.y + edge2.z * qvec.z) * invDet;
    // Hits at the endpoints do not block the sample itself.
    return t > 1e-7 && t < 1.0 - 1e-7;
}

} // namespace

std::string_view archBakeProfileToken(const ArchBakeProfile profile) noexcept
{
    return profile == ArchBakeProfile::SourcePointsWorldOccludedV1
        ? std::string_view("source-points-world-occluded-v1")
        : std::string_view("source-points-v1");
}

std::string_view archSampleOriginToken(const MotsArchSampleOrigin origin) noexcept
{
    switch (origin)
    {
    case MotsArchSampleOrigin::BoundExistingSamples:
        return "bound-existing";
    case MotsArchSampleOrigin::ResetZeroed:
        return "reset-zeroed";
    case MotsArchSampleOrigin::BakedSourcePointsV1:
        return "baked-source-points-v1";
    case MotsArchSampleOrigin::BakedWorldOccludedV1:
        return "baked-world-occluded-v1";
    case MotsArchSampleOrigin::ImportedUnknown:
    default:
        return "imported-unknown";
    }
}

std::optional<MotsArchSampleOrigin> parseArchSampleOrigin(
    const std::string_view token) noexcept
{
    if (text::iequalsAscii(token, "imported-unknown"))
    {
        return MotsArchSampleOrigin::ImportedUnknown;
    }
    if (text::iequalsAscii(token, "bound-existing"))
    {
        return MotsArchSampleOrigin::BoundExistingSamples;
    }
    if (text::iequalsAscii(token, "reset-zeroed"))
    {
        return MotsArchSampleOrigin::ResetZeroed;
    }
    if (text::iequalsAscii(token, "baked-source-points-v1"))
    {
        return MotsArchSampleOrigin::BakedSourcePointsV1;
    }
    if (text::iequalsAscii(token, "baked-world-occluded-v1"))
    {
        return MotsArchSampleOrigin::BakedWorldOccludedV1;
    }
    return std::nullopt;
}

std::string archSampleDigestHex(const MotsArchLightObject& object)
{
    std::string text = "sed-arch-samples-v1\n";
    text += std::to_string(object.meshes.size());
    text.push_back('\n');
    char buffer[16]{};
    const auto bits = [&text, &buffer](const float value)
    {
        const auto raw = std::bit_cast<std::uint32_t>(value);
        const auto result = std::to_chars(
            buffer, buffer + sizeof(buffer), raw, 16);
        // Zero-pad to eight digits for a stable spelling.
        const std::string digits(buffer, result.ptr);
        text.append(8 - digits.size(), '0');
        text += digits;
    };
    for (const MotsArchLightMesh& mesh : object.meshes)
    {
        text += std::to_string(mesh.vertices.size());
        text.push_back('\n');
        for (const MotsArchLightVertex& vertex : mesh.vertices)
        {
            bits(vertex.mono);
            text.push_back(' ');
            bits(vertex.red);
            text.push_back(' ');
            bits(vertex.green);
            text.push_back(' ');
            bits(vertex.blue);
            text.push_back('\n');
        }
    }
    return assets::sourceLightSha256Hex(text);
}

std::string archAuthoringChecksumHex(
    const std::span<const MotsArchLightObject> objects,
    const std::span<const std::pair<std::int32_t, std::int32_t>> bindings)
{
    std::string text = "sed-arch-authoring-base-v1\n";
    text += std::to_string(objects.size());
    text.push_back('\n');
    for (const MotsArchLightObject& object : objects)
    {
        text += std::to_string(object.wireIndex);
        text.push_back(' ');
        text += archSampleDigestHex(object);
        text.push_back('\n');
    }
    text += std::to_string(bindings.size());
    text.push_back('\n');
    for (const auto& [thingIndex, archIndex] : bindings)
    {
        text += std::to_string(thingIndex);
        text.push_back(' ');
        text += std::to_string(archIndex);
        text.push_back('\n');
    }
    return assets::sourceLightSha256Hex(text);
}

ArchBindingStatus archBindingStatus(
    const Level& level, const std::int32_t thingIndex,
    const std::optional<std::string_view> currentBakeInputHex) noexcept
{
    if (thingIndex < 0
        || static_cast<std::size_t>(thingIndex) >= level.things.size())
    {
        return ArchBindingStatus::Unbound;
    }
    const Thing& thing = level.things[static_cast<std::size_t>(thingIndex)];
    if (thing.archLightingIndex < 0
        || static_cast<std::size_t>(thing.archLightingIndex)
            >= level.motsArchLighting.size())
    {
        return ArchBindingStatus::Unbound;
    }
    const MotsArchAuthoringEntry* entry = entryFor(level, thingIndex);
    if (!entry)
    {
        return ArchBindingStatus::UnknownImported;
    }
    switch (entry->origin)
    {
    case MotsArchSampleOrigin::ImportedUnknown:
        return ArchBindingStatus::UnknownImported;
    case MotsArchSampleOrigin::BoundExistingSamples:
        return ArchBindingStatus::BoundExistingSamples;
    case MotsArchSampleOrigin::ResetZeroed:
        return ArchBindingStatus::ResetZeroed;
    case MotsArchSampleOrigin::BakedSourcePointsV1:
    case MotsArchSampleOrigin::BakedWorldOccludedV1:
        break;
    }
    const MotsArchLightObject& current =
        level.motsArchLighting[static_cast<std::size_t>(thing.archLightingIndex)];
    if (archSampleDigestHex(current) != entry->sampleSha256Hex)
    {
        return ArchBindingStatus::BakedOverridden;
    }
    if (!currentBakeInputHex.has_value())
    {
        return ArchBindingStatus::BakedSamplesIntact;
    }
    return *currentBakeInputHex == entry->bakeInputSha256Hex
        ? ArchBindingStatus::BakedCurrentInputs
        : ArchBindingStatus::BakedStaleInputs;
}

std::string archBakeInputDigestHex(
    const ArchBakeProfile profile,
    const std::span<const SedLight> lights,
    const std::vector<std::vector<TVector>>& meshVertices)
{
    std::string text = "sed-arch-bake-input-v1\n";
    text += archBakeProfileToken(profile);
    text.push_back('\n');
    text += std::to_string(lights.size());
    text.push_back('\n');
    char buffer[32]{};
    const auto doubleBits = [&text, &buffer](const double value)
    {
        const auto raw = std::bit_cast<std::uint64_t>(value);
        const auto result = std::to_chars(
            buffer, buffer + sizeof(buffer), raw, 16);
        const std::string digits(buffer, result.ptr);
        text.append(16 - digits.size(), '0');
        text += digits;
    };
    const auto floatBits = [&text, &buffer](const float value)
    {
        const auto raw = std::bit_cast<std::uint32_t>(value);
        const auto result = std::to_chars(
            buffer, buffer + sizeof(buffer), raw, 16);
        const std::string digits(buffer, result.ptr);
        text.append(8 - digits.size(), '0');
        text += digits;
    };
    for (const SedLight& light : lights)
    {
        doubleBits(light.position.x);
        text.push_back(' ');
        doubleBits(light.position.y);
        text.push_back(' ');
        doubleBits(light.position.z);
        text.push_back(' ');
        doubleBits(light.range);
        text.push_back(' ');
        doubleBits(light.intensity);
        text.push_back(' ');
        doubleBits(light.effectiveRgbIntensity());
        text.push_back(' ');
        text += std::to_string(light.flags);
        text.push_back(' ');
        floatBits(light.color.r);
        text.push_back(' ');
        floatBits(light.color.g);
        text.push_back(' ');
        floatBits(light.color.b);
        text.push_back('\n');
    }
    std::size_t vertexTotal = 0;
    for (const std::vector<TVector>& mesh : meshVertices)
    {
        vertexTotal += mesh.size();
    }
    text += std::to_string(vertexTotal);
    text.push_back('\n');
    for (const std::vector<TVector>& mesh : meshVertices)
    {
        for (const TVector& point : mesh)
        {
            doubleBits(point.x);
            text.push_back(' ');
            doubleBits(point.y);
            text.push_back(' ');
            doubleBits(point.z);
            text.push_back('\n');
        }
    }
    return assets::sourceLightSha256Hex(text);
}

bool PreparedArchAuthoring::apply(Level& level) const
{
    if (!ok() || thingIndex < 0
        || static_cast<std::size_t>(thingIndex) >= level.things.size())
    {
        return false;
    }
    Thing& thing = level.things[static_cast<std::size_t>(thingIndex)];
    if (thing.archLightingIndex != expectedArchIndex
        || level.motsArchLighting.size() != expectedObjectCount)
    {
        return false;
    }
    if (appendObject)
    {
        if (level.motsArchLighting.size() >= kMaxAuthoringItems)
        {
            return false;
        }
        level.motsArchLighting.push_back(newObject);
        thing.archLightingIndex =
            static_cast<std::int32_t>(level.motsArchLighting.size() - 1);
    }
    else
    {
        thing.archLightingIndex = -1;
    }
    // Replace only this thing's provenance row.
    std::vector<MotsArchAuthoringEntry>& entries = level.motsArchAuthoring;
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [thingIndex = thingIndex](const MotsArchAuthoringEntry& entry)
            {
                return entry.thingIndex == thingIndex;
            }),
        entries.end());
    if (newEntry.has_value())
    {
        entries.push_back(*newEntry);
        std::sort(entries.begin(), entries.end(),
            [](const MotsArchAuthoringEntry& a, const MotsArchAuthoringEntry& b)
            {
                return a.thingIndex < b.thingIndex;
            });
    }
    return true;
}

PreparedArchAuthoring prepareBindArchSamples(
    const Level& level, const std::int32_t thingIndex,
    const std::int32_t sourceLightingIndex,
    const MotsArchLightingTopology& topology,
    std::string modelName, std::string modelEntrySha256Hex,
    const assets::Templates* templates)
{
    if (const auto failure = checkThing(level, thingIndex, templates))
    {
        return *failure;
    }
    if (sourceLightingIndex < 0
        || static_cast<std::size_t>(sourceLightingIndex)
            >= level.motsArchLighting.size())
    {
        return refused("archSourceInvalid",
            "ARCH bind source lighting index is invalid.", thingIndex);
    }
    const MotsArchLightObject& source =
        level.motsArchLighting[static_cast<std::size_t>(sourceLightingIndex)];
    std::string code;
    if (!archTopologyAllowsBinding(source, topology, &code))
    {
        return refused(code.empty() ? "archTopologyIncompatible" : code.c_str(),
            "ARCH bind source is not address-compatible with the model.", thingIndex);
    }
    if (level.motsArchLighting.size() >= kMaxAuthoringItems)
    {
        return refused("archCapacityExceeded",
            "ARCH authoring exceeds the bounded record budget.", thingIndex);
    }
    MotsArchAuthoringEntry entry{};
    entry.thingIndex = thingIndex;
    entry.origin = MotsArchSampleOrigin::BoundExistingSamples;
    entry.modelName = std::move(modelName);
    entry.modelEntrySha256Hex = std::move(modelEntrySha256Hex);
    return staged(level, thingIndex, source, std::move(entry));
}

PreparedArchAuthoring prepareResetArchBinding(
    const Level& level, const std::int32_t thingIndex,
    const MotsArchLightingTopology& topology,
    std::string modelName, std::string modelEntrySha256Hex,
    const assets::Templates* templates)
{
    if (const auto failure = checkThing(level, thingIndex, templates))
    {
        return *failure;
    }
    if (topology.geosets.empty())
    {
        return refused("archTopologyMissing",
            "ARCH reset requires resolved model topology.", thingIndex);
    }
    const std::vector<std::size_t> counts = maxCounts(topology);
    if (counts.empty())
    {
        return refused("archTopologyMissing",
            "ARCH reset requires resolved model topology.", thingIndex);
    }
    std::size_t total = 0;
    for (const std::size_t count : counts)
    {
        if (count > kMaxAuthoringItems - total)
        {
            return refused("archCapacityExceeded",
                "ARCH authoring exceeds the bounded record budget.", thingIndex);
        }
        total += count;
    }
    MotsArchAuthoringEntry entry{};
    entry.thingIndex = thingIndex;
    entry.origin = MotsArchSampleOrigin::ResetZeroed;
    entry.modelName = std::move(modelName);
    entry.modelEntrySha256Hex = std::move(modelEntrySha256Hex);
    return staged(level, thingIndex, zeroedObject(counts), std::move(entry));
}

PreparedArchAuthoring prepareBakeArchSamples(
    const Level& level, const std::int32_t thingIndex,
    const MotsArchLightingTopology& topology,
    const std::vector<std::vector<TVector>>& meshVertices,
    const std::span<const SedLight> lights,
    const ArchBakeProfile profile,
    const std::span<const ArchOccluderTriangle> occluders,
    std::string modelName, std::string modelEntrySha256Hex,
    const assets::Templates* templates)
{
    if (const auto failure = checkThing(level, thingIndex, templates))
    {
        return *failure;
    }
    if (topology.geosets.empty())
    {
        return refused("archTopologyMissing",
            "ARCH bake requires resolved model topology.", thingIndex);
    }
    const std::vector<std::size_t>& geosetZero = topology.geosets.front();
    if (meshVertices.size() != geosetZero.size())
    {
        return refused("archBakeGeometryMismatch",
            "ARCH bake geometry does not match geoset zero.", thingIndex);
    }
    for (std::size_t m = 0; m < geosetZero.size(); ++m)
    {
        if (meshVertices[m].size() != geosetZero[m])
        {
            return refused("archBakeGeometryMismatch",
                "ARCH bake geometry does not match geoset zero.", thingIndex);
        }
        for (const TVector& point : meshVertices[m])
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)
                || !std::isfinite(point.z))
            {
                return refused("archBakeInputInvalid",
                    "ARCH bake requires finite inputs.", thingIndex);
            }
        }
    }
    for (const SedLight& light : lights)
    {
        if (!std::isfinite(light.position.x) || !std::isfinite(light.position.y)
            || !std::isfinite(light.position.z) || !std::isfinite(light.range)
            || !std::isfinite(light.intensity)
            || !std::isfinite(light.effectiveRgbIntensity())
            || !std::isfinite(light.color.r) || !std::isfinite(light.color.g)
            || !std::isfinite(light.color.b))
        {
            return refused("archBakeInputInvalid",
                "ARCH bake requires finite inputs.", thingIndex);
        }
    }
    for (const ArchOccluderTriangle& tri : occluders)
    {
        const auto finite = [](const TVector& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        };
        if (!finite(tri.a) || !finite(tri.b) || !finite(tri.c))
        {
            return refused("archBakeInputInvalid",
                "ARCH bake requires finite inputs.", thingIndex);
        }
    }

    MotsArchLightObject baked = zeroedObject(geosetZero);
    const bool occluded = profile == ArchBakeProfile::SourcePointsWorldOccludedV1;
    for (std::size_t m = 0; m < geosetZero.size(); ++m)
    {
        for (std::size_t v = 0; v < geosetZero[m]; ++v)
        {
            const TVector& point = meshVertices[m][v];
            double mono = 0.0;
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            for (const SedLight& light : lights)
            {
                const double range = light.range;
                if (!(range > 0.0))
                {
                    continue;
                }
                const double dx = point.x - light.position.x;
                const double dy = point.y - light.position.y;
                const double dz = point.z - light.position.z;
                const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (!std::isfinite(dist) || dist >= range)
                {
                    continue;
                }
                if (occluded && (light.flags & kNoBlockFlag) == 0)
                {
                    bool blocked = false;
                    for (const ArchOccluderTriangle& tri : occluders)
                    {
                        if (segmentBlocked(light.position, point, tri))
                        {
                            blocked = true;
                            break;
                        }
                    }
                    if (blocked)
                    {
                        continue;
                    }
                }
                const double falloff = 1.0 - dist / range;
                const double weight = falloff * falloff;
                mono += light.intensity * weight;
                const double rgb = light.effectiveRgbIntensity() * weight;
                red += static_cast<double>(light.color.r) * rgb;
                green += static_cast<double>(light.color.g) * rgb;
                blue += static_cast<double>(light.color.b) * rgb;
            }
            MotsArchLightVertex& vertex = baked.meshes[m].vertices[v];
            vertex.mono = static_cast<float>(mono);
            vertex.red = static_cast<float>(red);
            vertex.green = static_cast<float>(green);
            vertex.blue = static_cast<float>(blue);
            if (!finiteVertex(vertex))
            {
                return refused("archBakeInputInvalid",
                    "ARCH bake produced a non-finite sample.", thingIndex);
            }
        }
    }

    MotsArchAuthoringEntry entry{};
    entry.thingIndex = thingIndex;
    entry.origin = occluded
        ? MotsArchSampleOrigin::BakedWorldOccludedV1
        : MotsArchSampleOrigin::BakedSourcePointsV1;
    entry.modelName = std::move(modelName);
    entry.modelEntrySha256Hex = std::move(modelEntrySha256Hex);
    entry.bakeProfile = std::string(archBakeProfileToken(profile));
    entry.bakeInputSha256Hex = archBakeInputDigestHex(profile, lights, meshVertices);
    return staged(level, thingIndex, std::move(baked), std::move(entry));
}

PreparedArchAuthoring prepareUnbindArch(
    const Level& level, const std::int32_t thingIndex,
    const assets::Templates* templates)
{
    if (const auto failure = checkThing(level, thingIndex, templates))
    {
        return *failure;
    }
    PreparedArchAuthoring prepared{};
    prepared.thingIndex = thingIndex;
    prepared.expectedArchIndex =
        level.things[static_cast<std::size_t>(thingIndex)].archLightingIndex;
    prepared.expectedObjectCount = level.motsArchLighting.size();
    prepared.appendObject = false;
    prepared.clearEntry = true;
    if (prepared.expectedArchIndex < 0 && !entryFor(level, thingIndex))
    {
        // Still a valid step: apply clears nothing but keeps the
        // stale check meaningful for undo integration.
    }
    return prepared;
}

} // namespace sed::level
