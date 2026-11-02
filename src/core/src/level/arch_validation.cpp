#include "sed/level/arch_validation.hpp"

#include "sed/assets/sed.hpp"
#include "sed/assets/source_lights.hpp"
#include "sed/level/arch_lighting_clipboard.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sed::level
{

namespace
{

constexpr std::size_t kMaxTopologyGeosets = 64;
constexpr std::size_t kMaxTopologyMeshes = 100000;
constexpr std::size_t kMaxTopologyVertices = 1000000;
constexpr std::int32_t kArchFlag = 0x8;

std::string sha256Hex(const std::string_view text)
{
    return assets::sourceLightSha256Hex(text);
}

std::string modelNameOf(
    const Level& level, const assets::Templates* templates,
    const Thing& thing)
{
    const TplValue* model = thingFindValue(level, templates, thing, "model3d");
    if (!model)
    {
        model = thingFindValue(level, templates, thing, "model3do");
    }
    return model ? model->asString() : std::string{};
}

bool hexEqual(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto lower = [](const char ch) noexcept
        {
            return (ch >= 'A' && ch <= 'F')
                ? static_cast<char>(ch - 'A' + 'a') : ch;
        };
        if (lower(left[i]) != lower(right[i]))
        {
            return false;
        }
    }
    return true;
}

ArchBindingIssue issue(
    const char* code, const std::string& message, const bool blocking,
    const std::int32_t thingIndex, const std::int32_t geosetIndex = -1)
{
    return ArchBindingIssue{
        std::string(code), message, blocking, thingIndex, geosetIndex };
}

} // namespace

bool ArchValidationReport::usable() const noexcept
{
    for (const ArchBindingResult& binding : bindings)
    {
        if (!binding.usable)
        {
            return false;
        }
    }
    return true;
}

std::string archModelFingerprintHex(const MotsArchLightingTopology& topology)
{
    std::string text = "sed-arch-model-topology-v1\n";
    text += std::to_string(topology.geosets.size());
    text.push_back('\n');
    for (const std::vector<std::size_t>& geoset : topology.geosets)
    {
        text += std::to_string(geoset.size());
        for (const std::size_t count : geoset)
        {
            text.push_back(' ');
            text += std::to_string(count);
        }
        text.push_back('\n');
    }
    return sha256Hex(text);
}

