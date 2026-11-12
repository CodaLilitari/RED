#include "sed/level/package.hpp"

#include "dependency_references.hpp"

#include "sed/assets/episode.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

namespace sed::level
{

namespace
{

std::string lowerOf(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string normalizedOf(std::string_view path)
{
    return detail::normalizeReferenceName(path);
}

std::string baseNameOf(std::string_view path)
{
    return std::string(detail::referenceBaseName(path));
}

bool endsWithCi(const std::string_view text, const std::string_view suffix)
{
    return detail::referenceEndsWithCi(text, suffix);
}

std::string shaOf(const std::string_view bytes)
{
    return sed::assets::jklSourceSha256Hex(bytes);
}

struct Ref
{
    std::string folder{};
    std::string name{};
    std::string relation{};
    std::size_t depth{ 1 };
};

struct ArchiveIndex
{
    const PackageArchiveInput* archive{ nullptr };
    detail::ReferenceArchiveIndex lookup;
};

const std::string* resolveExactIn(const ArchiveIndex& index, const Ref& ref)
{
    const std::string folderKey = ref.folder.empty()
        ? normalizedOf(ref.name)
        : normalizedOf(ref.folder + "/" + ref.name);
    const std::string nameKey = normalizedOf(ref.name);
    if ( const auto found = index.lookup.exact(folderKey, nameKey) )
    {
        return &index.archive->entries[found->ordinal].second;
    }
    return nullptr;
}

const std::string* resolveBasenameIn(
    const ArchiveIndex& index, const Ref& ref, bool& ambiguous)
{
    if ( const auto found = index.lookup.basename(lowerOf(baseNameOf(ref.name)), ambiguous) )
    {
        return &index.archive->entries[found->ordinal].second;
    }
    return nullptr;
}

// Minimal canonical-JSON value: object keys sorted, compact separators.
struct JVal
{
    enum class Kind { Null, Bool, Num, Str, Arr, Obj };
    Kind kind{ Kind::Null };
    bool boolean{ false };
    std::string text{};
    std::vector<JVal> items{};
    std::vector<std::pair<std::string, JVal>> fields{};

    static JVal str(std::string value)
    {
        JVal v; v.kind = Kind::Str; v.text = std::move(value); return v;
    }
    static JVal num(const std::size_t value)
    {
        JVal v; v.kind = Kind::Num; v.text = std::to_string(value); return v;
    }
    static JVal booleanValue(const bool value)
    {
        JVal v; v.kind = Kind::Bool; v.boolean = value; return v;
    }
    static JVal arr(std::vector<JVal> values)
    {
        JVal v; v.kind = Kind::Arr; v.items = std::move(values); return v;
    }
    static JVal obj(std::vector<std::pair<std::string, JVal>> values)
    {
        JVal v; v.kind = Kind::Obj; v.fields = std::move(values); return v;
    }
};

void dumpJson(const JVal& value, std::string& out)
{
    switch (value.kind)
    {
    case JVal::Kind::Null: out += "null"; break;
    case JVal::Kind::Bool: out += value.boolean ? "true" : "false"; break;
    case JVal::Kind::Num: out += value.text; break;
    case JVal::Kind::Str:
    {
        out += '"';
        for (const char c : value.text)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                }
                else
                {
                    out += c;
                }
            }
        }
        out += '"';
        break;
    }
    case JVal::Kind::Arr:
        out += '[';
        for (std::size_t i = 0; i < value.items.size(); ++i)
        {
            if (i) { out += ','; }
            dumpJson(value.items[i], out);
        }
        out += ']';
        break;
    case JVal::Kind::Obj:
    {
        std::vector<const std::pair<std::string, JVal>*> sorted;
        for (const auto& field : value.fields)
        {
            sorted.push_back(&field);
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const auto* a, const auto* b) { return a->first < b->first; });
        out += '{';
        bool first = true;
        for (const auto* field : sorted)
        {
            if (!first) { out += ','; }
            first = false;
            JVal key = JVal::str(field->first);
            dumpJson(key, out);
            out += ':';
            dumpJson(field->second, out);
        }
        out += '}';
        break;
    }
    }
}

