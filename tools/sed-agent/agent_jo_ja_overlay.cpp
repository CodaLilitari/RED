#include "agent_jo_ja_overlay.hpp"
#include "agent_workspace.hpp"
#include "agent_semantic_preview.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/archive_admission.hpp"
#include "sed/assets/gob.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <optional>

namespace sed_agent
{
namespace
{
using namespace sed::assets;
using Hashes = std::map<std::string, std::string>;

GobArchive archiveFrom(std::string bytes)
{
    if (admitCapturedArchiveIndex(bytes, kRuntimeArchiveMaxEntries).status != ArchiveIndexAdmissionStatus::Accepted)
        throw AgentError { kExitRefusal, "Archive index exceeds admission limits or is malformed.",
            "joJaOverlayArchiveInvalid" };
    const auto* data = reinterpret_cast<const std::byte*>(bytes.data());
    return GobArchive::fromBytes({ data, data + bytes.size() });
}

Hashes digests(const std::vector<RuntimeArchiveEntry>& entries)
{
    Hashes out;
    for (const auto& entry : entries)
        out.emplace(canonicalRuntimeGobEntryName(entry.name), entry.sha256);
    return out;
}

Hashes digests(const GobArchive& archive)
{
    Hashes out;
    std::vector<std::size_t> sizes;
    sizes.reserve(archive.entries().size());
    for (const auto& info : archive.entries())
        sizes.push_back(info.size);
    validateRuntimeArchiveSizes(sizes);
    for (const auto& info : archive.entries())
    {
        const auto name = canonicalRuntimeGobEntryName(info.name);
        if (!out.emplace(name, archive.entrySha256Hex(info.name)).second)
            throw AgentError { kExitRefusal, "Ambiguous authored archive entry: " + name,
                "joJaOverlayArchiveInvalid" };
    }
    return out;
}

const std::string& hashAt(const Hashes& entries, const std::string& key)
{
    static const std::string absent;
    const auto found = entries.find(key);
    return found == entries.end() ? absent : found->second;
}

bool topologyEntry(const std::string& name)
{
    return name.ends_with(".3do") || name.ends_with(".jkl") || name.ends_with(".ndy") || name.ends_with(".cog")
        || name == "episode.jk";
}

bool independentAsset(const std::string& name)
{
    return name.ends_with(".mat") || name.ends_with(".cmp") || name.ends_with(".wav");
}

std::map<std::string, std::string> declarations(const Json& values)
{
    if (values.type != Json::Type::Array || values.array.size() > kRuntimeArchiveMaxEntries)
        throw AgentError { kExitUsage, "overrides must be a bounded array.", "joJaInvalidRequest" };
    static const std::set<std::string> roles = { "sky", "ambience", "palette", "starts", "throwables", "script",
        "authoredLighting", "collision", "gameplay", "visual" };
    std::map<std::string, std::string> specs;
    for (const auto& entry : values.array)
    {
        const Json* name = entry.find("entry");
        const Json* role = entry.find("role");
        if (!entry.isObject() || entry.object.size() != 2 || !name || name->type != Json::Type::String || !role
            || role->type != Json::Type::String || !roles.contains(role->str))
            throw AgentError { kExitUsage, "An override needs exactly entry and a supported role.",
                "joJaInvalidRequest" };
        if (!specs.emplace(canonicalRuntimeGobEntryName(name->str), role->str).second)
            throw AgentError { kExitUsage, "Duplicate override entry: " + name->str, "joJaInvalidRequest" };
    }
    return specs;
}

Json sourceIdentity(const JoJaImportResult& result)
{
    Json out = Json::makeObject();
    out.set("sourceSha256", Json::makeString(result.sourceSha256));
    out.set("recipeSha256", Json::makeString(result.manifestSha256));
    out.set("generatedArchiveSha256", Json::makeString(result.outputSha256));
    out.set("entityCount", Json::makeNumber(static_cast<double>(result.sourceEntities)));
    out.set("brushCount", Json::makeNumber(static_cast<double>(result.sourceBrushes)));
    out.set("surfaceCount", Json::makeNumber(static_cast<double>(result.sourceSurfaces)));
    out.set("shaderCount", Json::makeNumber(static_cast<double>(result.sourceShaders)));
    return out;
}

std::map<std::string, std::set<std::string>> materialShaders(const JoJaImportResult& result)
{
    std::map<std::string, std::set<std::string>> shaders;
    for (const auto& material : result.materials)
        shaders[canonicalRuntimeGobEntryName("mat/" + material.name)].insert(material.shader);
    return shaders;
}

// Field evidence is supplementary to byte identity. Oversized or non-modelled
// historical edits still have exact entry hashes and block publication.
constexpr std::size_t levelEvidenceLimit = 4u * 1024u * 1024u;

std::string entryBytes(const GobArchive& archive, const std::string& name)
{
    const auto raw = archive.read(name);
    return raw.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
}

Json levelChanges(const std::string& old, const std::string& revised)
{
    try
    {
        std::istringstream oldInput(old), newInput(revised);
        const auto source = sed::assets::loadFromJKL(oldInput);
        const auto after = sed::assets::loadFromJKL(newInput);
        const SemanticPreviewLimits limits { 50000, 1024 * 1024, 48, 120, 4096 };
        return buildSemanticPreview(source, after, after, sha256Hex(old), sha256Hex(revised), limits)
            .object.at("authored");
    }
    catch (const std::exception& e)
    {
        Json result = Json::makeObject();
        result.set("readError", Json::makeString(e.what()));
        return result;
    }
}

Json hashOrNull(const std::string& hash) { return hash.empty() ? Json {} : Json::makeString(hash); }

} // namespace

std::map<std::string, std::string> takeGeneratedLevels(JoJaImportResult& baseline)
{
    auto archive = archiveFrom(std::move(baseline.archive.outputData));
    std::map<std::string, std::string> levels;
    for (const auto& entry : archive.entries())
    {
        const auto name = canonicalRuntimeGobEntryName(entry.name);
        if (name.ends_with(".jkl") && entry.size <= levelEvidenceLimit)
            levels.emplace(name, entryBytes(archive, name));
    }
    return levels;
}

Json applyJoJaOverlays(const JoJaImportResult& baseline, JoJaImportResult& next, std::string authoredBytes,
    const Json& overrides, const std::map<std::string, std::string>& baselineLevels)
{
    const auto specs = declarations(overrides);
    const auto authoredHash = sha256Hex(authoredBytes);
    const auto authoredArchive = archiveFrom(std::move(authoredBytes));
    const auto oldHashes = digests(baseline.archive.entries);
    const auto editedHashes = digests(authoredArchive);
    const auto nextHashes = digests(next.archive.entries);
    const auto oldShaders = materialShaders(baseline);
    const auto nextShaders = materialShaders(next);
    std::set<std::string> names;
    for (const auto* table : { &oldHashes, &editedHashes, &nextHashes })
        for (const auto& row : *table)
            names.insert(row.first);
    for (const auto& row : specs)
        names.insert(row.first);
    const bool sourceChanged = baseline.sourceSha256 != next.sourceSha256;
    const bool topologyChanged = sourceChanged
        || std::any_of(names.begin(), names.end(), [&](const std::string& name)
            { return topologyEntry(name) && hashAt(oldHashes, name) != hashAt(nextHashes, name); });

    Json manifest = Json::makeObject();
    manifest.set("schemaVersion", Json::makeNumber(1));
    manifest.set("baseline", sourceIdentity(baseline));
    manifest.set("regenerated", sourceIdentity(next));
    manifest.set("authoredArchiveSha256", Json::makeString(authoredHash));
    Json changes = Json::makeArray();
    Json conflicts = Json::makeArray();
    std::set<std::string> takeAuthored;
    std::optional<GobArchive> generatedCapture;
    auto generatedArchive = [&]() -> const GobArchive&
    {
        if (!generatedCapture)
            generatedCapture = archiveFrom(std::move(next.archive.outputData));
        return *generatedCapture;
    };
    for (const auto& name : names)
    {
        const auto& old = hashAt(oldHashes, name);
        const auto& edited = hashAt(editedHashes, name);
        const auto& generated = hashAt(nextHashes, name);
        const auto spec = specs.find(name);
        if (old == edited && spec == specs.end())
            continue;
        Json row = Json::makeObject();
        row.set("entry", Json::makeString(name));
        row.set("baseSha256", hashOrNull(old));
        row.set("authoredSha256", hashOrNull(edited));
        row.set("generatedSha256", hashOrNull(generated));
        if (spec != specs.end())
            row.set("role", Json::makeString(spec->second));
        std::string reason;
        if (spec == specs.end())
            reason = "undeclaredManualChange";
        else if (old == edited)
            reason = "declaredEntryHasNoManualChange";
        else if (edited == generated)
            row.set("decision", Json::makeString("alreadyPresent"));
        else if (edited.empty() && (name == "episode.jk" || name.ends_with(".jkl")))
            reason = "requiredEpisodeEntry";
        else if (old != generated)
            reason = "bothChanged";
        else if (sourceChanged && !old.empty())
            reason = "sourceIdentityChanged";
        else if (topologyChanged && !independentAsset(name))
            reason = "topologyChanged";
        else if (name.ends_with(".mat") && oldShaders.find(name) != oldShaders.end()
            && (nextShaders.find(name) == nextShaders.end() || oldShaders.at(name) != nextShaders.at(name)))
            reason = "shaderIdentityChanged";
        else
        {
            takeAuthored.insert(name);
            row.set("decision", Json::makeString(edited.empty() ? "remove" : "preserve"));
        }
        if (!reason.empty())
        {
            row.set("reason", Json::makeString(reason));
            if (const auto oldLevel = baselineLevels.find(name); oldLevel != baselineLevels.end())
            {
                if (const auto e = authoredArchive.find(name); e && e->size <= levelEvidenceLimit)
                    row.set("authoredFields", levelChanges(oldLevel->second, entryBytes(authoredArchive, name)));
                if (const auto e = generatedArchive().find(name); e && e->size <= levelEvidenceLimit)
                    row.set(
                        "regeneratedFields", levelChanges(oldLevel->second, entryBytes(generatedArchive(), name)));
            }
            conflicts.array.push_back(row);
        }
        changes.array.push_back(std::move(row));
    }
    manifest.set("changes", std::move(changes));
    manifest.set("conflicts", std::move(conflicts));
    if (!manifest.object.at("conflicts").array.empty())
    {
        Json block = Json::makeObject();
        block.set("error", Json::makeString("joJaReimportConflict"));
        block.set("reimport", std::move(manifest));
        throw AgentError { kExitConflict, "Reimport has unresolved entry conflicts.", "joJaReimportConflict",
            std::move(block), true };
    }
    const auto manifestHash = sha256Hex(toJsonString(manifest));
    if (!takeAuthored.empty())
    {
        std::vector<std::pair<std::string, const GobArchive*>> selected;
        std::vector<std::size_t> sizes;
        for (const auto& name : names)
        {
            const bool authored = takeAuthored.contains(name);
            if (!(authored ? editedHashes : nextHashes).contains(name))
                continue;
            const auto& source = authored ? authoredArchive : generatedArchive();
            selected.emplace_back(name, &source);
            sizes.push_back(source.find(name)->size);
        }
        validateRuntimeArchiveSizes(sizes);
        RuntimeArchiveBuilder builder(selected.size());
        for (const auto& [name, source] : selected)
            builder.add({ name, entryBytes(*source, name) });
        next.archive = builder.finish();
        next.outputSha256 = sha256Hex(next.archive.outputData);
    }
    next.manifestSha256 = manifestHash;
    return manifest;
}
} // namespace sed_agent