std::optional<MotsArchLightingTopology> readArchModelTopology(
    const std::string_view text, std::string* error)
{
    const auto fail = [error](const std::string& message)
        -> std::optional<MotsArchLightingTopology>
    {
        if (error)
        {
            *error = message;
        }
        return std::nullopt;
    };

    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t nl = text.find('\n', start);
        std::string_view line = nl == std::string_view::npos
            ? text.substr(start) : text.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (nl == std::string_view::npos)
        {
            break;
        }
        start = nl + 1;
    }

    std::size_t geosetCount = 1;
    bool sawGeosets = false;
    for (const std::string_view line : lines)
    {
        const auto tokens = text::splitWords(line);
        if (tokens.size() >= 2 && text::iequalsAscii(tokens[0], "GEOSETS"))
        {
            const auto parsed = text::parseInt<std::int64_t>(tokens[1]);
            if (!parsed || *parsed < 1
                || *parsed > static_cast<std::int64_t>(kMaxTopologyGeosets))
            {
                return fail("Cannot read 3DO topology: GEOSETS count is invalid.");
            }
            geosetCount = static_cast<std::size_t>(*parsed);
            sawGeosets = true;
        }
    }

    // Collect each GEOSET block in physical order.
    struct Block { std::size_t begin; std::size_t end; };
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const auto tokens = text::splitWords(lines[i]);
        if (!tokens.empty() && text::iequalsAscii(tokens[0], "GEOSET")
            && !(tokens.size() >= 1 && text::iequalsAscii(tokens[0], "GEOSETS")))
        {
            if (!blocks.empty())
            {
                blocks.back().end = i;
            }
            blocks.push_back(Block{ i, lines.size() });
        }
    }
    if (blocks.empty())
    {
        return fail("Cannot read 3DO topology: no GEOSET block is present.");
    }
    if (sawGeosets && blocks.size() != geosetCount)
    {
        return fail("Cannot read 3DO topology: GEOSET blocks do not match the declared count.");
    }
    if (blocks.size() > kMaxTopologyGeosets)
    {
        return fail("Cannot read 3DO topology: geoset budget is exceeded.");
    }

    MotsArchLightingTopology topology{};
    std::size_t meshTotal = 0;
    std::size_t vertexTotal = 0;
    for (const Block& block : blocks)
    {
        std::size_t idx = block.begin + 1;
        std::int64_t meshCount = -1;
        for (; idx < block.end; ++idx)
        {
            const auto tokens = text::splitWords(lines[idx]);
            if (tokens.size() >= 2 && text::iequalsAscii(tokens[0], "MESHES"))
            {
                const auto parsed = text::parseInt<std::int64_t>(tokens[1]);
                if (!parsed || *parsed < 0)
                {
                    return fail("Cannot read 3DO topology: MESHES count is invalid.");
                }
                meshCount = *parsed;
                ++idx;
                break;
            }
        }
        if (meshCount < 0)
        {
            return fail("Cannot read 3DO topology: a GEOSET has no MESHES count.");
        }
        if (static_cast<std::size_t>(meshCount) > kMaxTopologyMeshes - meshTotal)
        {
            return fail("Cannot read 3DO topology: mesh budget is exceeded.");
        }
        meshTotal += static_cast<std::size_t>(meshCount);

        std::vector<std::size_t> geoset;
        geoset.reserve(static_cast<std::size_t>(meshCount));
        for (std::int64_t mesh = 0; mesh < meshCount; ++mesh)
        {
            // Advance to this mesh header, then its VERTICES count.
            bool foundMesh = false;
            for (; idx < block.end; ++idx)
            {
                const auto tokens = text::splitWords(lines[idx]);
                if (!tokens.empty() && text::iequalsAscii(tokens[0], "MESH"))
                {
                    foundMesh = true;
                    ++idx;
                    break;
                }
            }
            if (!foundMesh)
            {
                return fail("Cannot read 3DO topology: a declared mesh is missing.");
            }
            bool foundVertices = false;
            for (; idx < block.end; ++idx)
            {
                const auto tokens = text::splitWords(lines[idx]);
                if (!tokens.empty() && text::iequalsAscii(tokens[0], "MESH"))
                {
                    break;
                }
                if (tokens.size() >= 2 && text::iequalsAscii(tokens[0], "VERTICES"))
                {
                    const auto parsed = text::parseInt<std::int64_t>(tokens[1]);
                    if (!parsed || *parsed < 0)
                    {
                        return fail("Cannot read 3DO topology: VERTICES count is invalid.");
                    }
                    if (static_cast<std::size_t>(*parsed) > kMaxTopologyVertices - vertexTotal)
                    {
                        return fail("Cannot read 3DO topology: vertex budget is exceeded.");
                    }
                    vertexTotal += static_cast<std::size_t>(*parsed);
                    geoset.push_back(static_cast<std::size_t>(*parsed));
                    foundVertices = true;
                    ++idx;
                    break;
                }
            }
            if (!foundVertices)
            {
                return fail("Cannot read 3DO topology: a mesh has no VERTICES count.");
            }
            // Skip this mesh's rows up to the next mesh header.
            for (; idx < block.end; ++idx)
            {
                const auto tokens = text::splitWords(lines[idx]);
                if (!tokens.empty() && text::iequalsAscii(tokens[0], "MESH"))
                {
                    break;
                }
            }
        }
        topology.geosets.push_back(std::move(geoset));
    }
    return topology;
}

bool archTopologyAllowsBinding(
    const MotsArchLightObject& stored,
    const MotsArchLightingTopology& topology,
    std::string* code) noexcept
{
    const auto reject = [code](const char* value) noexcept
    {
        if (code)
        {
            *code = value;
        }
        return false;
    };
    if (topology.geosets.empty())
    {
        return reject("archTopologyMissing");
    }
    const std::vector<std::size_t>& first = topology.geosets.front();
    if (first.size() != stored.meshes.size())
    {
        return reject("archGeosetZeroMismatch");
    }
    for (std::size_t m = 0; m < first.size(); ++m)
    {
        if (first[m] != stored.meshes[m].vertices.size())
        {
            return reject("archGeosetZeroMismatch");
        }
    }
    for (std::size_t g = 1; g < topology.geosets.size(); ++g)
    {
        const std::vector<std::size_t>& geoset = topology.geosets[g];
        if (geoset.size() > stored.meshes.size())
        {
            return reject("archLowerLodOverflow");
        }
        for (std::size_t m = 0; m < geoset.size(); ++m)
        {
            if (geoset[m] > stored.meshes[m].vertices.size())
            {
                return reject("archLowerLodOverflow");
            }
        }
    }
    if (code)
    {
        code->clear();
    }
    return true;
}