std::vector<Ref> collectRootReferences(const PackageRequest& request)
{
    std::vector<Ref> refs;
    detail::ReferenceCallbacks callbacks;
    callbacks.reference = [&refs](const detail::ReferenceView ref)
    {
        refs.push_back(Ref{
            std::string(ref.folder), std::string(ref.name), std::string(ref.relation), 1 });
        return true;
    };
    (void)detail::visitRootReferences(request.inputBytes, request.extraResources, callbacks);
    return refs;
}

} // namespace

PackagePlan planLevelPackage(const PackageRequest& request)
{
    if (!endsWithCi(request.levelFileName, ".jkl")
        || request.levelFileName.find_first_of("/\\") != std::string::npos
        || request.levelFileName == "." || request.levelFileName == "..")
    {
        throw std::invalid_argument(
            "levelFileName must be a plain .jkl file name");
    }
    if (request.episodeTitle.empty() || request.episodeTitle.size() > 80)
    {
        throw std::invalid_argument(
            "episodeTitle must be 1 to 80 printable ASCII characters");
    }
    for (const char c : request.episodeTitle)
    {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc > 0x7E || c == '"' || c == '#')
        {
            throw std::invalid_argument(
                "episodeTitle must be 1 to 80 printable ASCII characters");
        }
    }
    if (request.archiveExtension != ".gob" && request.archiveExtension != ".goo")
    {
        throw std::invalid_argument("archiveExtension must be .gob or .goo");
    }

    PackagePlan plan{};
    plan.archiveExtension = request.archiveExtension;
    const auto addIssue = [&plan](const char* severity, std::string code,
                                  std::string reference, std::string message)
    {
        plan.issues.push_back(PackageIssue{
            severity, std::move(code), std::move(reference), std::move(message) });
    };

    constexpr std::size_t kMandatoryNodes = 2;
    const bool mandatoryNodesFit = request.options.maxNodes >= kMandatoryNodes;
    if ( !mandatoryNodesFit )
    {
        addIssue("error", "PackageBudgetExceeded", request.levelFileName,
            "Package node budget cannot contain the episode and level.");
    }

    std::vector<ArchiveIndex> indexes;
    for (const PackageArchiveInput& archive : request.archives)
    {
        ArchiveIndex index{};
        index.archive = &archive;
        for (std::size_t i = 0; i < archive.entries.size(); ++i)
        {
            index.lookup.add(normalizedOf(archive.entries[i].first),
                lowerOf(baseNameOf(archive.entries[i].first)), i);
        }
        indexes.push_back(std::move(index));
    }

    struct Node
    {
        std::string destPath{};
        std::string bytes{};
        std::string source{};
        std::size_t bytesLen{ 0 };
        std::string sha{};
        bool omitted{ false };
    };
    std::map<std::string, Node> nodesByKey;
    std::map<std::string, std::set<std::string>> resolvedNamesByKey;
    std::vector<std::string> discoveryOrder;
    std::set<std::string> scopes;
    std::queue<Ref> pending;
    for (Ref& ref : collectRootReferences(request))
    {
        pending.push(std::move(ref));
    }

    std::size_t edges = 0;
    std::size_t resolverBytesRemaining = request.options.maxPackageBytes;
    while ( mandatoryNodesFit && !pending.empty() )
    {
        Ref ref = std::move(pending.front());
        pending.pop();
        ++edges;
        if (edges > request.options.maxEdges)
        {
            addIssue("error", "PackageBudgetExceeded", ref.name,
                "Package graph exceeds the edge budget.");
            break;
        }
        if (request.options.maxDepth > 0 && ref.depth > request.options.maxDepth)
        {
            addIssue("error", "PackageBudgetExceeded", ref.name,
                "Package graph exceeds the depth budget.");
            continue;
        }
        const std::string destPath = ref.folder.empty()
            ? baseNameOf(ref.name)
            : ref.folder + "/" + baseNameOf(ref.name);
        const std::string destKey = lowerOf(destPath);
        if ( request.resolveResource && ref.name.find_first_of("/\\:") != std::string::npos )
        {
            const std::string authoredPath = normalizedOf(ref.name);
            if ( authoredPath != normalizedOf(destPath) )
            {
                addIssue("error", "UnsupportedResourcePath", ref.name,
                    "Source-resolved staging does not support noncanonical nested resource paths.");
                continue;
            }
        }
        if (destKey == "episode.jk"
            || destKey == "jkl/" + lowerOf(request.levelFileName))
        {
            addIssue("error", "PackagePathCollision", destPath,
                "A resource destination collides with a generated package path.");
            continue;
        }
        const auto existing = nodesByKey.find(destKey);
        const std::string& authoredKey = ref.name;
        if ( existing != nodesByKey.end()
            && ( !request.resolveResource
                || resolvedNamesByKey[destKey].contains(authoredKey) ) )
        {
            continue;
        }
        if ( existing == nodesByKey.end()
            && nodesByKey.size() >= request.options.maxNodes - kMandatoryNodes )
        {
            addIssue("error", "PackageBudgetExceeded", ref.name,
                "Package graph exceeds the node budget.");
            break;
        }

        const std::string* foundBytes = nullptr;
        const PackageArchiveInput* foundArchive = nullptr;
        std::optional<PackageResolvedResource> resolved;
        if ( request.resolveResource )
        {
            // ResourceLocator tries the authored name before its conventional
            // folder aliases. Passing the flattened destination here could
            // select different bytes from a shadowed same-basename resource.
            resolved = request.resolveResource(ref.name, resolverBytesRemaining);
            if ( resolved && resolved->bytes.size() > resolverBytesRemaining )
            {
                addIssue("error", "PackageResourceTooLarge", ref.name,
                    "The selected resource exceeds the remaining resolver byte budget.");
                break;
            }
            if ( resolved )
            {
                resolverBytesRemaining -= resolved->bytes.size();
                foundBytes = &resolved->bytes;
            }
        }
        bool ambiguous = false;
        // Exact folder/name evidence outranks a basename coincidence in an
        // earlier archive; basename fallback runs only when no archive
        // holds the authored path, preserving archive order within a tier.
        for (const ArchiveIndex& index : indexes)
        {
            if ( foundBytes )
            {
                break;
            }
            if (const std::string* bytes = resolveExactIn(index, ref))
            {
                foundBytes = bytes;
                foundArchive = index.archive;
                break;
            }
        }
        for (const ArchiveIndex& index : indexes)
        {
            if ( foundBytes )
            {
                break;
            }
            bool localAmbiguous = false;
            if (const std::string* bytes = resolveBasenameIn(index, ref, localAmbiguous))
            {
                foundBytes = bytes;
                foundArchive = index.archive;
                break;
            }
            ambiguous = ambiguous || localAmbiguous;
        }
        if (ambiguous && !foundBytes)
        {
            addIssue("error", "AmbiguousResource", ref.name,
                "More than one distinct archive entry matches the reference.");
            nodesByKey.emplace(destKey, Node{ destPath, {}, {}, false });
            discoveryOrder.push_back(destKey);
            continue;
        }
        if (!foundBytes)
        {
            addIssue(request.options.allowUnverifiedReferences ? "warning" : "error",
                "MissingResource", ref.name,
                "The reference was not found in the ordered resource archives.");
            continue;
        }

        if ( existing != nodesByKey.end() )
        {
            // Distinct authored aliases can select different ResourceLocator
            // winners even when they flatten to the same engine output path.
            // Resolve each spelling once, charging its read to the same budget.
            if ( existing->second.bytes != *foundBytes )
            {
                addIssue("error", "PackagePathCollision", ref.name,
                    "Distinct authored resource names select different bytes for the same package path.");
                continue;
            }
            resolvedNamesByKey[destKey].insert(authoredKey);
            const bool omitted = resolved ? resolved->stock : foundArchive->stock;
            if ( existing->second.omitted && !omitted )
            {
                existing->second.omitted = false;
                existing->second.source = resolved ? resolved->source : foundArchive->label;
            }
            continue;
        }

        Node node{};
        node.destPath = destPath;
        node.source = resolved ? resolved->source : foundArchive->label;
        node.omitted = resolved ? resolved->stock : foundArchive->stock;
        node.bytesLen = foundBytes->size();
        node.sha = shaOf(*foundBytes);
        if ( !node.omitted || request.resolveResource )
        {
            node.bytes = *foundBytes;
        }
        nodesByKey.emplace(destKey, node);
        if ( request.resolveResource )
        {
            resolvedNamesByKey[destKey].insert(authoredKey);
        }
        discoveryOrder.push_back(destKey);

        // Stock entries are omitted from the output, but their own
        // dependencies are still traversed from the archive bytes.
        const std::string& traversalBytes =
            node.omitted ? *foundBytes : node.bytes;
        detail::ReferenceCallbacks callbacks;
        callbacks.reference = [&pending, &ref](const detail::ReferenceView child)
        {
            pending.push(Ref{std::string(child.folder), std::string(child.name),
                std::string(child.relation), ref.depth + 1});
            return true;
        };
        callbacks.scope = [&scopes](const std::string_view scope)
        {
            scopes.emplace(scope);
        };
        // These readers historically disclose a subresource scope after a
        // parser or queue-allocation exception; COG and 3DO exceptions propagate.
        if ( endsWithCi(ref.name, ".pup") || endsWithCi(ref.name, ".spr")
            || endsWithCi(ref.name, ".snd") )
        {
            try
            {
                (void)detail::visitChildReferences(ref.name, traversalBytes, callbacks);
            }
            catch ( const std::exception& )
            {
                scopes.insert("subresource-dependencies");
            }
        }
        else
        {
            (void)detail::visitChildReferences(ref.name, traversalBytes, callbacks);
        }
    }

    sed::assets::EpisodeFile episode{};
    episode.name = request.episodeTitle;
    episode.type = static_cast<int>(request.episodeType);
    sed::assets::EpisodeSequence sequence{};
    sequence.lineNumber = 0;
    sequence.cdNumber = 1;
    sequence.levelNumber = 1;
    sequence.type = "LEVEL";
    sequence.fileName = request.levelFileName;
    episode.sequences.push_back(sequence);
    const std::string episodeBytes = sed::assets::saveToEpisodeJkText(episode);

    std::vector<std::pair<std::string, std::string>> files;
    files.emplace_back("episode.jk", episodeBytes);
    files.emplace_back("jkl/" + request.levelFileName, request.inputBytes);
    plan.entries.push_back(PackageEntry{
        "episode.jk", "generated:episode", episodeBytes.size(), shaOf(episodeBytes), false });
    plan.entries.push_back(PackageEntry{
        "jkl/" + request.levelFileName, "source:level",
        request.inputBytes.size(), shaOf(request.inputBytes), false });

    for (const std::string& key : discoveryOrder)
    {
        const Node& node = nodesByKey.at(key);
        PackageEntry entry{
            node.destPath, node.source, node.bytesLen, node.sha, node.omitted };
        if (node.omitted)
        {
            plan.stockOmissions.push_back(entry);
            continue;
        }
        std::string archiveName = node.destPath;
        for (char& c : archiveName)
        {
            if (c == '/')
            {
                c = '\\';
            }
        }
        if (archiveName.size() > 128)
        {
            addIssue("error", "PackageEntryNameTooLong", node.destPath,
                "A package entry name exceeds 128 bytes.");
            continue;
        }
        files.emplace_back(node.destPath, node.bytes);
        plan.entries.push_back(std::move(entry));
    }

    plan.nodes = nodesByKey.size() + kMandatoryNodes;
    plan.edges = edges;
    plan.unverifiedScopes.assign(scopes.begin(), scopes.end());

    const bool hasError = std::any_of(plan.issues.begin(), plan.issues.end(),
        [](const PackageIssue& issue) { return issue.severity == "error"; });
    std::string output;
    if (!hasError)
    {
        output = sed::assets::buildGobBytes(files);
        if (output.size() > request.options.maxPackageBytes)
        {
            addIssue("error", "PackageTooLarge", request.levelFileName,
                "The built package exceeds the package byte budget.");
            output.clear();
        }
    }
    plan.canBuild = !hasError && !output.empty();
    if (plan.canBuild)
    {
        plan.outputData = output;
        plan.outputBytes = output.size();
        plan.outputSha256 = shaOf(output);
    }

    std::vector<JVal> entryJson;
    for (const PackageEntry& entry : plan.entries)
    {
        entryJson.push_back(JVal::obj({
            { "bytes", JVal::num(entry.bytes) },
            { "path", JVal::str(entry.path) },
            { "sha256", JVal::str(entry.sha256) },
            { "source", JVal::str(entry.source) },
        }));
    }
    std::vector<JVal> omissionJson;
    for (const PackageEntry& entry : plan.stockOmissions)
    {
        omissionJson.push_back(JVal::obj({
            { "bytes", JVal::num(entry.bytes) },
            { "path", JVal::str(entry.path) },
            { "sha256", JVal::str(entry.sha256) },
            { "source", JVal::str(entry.source) },
        }));
    }
    std::vector<JVal> issueJson;
    for (const PackageIssue& issue : plan.issues)
    {
        issueJson.push_back(JVal::obj({
            { "code", JVal::str(issue.code) },
            { "message", JVal::str(issue.message) },
            { "reference", JVal::str(issue.reference) },
            { "severity", JVal::str(issue.severity) },
        }));
    }
    std::vector<JVal> archiveJson;
    for (const PackageArchiveInput& archive : request.archives)
    {
        std::string digestInput;
        for (const auto& [name, bytes] : archive.entries)
        {
            digestInput += normalizedOf(name);
            digestInput += '\0';
            digestInput += bytes;
            digestInput += '\0';
        }
        archiveJson.push_back(JVal::obj({
            { "fileName", JVal::str(archive.label) },
            { "role", JVal::str(archive.stock ? "stock" : "resource") },
            { "sha256", JVal::str(shaOf(digestInput)) },
        }));
    }
    std::vector<JVal> scopeJson;
    for (const std::string& scope : plan.unverifiedScopes)
    {
        scopeJson.push_back(JVal::str(scope));
    }

    const JVal manifest = JVal::obj({
        { "archiveExtension", JVal::str(plan.archiveExtension) },
        { "canBuild", JVal::booleanValue(plan.canBuild) },
        { "edges", JVal::num(plan.edges) },
        { "entries", JVal::arr(std::move(entryJson)) },
        { "episode", JVal::obj({
            { "episodeTitle", JVal::str(request.episodeTitle) },
            { "episodeType", JVal::num(
                static_cast<std::size_t>(request.episodeType)) },
            { "fileName", JVal::str("episode.jk") },
            { "levelFileName", JVal::str(request.levelFileName) },
        }) },
        { "inputSha256", JVal::str(shaOf(request.inputBytes)) },
        { "issues", JVal::arr(std::move(issueJson)) },
        { "nodes", JVal::num(plan.nodes) },
        { "options", JVal::obj({
            { "allowUnverifiedReferences",
                JVal::booleanValue(request.options.allowUnverifiedReferences) },
            { "maxDepth", JVal::num(request.options.maxDepth) },
            { "maxEdges", JVal::num(request.options.maxEdges) },
            { "maxNodes", JVal::num(request.options.maxNodes) },
            { "maxPackageBytes", JVal::num(request.options.maxPackageBytes) },
        }) },
        { "outputSha256", JVal::str(plan.outputSha256) },
        { "runtimeClosureVerified", JVal::booleanValue(false) },
        { "sourceArchives", JVal::arr(std::move(archiveJson)) },
        { "stockOmissions", JVal::arr(std::move(omissionJson)) },
        { "unverifiedScopes", JVal::arr(std::move(scopeJson)) },
    });
    std::string manifestText;
    dumpJson(manifest, manifestText);
    plan.manifestSha256 = shaOf(manifestText);
    return plan;
}

} // namespace sed::level
