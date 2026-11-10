#include "sed/level/package.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/episode.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/pup.hpp"
#include "sed/assets/snd.hpp"
#include "sed/assets/spr.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <queue>
#include <set>
#include <sstream>
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
    std::string out(path);
    for (char& c : out)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    return lowerOf(out);
}

std::string baseNameOf(std::string_view path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos
        ? std::string(path)
        : std::string(path.substr(slash + 1));
}

bool endsWithCi(const std::string_view text, const std::string_view suffix)
{
    return text.size() >= suffix.size()
        && lowerOf(text.substr(text.size() - suffix.size())) == lowerOf(suffix);
}

std::vector<std::string_view> wordsOf(const std::string_view line)
{
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size()
            && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
        {
            ++i;
        }
        if (i >= line.size())
        {
            break;
        }
        std::size_t j = i;
        while (j < line.size()
            && line[j] != ' ' && line[j] != '\t' && line[j] != '\r')
        {
            ++j;
        }
        words.push_back(line.substr(i, j - i));
        i = j;
    }
    return words;
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
    std::map<std::string, std::size_t> byKey{};
    std::map<std::string, std::vector<std::size_t>> byBase{};
};

const std::string* resolveExactIn(const ArchiveIndex& index, const Ref& ref)
{
    const std::string folderKey = ref.folder.empty()
        ? normalizedOf(ref.name)
        : normalizedOf(ref.folder + "/" + ref.name);
    const std::string nameKey = normalizedOf(ref.name);
    for (const std::string& key : { folderKey, nameKey })
    {
        if (const auto it = index.byKey.find(key); it != index.byKey.end())
        {
            return &index.archive->entries[it->second].second;
        }
    }
    return nullptr;
}

const std::string* resolveBasenameIn(
    const ArchiveIndex& index, const Ref& ref, bool& ambiguous)
{
    ambiguous = false;
    const auto base = index.byBase.find(lowerOf(baseNameOf(ref.name)));
    if (base != index.byBase.end())
    {
        if (base->second.size() == 1)
        {
            return &index.archive->entries[base->second.front()].second;
        }
        ambiguous = true;
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
    const auto addRef = [&refs](std::string folder, const std::string_view name,
                                const char* relation)
    {
        const std::string trimmed = std::string(text::trimView(name));
        if (trimmed.empty() || text::iequalsAscii(trimmed, "none"))
        {
            return;
        }
        refs.push_back(Ref{ std::move(folder), trimmed, relation, 1 });
    };

    static const std::map<std::string, std::pair<std::string, const char*>> kTables{
        { "sounds", { "sound", "jklSound" } },
        { "models", { "3do", "jklModel" } },
        { "sprites", { "misc/spr", "jklSprite" } },
        { "keyframes", { "3do/key", "jklKeyframe" } },
        { "animclass", { "misc/pup", "jklPuppet" } },
        { "soundclass", { "misc/snd", "jklSoundclass" } },
        { "aiclass", { "misc/ai", "jklAiClass" } },
        { "cogscripts", { "cog", "jklCogScript" } },
    };
    static const std::map<std::string, std::pair<std::string, const char*>> kTemplateKeys{
        { "model3d", { "3do", "templateModel" } },
        { "keyframe", { "3do/key", "templateKeyframe" } },
        { "puppet", { "misc/pup", "templatePuppet" } },
        { "sound", { "sound", "templateSound" } },
        { "snd", { "sound", "templateSound" } },
        { "material", { "mat", "templateMaterial" } },
        { "sprite", { "misc/spr", "templateSprite" } },
        { "aiclass", { "misc/ai", "templateAiClass" } },
        { "cog", { "cog", "templateCog" } },
    };

    std::string section;
    bool skipHeaderRow = false;
    bool inColormaps = false;
    std::size_t start = 0;
    const std::string_view text = request.inputBytes;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::string_view line = text.substr(
            start, newline == std::string_view::npos ? std::string_view::npos
                                                     : newline - start);
        start = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        const std::string_view trimmed = text::trimView(line);
        if (trimmed.empty() || trimmed.front() == '#')
        {
            continue;
        }
        const std::string lowered = lowerOf(trimmed);
        if (lowered.rfind("section:", 0) == 0)
        {
            section = lowerOf(text::trimView(
                std::string_view(trimmed).substr(std::string_view("section:").size())));
            skipHeaderRow = true;
            inColormaps = false;
            continue;
        }
        if (text::iequalsAscii(trimmed, "END"))
        {
            section.clear();
            inColormaps = false;
            continue;
        }
        if (section.empty())
        {
            continue;
        }
        if (skipHeaderRow)
        {
            skipHeaderRow = false;
            if (section == "georesource"
                && lowered.rfind("world colormaps", 0) == 0)
            {
                inColormaps = true;
            }
            continue;
        }

        const std::vector<std::string_view> words = wordsOf(trimmed);
        if (words.empty())
        {
            continue;
        }
        if (section == "georesource")
        {
            if (lowered.rfind("world ", 0) == 0)
            {
                inColormaps = lowered.rfind("world colormaps", 0) == 0;
                continue;
            }
            if (inColormaps)
            {
                for (const std::string_view word : words)
                {
                    if (endsWithCi(word, ".cmp"))
                    {
                        addRef("misc/cmp", word, "levelColormap");
                        break;
                    }
                }
            }
            continue;
        }
        if (section == "materials")
        {
            for (const std::string_view word : words)
            {
                if (endsWithCi(word, ".mat"))
                {
                    addRef("mat", word, "jklMaterial");
                    break;
                }
            }
            continue;
        }
        if (section == "templates")
        {
            for (std::size_t w = 2; w < words.size(); ++w)
            {
                const std::size_t eq = words[w].find('=');
                if (eq == std::string_view::npos)
                {
                    continue;
                }
                const auto it = kTemplateKeys.find(
                    lowerOf(words[w].substr(0, eq)));
                if (it != kTemplateKeys.end())
                {
                    addRef(it->second.first, words[w].substr(eq + 1),
                        it->second.second);
                }
            }
            continue;
        }
        if (section == "cogs")
        {
            for (const std::string_view word : words)
            {
                if (endsWithCi(word, ".cog"))
                {
                    addRef("cog", word, "placedCogScript");
                    break;
                }
            }
            continue;
        }
        if (const auto table = kTables.find(section); table != kTables.end())
        {
            for (const std::string_view word : words)
            {
                if (word.find('.') != std::string_view::npos)
                {
                    addRef(table->second.first, word, table->second.second);
                    break;
                }
            }
        }
    }

    for (const PackageExtraResource& extra : request.extraResources)
    {
        refs.push_back(Ref{ extra.folder, extra.name, "extraResource", 1 });
    }
    return refs;
}