ArchValidationReport validateArchBindings(
    const Level& level,
    const std::span<const ArchBindingExpectation> expectations,
    const std::span<const ArchStoredFingerprint> storedFingerprints,
    const assets::Templates* templates)
{
    std::unordered_map<std::int32_t, const ArchBindingExpectation*> byThing;
    for (const ArchBindingExpectation& item : expectations)
    {
        byThing.emplace(item.thingIndex, &item);
    }
    std::unordered_map<std::int32_t, std::string> storedByThing;
    for (const ArchStoredFingerprint& item : storedFingerprints)
    {
        storedByThing.emplace(item.thingIndex, item.fingerprintHex);
    }

    ArchValidationReport report{};
    for (std::size_t i = 0; i < level.things.size(); ++i)
    {
        const Thing& thing = level.things[i];
        const auto index = static_cast<std::int32_t>(i);
        const bool flagged = (thingFlags(level, templates, thing) & kArchFlag) != 0;
        const bool bound = thing.archLightingIndex >= 0;
        if (!flagged && !bound)
        {
            continue;
        }
        ArchBindingResult result{};
        result.thingIndex = index;
        result.usable = true;

        if (!flagged)
        {
            result.issues.push_back(issue("archThingFlagMissing",
                "ARCH binding exists without thingflags 0x8.", true, index));
        }
        if (thing.archLightingIndex < -1
            || (bound && static_cast<std::size_t>(thing.archLightingIndex)
                >= level.motsArchLighting.size()))
        {
            result.issues.push_back(issue("archIndexInvalid",
                "ARCH lighting index is outside the stored arrays.", true, index));
            result.usable = false;
            report.bindings.push_back(std::move(result));
            continue;
        }
        if (!bound)
        {
            // Flagged but unbound: no arrays to authorize.
            result.usable = false;
            result.issues.push_back(issue("archUnbound",
                "ARCH thing has no bound lighting arrays.", true, index));
            report.bindings.push_back(std::move(result));
            continue;
        }

        const auto found = byThing.find(index);
        if (found == byThing.end())
        {
            result.usable = false;
            result.issues.push_back(issue("archModelMissing",
                "No explicitly resolved model authorizes this binding.", true, index));
            report.bindings.push_back(std::move(result));
            continue;
        }
        const ArchBindingExpectation& expected = *found->second;

        const std::string thingModel = modelNameOf(level, templates, thing);
        if (thingModel.empty() || expected.modelName.empty())
        {
            result.usable = false;
            result.issues.push_back(issue("archModelMissing",
                "ARCH thing or resolved model has no model name.", true, index));
        }
        else if (normalizeMotsArchLightingModelIdentity(thingModel)
            != normalizeMotsArchLightingModelIdentity(expected.modelName))
        {
            result.usable = false;
            result.issues.push_back(issue("archModelNameMismatch",
                "Resolved model name does not match the thing model.", true, index));
        }

        const MotsArchLightObject& stored =
            level.motsArchLighting[static_cast<std::size_t>(thing.archLightingIndex)];
        std::string topologyCode;
        if (!archTopologyAllowsBinding(stored, expected.topology, &topologyCode))
        {
            result.usable = false;
            std::int32_t geosetIndex = -1;
            if (topologyCode == "archLowerLodOverflow")
            {
                // Locate the first overflowing lower geoset for the report.
                for (std::size_t g = 1; g < expected.topology.geosets.size(); ++g)
                {
                    const std::vector<std::size_t>& geoset = expected.topology.geosets[g];
                    bool over = geoset.size() > stored.meshes.size();
                    for (std::size_t m = 0; !over && m < geoset.size(); ++m)
                    {
                        over = geoset[m] > stored.meshes[m].vertices.size();
                    }
                    if (over)
                    {
                        geosetIndex = static_cast<std::int32_t>(g);
                        break;
                    }
                }
            }
            const char* message = topologyCode == "archLowerLodOverflow"
                ? "A lower LOD needs more ARCH slots than the stored arrays hold."
                : (topologyCode == "archTopologyMissing"
                    ? "Resolved model has no usable topology."
                    : "Geoset zero does not match the stored ARCH arrays exactly.");
            result.issues.push_back(issue(
                topologyCode.c_str(), message, true, index, geosetIndex));
        }

        const auto storedFound = storedByThing.find(index);
        if (storedFound == storedByThing.end()
            || !expected.resolvedFingerprintHex.has_value())
        {
            result.fingerprintChecked = false;
            result.issues.push_back(issue("archFingerprintUnknown",
                "No stored fingerprint baseline is available to verify.", false, index));
        }
        else
        {
            result.fingerprintChecked = true;
            if (!hexEqual(storedFound->second, *expected.resolvedFingerprintHex))
            {
                result.usable = false;
                result.issues.push_back(issue("archFingerprintMismatch",
                    "Stored fingerprint does not match the resolved model.", true, index));
            }
        }

        for (const ArchBindingIssue& item : result.issues)
        {
            if (item.blocking)
            {
                result.usable = false;
            }
        }
        report.bindings.push_back(std::move(result));
    }
    return report;
}

} // namespace sed::level
