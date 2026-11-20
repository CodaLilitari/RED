#include "resource_families.hpp"

#include "level/dependency_references.hpp"
#include "sed/assets/tpl.hpp"
#include "sed/assets/3do.hpp"
#include "sed/level/geometry.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace sed::project
{
namespace
{
namespace scan = level::detail;
constexpr std::size_t maxEntries = 100000;
constexpr std::size_t maxEdges = 100000;
constexpr std::size_t maxScanBytes = 64u << 20;
constexpr std::size_t maxWork = 2000000;

std::string keyFor(std::string_view name)
{
    return scan::normalizeReferenceName(name);
}

std::string kindFor(const std::string &name)
{
    static const std::map<std::string, std::string> types{
        {".3do", "Model"},  {".mat", "Material"}, {".key", "Animation"},   {".pup", "Puppet"},
        {".cog", "Script"}, {".wav", "Sound"},    {".snd", "Sound class"}, {".tpl", "Template library"},
        {".ai", "AI"},      {".spr", "Sprite"},   {".cmp", "Palette"},     {".par", "Particles"}};
    const auto found = types.find(std::filesystem::path(name).extension().string());
    return found == types.end() ? std::string{} : found->second;
}

std::string capturedText(const ResourceFamilyIndex &index, const ResourceLocation &winner, std::size_t &budget)
{
    if ( winner.entry.size > budget )
    {
        throw std::runtime_error("Resource-family inspection reached its byte budget.");
    }
    budget -= winner.entry.size;
    const auto bytes = index.sources.mounts[winner.mount].archive.read(winner.entry.name);
    return bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::string templateSection(std::string_view level)
{
    std::istringstream lines{std::string(level)};
    std::string rows, line;
    bool inside = false;
    while ( std::getline(lines, line) )
    {
        const auto text = sed::text::lowerAscii(sed::text::trimView(line));
        if ( text.starts_with("section:") )
        {
            inside = sed::text::trimView(std::string_view(text).substr(8)) == "templates";
        }
        else if ( inside && text == "end" )
        {
            inside = false;
        }
        else if ( inside && !text.starts_with("world templates") )
        {
            rows += line;
            rows += '\n';
        }
    }
    return rows;
}

std::set<std::string> connected(const ResourceFamilyIndex &index, const std::string &key, bool inbound)
{
    std::set<std::string> seen;
    std::vector<std::string> pending{key};
    while ( !pending.empty() )
    {
        std::string current = std::move(pending.back());
        pending.pop_back();
        const auto row = index.rows.find(current);
        if ( row == index.rows.end() || !seen.insert(current).second )
        {
            continue;
        }
        if ( inbound )
        {
            pending.insert(pending.end(), row->second.users.begin(), row->second.users.end());
        }
        else
        {
            for ( const auto &[child, reason] : row->second.dependencies )
            {
                pending.push_back(child);
            }
        }
    }
    return seen;
}
} // namespace

ResourceFamilyIndex indexProjectAssets(AssetSources sources)
{
    ResourceFamilyIndex index;
    index.sources = std::move(sources);
    index.previewResources.setArchivesOnly(true);
    for ( auto i = index.sources.mounts.rbegin(); i != index.sources.mounts.rend(); ++i )
    {
        index.previewResources.mountGob(i->archive);
    }

    std::size_t entryCount{}, edgeCount{}, scanBudget = maxScanBytes, work = maxWork;
    std::vector<std::string> named;
    const auto remember = [&](const std::string &authored) -> std::string
    {
        const auto key = keyFor(authored);
        if ( index.rows.contains(key) )
        {
            return key;
        }
        if ( index.rows.size() >= maxEntries )
        {
            throw std::runtime_error("Too many distinct project resource names.");
        }
        AssetRow row;
        row.name = authored;
        row.kind = kindFor(key);
        if ( row.kind.empty() )
        {
            row.kind = "Resource";
        }
        const auto matches = projectResourceLocations(index.sources.mounts, authored);
        row.missing = matches.empty();
        for ( const auto &match : matches )
        {
            const auto &mount = index.sources.mounts[match.mount];
            const auto location = mount.label + " / " + match.entry.name;
            if ( !row.winner )
            {
                row.winner = match;
                row.location = location;
                row.stock = mount.stock;
            }
            else
            {
                row.shadows.push_back(location);
            }
        }
        index.rows.emplace(key, std::move(row));
        named.push_back(key);
        return key;
    };
    for ( const auto &mount : index.sources.mounts )
    {
        for ( const auto &entry : mount.archive.entries() )
        {
            if ( ++entryCount > maxEntries )
            {
                throw std::runtime_error("Too many mounted archive entries.");
            }
            const auto path = keyFor(entry.name);
            if ( kindFor(path).empty() )
            {
                continue;
            }
            const std::string base(scan::referenceBaseName(path));
            const auto matches = projectResourceLocations(index.sources.mounts, base);
            const bool known =
                std::ranges::any_of(matches, [&](const auto &match) { return keyFor(match.entry.name) == path; });
            remember(known ? base : path);
        }
    }

    const auto edge = [&](const std::string &parent, const std::string &child, std::string reason)
    {
        if ( ++edgeCount > maxEdges )
        {
            throw std::runtime_error("Too many project resource associations.");
        }
        index.rows.at(parent).dependencies.emplace(child, std::move(reason));
        index.rows.at(child).users.insert(parent);
    };
    const auto callbacks = [&](const std::string &parent)
    {
        scan::ReferenceCallbacks c;
        c.work = [&]
        {
            if ( work == 0 )
            {
                throw std::runtime_error("Resource inspection reached its traversal budget.");
            }
            --work;
            return true;
        };
        c.reference = [&, parent](scan::ReferenceView ref)
        {
            edge(parent, remember(std::string(ref.name)), std::string(ref.relation));
            return true;
        };
        return c;
    };
    const auto templates = [&](const std::string &owner, const std::string &bytes)
    {
        std::istringstream input(bytes);
        const auto database = assets::loadTemplates(input);
        const auto templateKey = [&](std::string_view name) { return "template:" + owner + "#" + keyFor(name); };
        for ( std::size_t i = 0; i < database.size(); ++i )
        {
            const auto &tpl = *database.get(i);
            const auto key = templateKey(tpl.name);
            if ( !index.rows.contains(key) && index.rows.size() >= maxEntries )
            {
                throw std::runtime_error("Too many distinct project resource names.");
            }
            auto &row = index.rows[key];
            row.name = tpl.name;
            row.kind = "Template";
            row.location = index.rows.at(owner).location;
            row.authoringTemplate = tpl;
            row.missing = false;
            edge(owner, key, "declares template");
            if ( !tpl.parent.empty() && !sed::text::iequalsAscii(tpl.parent, "none") )
            {
                const auto parentKey = templateKey(tpl.parent);
                if ( !index.rows.contains(parentKey) && index.rows.size() >= maxEntries )
                {
                    throw std::runtime_error("Too many distinct project resource names.");
                }
                auto &parent = index.rows[parentKey];
                if ( parent.kind.empty() )
                {
                    parent.name = tpl.parent;
                    parent.kind = "Template";
                    parent.missing = true;
                }
                edge(key, parentKey, "inherits template");
            }
            assets::Templates single;
            single.addOrUpdate(tpl);
            std::ostringstream text;
            assets::saveTemplates(text, single);
            scan::visitRootReferences("SECTION: TEMPLATES\nWORLD TEMPLATES 1\n" + text.str() + "END\n", {},
                                      callbacks(key));
        }
    };
    for ( const auto &[name, bytes] : index.sources.levels )
    {
        const auto key = "level:" + name;
        if ( !index.rows.contains(key) && index.rows.size() >= maxEntries )
        {
            throw std::runtime_error("Too many distinct project resource names.");
        }
        auto &row = index.rows[key];
        row.name = name;
        row.kind = "Level";
        row.location = name;
        scan::visitRootReferences(bytes, {}, callbacks(key));
        templates(key, templateSection(bytes));
    }
    for ( std::size_t i = 0; i < named.size(); ++i )
    {
        const auto key = named[i];
        const auto &row = index.rows.at(key);
        if ( !row.winner )
        {
            continue;
        }
        if ( row.kind == "Template library" )
        {
            templates(key, capturedText(index, *row.winner, scanBudget));
        }
        else if ( row.kind == "Model" || row.kind == "Puppet" || row.kind == "Sound class" ||
                  row.kind == "Script" || row.kind == "Sprite" )
        {
            scan::visitChildReferences(row.name, capturedText(index, *row.winner, scanBudget), callbacks(key));
        }
    }
    return index;
}

std::set<std::string> resourceFamily(const ResourceFamilyIndex &index, const std::string &key)
{
    return connected(index, key, false);
}
std::set<std::string> affectedResources(const ResourceFamilyIndex &index, const std::string &key)
{
    return connected(index, key, true);
}

FamilyInsertion prepareFamily(const ResourceFamilyIndex &index, const std::string &key, const Level &level,
                              const assets::ResourceLocator &live, std::string name,
                              const assets::Templates *global)
{
    const auto &root = index.rows.at(key);
    if ( !root.authoringTemplate )
    {
        throw std::runtime_error("Select an actor template.");
    }
    const auto scope = resourceFamily(index, key);
    if ( name.empty() || name.size() > 63 ||
         name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_+-") !=
             std::string::npos )
    {
        throw std::runtime_error(
            "Choose a template name using letters, numbers, underscores, + or - (up to 63 characters).");
    }
    FamilyInsertion result;
    result.name = std::move(name);
    // Preserve parent-before-child order for the exported native template section.
    std::vector<const ThingTemplate *> chain;
    std::set<std::string> visitedTemplates;
    std::string next = key;
    while ( !next.empty() )
    {
        const auto &row = index.rows.at(next);
        if ( !row.authoringTemplate )
        {
            throw std::runtime_error("Missing parent template: " + row.name);
        }
        if ( !visitedTemplates.insert(next).second )
        {
            throw std::runtime_error("The selected template has a parent cycle.");
        }
        chain.push_back(&*row.authoringTemplate);
        next.clear();
        for ( const auto &[dependency, reason] : row.dependencies )
        {
            if ( reason == "inherits template" )
            {
                next = dependency;
            }
        }
    }
    const auto same = [](const ThingTemplate &a, const ThingTemplate &b)
    {
        assets::Templates first, second;
        first.addOrUpdate(a);
        second.addOrUpdate(b);
        std::ostringstream left, right;
        assets::saveTemplates(left, first);
        assets::saveTemplates(right, second);
        return left.str() == right.str();
    };
    for ( auto it = chain.rbegin(); it != chain.rend(); ++it )
    {
        auto definition = **it;
        if ( *it == chain.front() )
        {
            definition.name = result.name;
        }
        else if ( text::iequalsAscii(definition.name, result.name) )
        {
            throw std::runtime_error("The new name belongs to the family’s parent template.");
        }
        const auto existing = std::ranges::find_if(level.templates, [&](const auto &old)
                                                   { return text::iequalsAscii(old.name, definition.name); });
        if ( existing == level.templates.end() )
        {
            const auto *fallback = global ? global->find(definition.name) : nullptr;
            if ( fallback && !same(*fallback, definition) )
            {
                throw std::runtime_error("Different global template " + definition.name +
                                         ". Choose a new name or reconcile its parent.");
            }
            result.templates.push_back(std::move(definition));
        }
        else if ( !same(*existing, definition) )
        {
            throw std::runtime_error(
                "Different existing template " + definition.name +
                ". Choose a new actor name, or reconcile a conflicting parent in the template editor.");
        }
    }
    std::size_t provisionBytes{};
    static const std::map<std::string, std::string> folders{
        {"Model", "3do"},       {"Material", "mat"},     {"Animation", "3do/key"},    {"Puppet", "misc/pup"},
        {"Script", "cog"},      {"Sound", "sound"},      {"Sound class", "misc/snd"}, {"AI", "misc/ai"},
        {"Sprite", "misc/spr"}, {"Palette", "misc/cmp"}, {"Particles", "misc/par"}};
    for ( const auto &dependency : scope )
    {
        const auto &row = index.rows.at(dependency);
        if ( row.missing )
        {
            throw std::runtime_error("Missing family dependency: " + row.name);
        }
        if ( row.authoringTemplate )
        {
            continue;
        }
        if ( !row.winner )
        {
            throw std::runtime_error("Unresolved family dependency: " + row.name);
        }
        const auto &source = *row.winner;
        const auto bytes = index.sources.mounts[source.mount].archive.read(source.entry.name);
        bool exceeded = false;
        const auto actual = live.tryReadBinary(row.name, bytes.size(), exceeded);
        if ( exceeded )
        {
            throw std::runtime_error("The active resource differs in length: " + row.name);
        }
        if ( actual && *actual == bytes )
        {
            continue;
        }
        if ( row.stock )
        {
            throw std::runtime_error("Configure the reviewed stock resource in the active level: " + row.name);
        }
        if ( actual )
        {
            throw std::runtime_error(
                "Different active resource " + row.name +
                ". Remap or rename the conflicting asset and its references, then inspect again.");
        }
        const auto folder = folders.find(row.kind);
        if ( folder == folders.end() )
        {
            throw std::runtime_error("Unsupported family asset: " + row.name);
        }
        const auto destination =
            (std::filesystem::path(folder->second) / std::filesystem::path(keyFor(source.entry.name)).filename())
                .generic_string();
        if ( bytes.size() > (16u << 20) || result.provisions.size() >= 32 ||
             bytes.size() > (32u << 20) - provisionBytes )
        {
            throw std::runtime_error(
                "Family provisioning exceeds the gameplay journal’s size or file count limit.");
        }
        const auto candidates = live.candidates(row.name);
        if ( std::find(candidates.begin(), candidates.end(), destination) == candidates.end() )
        {
            throw std::runtime_error("Resource has no supported native destination: " + row.name);
        }
        auto data =
            bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        const auto [at, inserted] =
            result.provisions.try_emplace(std::filesystem::path(destination), std::move(data));
        if ( inserted )
        {
            provisionBytes += bytes.size();
        }
        else if ( at->second != data )
        {
            throw std::runtime_error("Ambiguous family destination: " + destination);
        }
    }
    return result;
}

bool bindFamily(Level &level, const FamilyInsertion &insertion, const std::int32_t sector, const TVector &position)
{
    if ( sector < 0 || static_cast<std::size_t>(sector) >= level.sectors.size() || !level.canAllocateIds(1) ||
         level.sectors[sector].vertices.empty() ||
         !sed::level::sectorContainsPoint(level.sectors[sector], position.x, position.y, position.z) )
    {
        return false;
    }
    level.templates.insert(level.templates.end(), insertion.templates.begin(), insertion.templates.end());
    Thing &thing = level.newThing();
    thing.templateName = insertion.name;
    thing.name = insertion.name;
    thing.sector = sector;
    thing.x = position.x;
    thing.y = position.y;
    thing.z = position.z;
    return true;
}
std::string inspectFamilyModel(const ResourceFamilyIndex &index, const std::string &key)
{
    const auto &row = index.rows.at(key);
    if ( !row.winner || row.kind != "Model" )
    {
        throw std::runtime_error("Select a resolved 3DO model.");
    }
    const auto &source = *row.winner;
    if ( source.entry.size > (16u << 20) )
    {
        throw std::runtime_error("Model exceeds the inspection size budget.");
    }
    const auto bytes = index.sources.mounts[source.mount].archive.read(source.entry.name);
    auto model = assets::loadFrom3DO(bytes, {.lod = 0, .applyHierarchyTransforms = false});
    const auto count = model.geosetCount;
    if ( count == 0 || count > 32 || source.entry.size > (64u << 20) / count )
    {
        throw std::runtime_error("Cannot inspect all declared model LODs within the inspection budget.");
    }
    std::ostringstream report;
    report << row.location << "\nModel radius " << model.radius << "\n";
    const bool jkProfile = index.sources.target.game == "jk";
    if ( jkProfile )
    {
        report << "JK.EXE loader reference: 512 vertices, 768 texture vertices and 512 faces per mesh; 24 corners "
                  "per face.\n";
    }
    report << "Materials:";
    for ( const auto &material : model.materials )
    {
        report << " " << material;
    }
    for ( const auto &node : model.nodes )
    {
        report << "\nNode " << node.id << " " << node.name << " parent " << node.parent << " mesh " << node.mesh
               << " pivot (" << node.pivot.x << ", " << node.pivot.y << ", " << node.pivot.z << ")";
    }
    for ( std::size_t lod = 0; lod < count; ++lod )
    {
        if ( lod )
        {
            model = assets::loadFrom3DO(bytes,
                                        {.lod = static_cast<std::int32_t>(lod), .applyHierarchyTransforms = false});
        }
        std::size_t faces{};
        for ( const auto &mesh : model.meshes )
        {
            faces += mesh.faces.size();
        }
        report << "\nLOD " << lod << ": " << model.meshes.size() << " meshes, " << faces << " total faces";
        if ( jkProfile )
        {
            for ( const auto &issue : assets::model3doEngineLimitIssues(model) )
            {
                report << "\n  JK loader limit: mesh " << issue.mesh << ", " << issue.what << " " << issue.count
                       << " > " << issue.limit;
            }
        }
        if ( jkProfile && faces >= 1199 )
        {
            report << "\n  Draw/performance warning: a 1,199-face specimen failed to appear in retail JK 1.01. "
                      "Test this model in the chosen engine.";
        }
    }
    report << "\nTarget rendering requires an engine observation.";
    return report.str();
}

std::vector<std::string> searchAssets(const ResourceFamilyIndex &index, std::string_view query)
{
    const auto needle = sed::text::lowerAscii(sed::text::trimView(query));
    std::vector<std::string> result;
    for ( const auto &[key, row] : index.rows )
    {
        if ( needle.empty() || sed::text::lowerAscii(row.name).find(needle) != std::string::npos ||
             sed::text::lowerAscii(row.kind).find(needle) != std::string::npos )
        {
            result.push_back(key);
        }
    }
    return result;
}
} // namespace sed::project