std::string buildGobBytes(const std::vector<std::pair<std::string, std::string>>& files)
{
    std::string out;
    const auto putU32 = [&out](const std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            out += static_cast<char>((value >> shift) & 0xFFu);
        }
    };
    out += "GOB ";
    putU32(0x14u);
    putU32(12u);
    putU32(static_cast<std::uint32_t>(files.size()));
    std::uint32_t offset = 16u + static_cast<std::uint32_t>(files.size()) * 136u;
    for (const auto& [name, bytes] : files)
    {
        putU32(offset);
        putU32(static_cast<std::uint32_t>(bytes.size()));
        std::string entryName = name;
        for (char& c : entryName)
        {
            if (c == '/')
            {
                c = '\\';
            }
        }
        entryName.resize(128, '\0');
        out += entryName;
        offset += static_cast<std::uint32_t>(bytes.size());
    }
    for (const auto& [name, bytes] : files)
    {
        (void)name;
        out += bytes;
    }
    return out;
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

    std::vector<ArchiveIndex> indexes;
    for (const PackageArchiveInput& archive : request.archives)
    {
        ArchiveIndex index{};
        index.archive = &archive;
        for (std::size_t i = 0; i < archive.entries.size(); ++i)
        {
            index.byKey.try_emplace(
                normalizedOf(archive.entries[i].first), i);
            index.byBase[lowerOf(baseNameOf(archive.entries[i].first))]
                .push_back(i);
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
    while (!pending.empty())
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
            && nodesByKey.size() + 2 > request.options.maxNodes )
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
        if (endsWithCi(ref.name, ".cog"))
        {
            // The code section can also name resources through literal
            // call arguments, which no scan here follows; disclose that
            // family for every COG in the closure, schema or not.
            scopes.insert("cog-literal-references");
            // Resource-kind symbol defaults name files the engine loads
            // when the level does not supply a value. Only a completely
            // parsed schema is trusted; anything else stays disclosed.
            const sed::assets::CogSchema schema =
                sed::assets::parseCogSchema(ref.name, traversalBytes);
            if (!schema.isComplete())
            {
                scopes.insert("cog-resource-defaults");
            }
            else
            {
                for (const sed::assets::CogSymbol& symbol : schema.symbols)
                {
                    if (!symbol.defaultText)
                    {
                        continue;
                    }
                    const char* folder = nullptr;
                    switch (symbol.kind)
                    {
                    case sed::assets::CogSchemaSymbolKind::sound:
                        folder = "sound";
                        break;
                    case sed::assets::CogSchemaSymbolKind::material:
                        folder = "mat";
                        break;
                    case sed::assets::CogSchemaSymbolKind::model:
                        folder = "3do";
                        break;
                    case sed::assets::CogSchemaSymbolKind::keyframe:
                        folder = "3do/key";
                        break;
                    case sed::assets::CogSchemaSymbolKind::ai:
                        folder = "misc/ai";
                        break;
                    case sed::assets::CogSchemaSymbolKind::cog:
                        folder = "cog";
                        break;
                    default:
                        break;
                    }
                    if (!folder)
                    {
                        continue;
                    }
                    std::string value = std::string(
                        text::trimView(*symbol.defaultText));
                    if (value.size() >= 2
                        && (value.front() == '\'' || value.front() == '"')
                        && value.back() == value.front())
                    {
                        value = std::string(text::trimView(
                            std::string_view(value).substr(
                                1, value.size() - 2)));
                    }
                    if (!value.empty()
                        && value.find_first_of(" \t") == std::string::npos)
                    {
                        pending.push(Ref{ folder, value,
                            "cogDefaultResource", ref.depth + 1 });
                    }
                }
            }
        }
        // KEY and CMP name no outgoing files, so they close as leaves.
        // AI class files have no dependency parser yet and stay disclosed.
        if (endsWithCi(ref.name, ".ai"))
        {
            scopes.insert("subresource-dependencies");
        }
        if (endsWithCi(ref.name, ".pup"))
        {
            // Puppet rows name their keyframe files.
            try
            {
                std::istringstream pupIn(traversalBytes);
                const sed::assets::PupFile pup =
                    sed::assets::loadFromPUP(pupIn);
                for (const std::string& key : pup.keys)
                {
                    if (!key.empty())
                    {
                        pending.push(Ref{ "3do/key", key,
                            "puppetKeyframe", ref.depth + 1 });
                    }
                }
            }
            catch (const std::exception&)
            {
                scopes.insert("subresource-dependencies");
            }
        }
        if (endsWithCi(ref.name, ".spr"))
        {
            // SPR text names its material in the first token.
            try
            {
                std::istringstream sprIn(traversalBytes);
                const std::string material = sed::assets::matFromSPR(sprIn);
                if (!material.empty())
                {
                    pending.push(Ref{ "mat", material,
                        "spriteMaterial", ref.depth + 1 });
                }
                else
                {
                    scopes.insert("subresource-dependencies");
                }
            }
            catch (const std::exception&)
            {
                scopes.insert("subresource-dependencies");
            }
        }
        if (endsWithCi(ref.name, ".snd"))
        {
            // Sound-class rows name their WAV alternatives.
            try
            {
                std::istringstream sndIn(traversalBytes);
                const sed::assets::SndFile snd =
                    sed::assets::loadFromSND(sndIn);
                for (const std::string& wav : snd.wavs)
                {
                    if (!wav.empty())
                    {
                        pending.push(Ref{ "sound", wav,
                            "soundClassWav", ref.depth + 1 });
                    }
                }
            }
            catch (const std::exception&)
            {
                scopes.insert("subresource-dependencies");
            }
        }
        if (endsWithCi(ref.name, ".3do"))
        {
            // 3DO text names its materials; other model subresources are
            // disclosed through the scopes above instead of being guessed.
            std::size_t start = 0;
            const std::string_view modelText = traversalBytes;
            while (start <= modelText.size())
            {
                const std::size_t newline = modelText.find('\n', start);
                const std::string_view line = modelText.substr(start,
                    newline == std::string_view::npos ? std::string_view::npos
                                                      : newline - start);
                start = newline == std::string_view::npos
                    ? modelText.size() + 1 : newline + 1;
                for (const std::string_view word : wordsOf(text::trimView(line)))
                {
                    if (endsWithCi(word, ".mat"))
                    {
                        pending.push(Ref{ "mat", std::string(word),
                            "modelMaterial", ref.depth + 1 });
                    }
                }
            }
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

    plan.nodes = nodesByKey.size() + 2;
    plan.edges = edges;
    plan.unverifiedScopes.assign(scopes.begin(), scopes.end());

    const bool hasError = std::any_of(plan.issues.begin(), plan.issues.end(),
        [](const PackageIssue& issue) { return issue.severity == "error"; });
    std::string output;
    if (!hasError)
    {
        output = buildGobBytes(files);
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
