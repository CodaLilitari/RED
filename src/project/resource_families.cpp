#include "resource_families.hpp"

#include "level/dependency_references.hpp"
#include "sed/assets/tpl.hpp"
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

std::string kindFor(const std::string& name)
{
    static const std::map<std::string, std::string> types{{".3do", "Model"}, {".mat", "Material"},
        {".key", "Animation"}, {".pup", "Puppet"}, {".cog", "Script"}, {".wav", "Sound"},
        {".snd", "Sound class"}, {".tpl", "Template library"}, {".ai", "AI"}, {".spr", "Sprite"},
        {".cmp", "Palette"}, {".par", "Particles"}};
    const auto found = types.find(std::filesystem::path(name).extension().string());
    return found == types.end() ? std::string{} : found->second;
}

std::string capturedText(const ResourceFamilyIndex& index, const ResourceLocation& winner, std::size_t& budget)
{
    if (winner.entry.size > budget) throw std::runtime_error("Resource-family inspection reached its byte budget.");
    budget -= winner.entry.size;
    const auto bytes = index.sources.mounts[winner.mount].archive.read(winner.entry.name);
    return bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string templateSection(std::string_view level)
{
    std::istringstream lines{std::string(level)};
    std::string rows, line;
    bool inside = false;
    while (std::getline(lines, line))
    {
        const auto text = sed::text::lowerAscii(sed::text::trimView(line));
        if (text.starts_with("section:"))
        {
            inside = sed::text::trimView(std::string_view(text).substr(8)) == "templates";
        }
        else if (inside && text == "end") inside = false;
        else if (inside && !text.starts_with("world templates")) { rows += line; rows += '\n'; }
    }
    return rows;
}

std::set<std::string> connected(const ResourceFamilyIndex& index, const std::string& key, bool inbound)
{
    std::set<std::string> seen;
    std::vector<std::string> pending{key};
    while (!pending.empty())
    {
        std::string current = std::move(pending.back());
        pending.pop_back();
        const auto row = index.rows.find(current);
        if (row == index.rows.end() || !seen.insert(current).second) continue;
        if (inbound) pending.insert(pending.end(), row->second.users.begin(), row->second.users.end());
        else for (const auto& [child, reason] : row->second.dependencies) pending.push_back(child);
    }
    return seen;
}
} // namespace

ResourceFamilyIndex indexProjectAssets(AssetSources sources)
{
    ResourceFamilyIndex index;
    index.sources = std::move(sources);
    index.previewResources.setArchivesOnly(true);
    for (auto i = index.sources.mounts.rbegin(); i != index.sources.mounts.rend(); ++i)
        index.previewResources.mountGob(i->archive);

    std::size_t entryCount{}, edgeCount{}, scanBudget = maxScanBytes, work = maxWork;
    std::vector<std::string> named;
    const auto remember = [&](const std::string& authored) -> std::string
    {
        const auto key = keyFor(authored);
        if (index.rows.contains(key)) return key;
        if (index.rows.size() >= maxEntries) throw std::runtime_error("Too many distinct project resource names.");
        AssetRow row;
        row.name = authored;
        row.kind = kindFor(key);
        if (row.kind.empty()) row.kind = "Resource";
        const auto matches = projectResourceLocations(index.sources.mounts, authored);
        row.missing = matches.empty();
        for (const auto& match : matches)
        {
            const auto& mount = index.sources.mounts[match.mount];
            const auto location = mount.label + " / " + match.entry.name;
            if (!row.winner) { row.winner = match; row.location = location; row.stock = mount.stock; }
            else row.shadows.push_back(location);
        }
        index.rows.emplace(key, std::move(row));
        named.push_back(key);
        return key;
    };
    for (const auto& mount : index.sources.mounts)
        for (const auto& entry : mount.archive.entries())
        {
            if (++entryCount > maxEntries) throw std::runtime_error("Too many mounted archive entries.");
            const auto path = keyFor(entry.name);
            if (kindFor(path).empty()) continue;
            const std::string base(scan::referenceBaseName(path));
            const auto matches = projectResourceLocations(index.sources.mounts, base);
            const bool known = std::ranges::any_of(matches, [&](const auto& match)
                { return keyFor(match.entry.name) == path; });
            remember(known ? base : path);
        }

    const auto edge = [&](const std::string& parent, const std::string& child, std::string reason)
    {
        if (++edgeCount > maxEdges) throw std::runtime_error("Too many project resource associations.");
        index.rows.at(parent).dependencies.emplace(child, std::move(reason));
        index.rows.at(child).users.insert(parent);
    };
    const auto callbacks = [&](const std::string& parent)
    {
        scan::ReferenceCallbacks c;
        c.work = [&] { if (work == 0) throw std::runtime_error("Resource inspection reached its traversal budget."); --work; return true; };
        c.reference = [&, parent](scan::ReferenceView ref)
        {
            edge(parent, remember(std::string(ref.name)), std::string(ref.relation));
            return true;
        };
        return c;
    };
    const auto templates = [&](const std::string& owner, const std::string& bytes)
    {
        std::istringstream input(bytes);
        const auto database = assets::loadTemplates(input);
        const auto templateKey = [&](std::string_view name) { return "template:" + owner + "#" + keyFor(name); };
        for (std::size_t i = 0; i < database.size(); ++i)
        {
            const auto& tpl = *database.get(i);
            const auto key = templateKey(tpl.name);
            if (!index.rows.contains(key) && index.rows.size() >= maxEntries) throw std::runtime_error("Too many distinct project resource names.");
            auto& row = index.rows[key];
            row.name = tpl.name; row.kind = "Template"; row.location = index.rows.at(owner).location;
            row.authoringTemplate = tpl; row.missing = false;
            edge(owner, key, "declares template");
            if (!tpl.parent.empty() && !sed::text::iequalsAscii(tpl.parent, "none"))
            {
                const auto parentKey = templateKey(tpl.parent);
                if (!index.rows.contains(parentKey) && index.rows.size() >= maxEntries) throw std::runtime_error("Too many distinct project resource names.");
                auto& parent = index.rows[parentKey];
                if (parent.kind.empty()) { parent.name = tpl.parent; parent.kind = "Template"; parent.missing = true; }
                edge(key, parentKey, "inherits template");
            }
            assets::Templates single;
            single.addOrUpdate(tpl);
            std::ostringstream text;
            assets::saveTemplates(text, single);
            scan::visitRootReferences("SECTION: TEMPLATES\nWORLD TEMPLATES 1\n" + text.str() + "END\n", {}, callbacks(key));
        }
    };
    for (const auto& [name, bytes] : index.sources.levels)
    {
        const auto key = "level:" + name;
        if (!index.rows.contains(key) && index.rows.size() >= maxEntries) throw std::runtime_error("Too many distinct project resource names.");
            auto& row = index.rows[key];
        row.name = name; row.kind = "Level"; row.location = name;
        scan::visitRootReferences(bytes, {}, callbacks(key));
        templates(key, templateSection(bytes));
    }
    for (std::size_t i = 0; i < named.size(); ++i)
    {
        const auto key = named[i];
        const auto& row = index.rows.at(key);
        if (!row.winner) continue;
        if (row.kind == "Template library") templates(key, capturedText(index, *row.winner, scanBudget));
        else if (row.kind == "Model" || row.kind == "Puppet" || row.kind == "Sound class" || row.kind == "Script" || row.kind == "Sprite")
            scan::visitChildReferences(row.name, capturedText(index, *row.winner, scanBudget), callbacks(key));
    }
    return index;
}

std::set<std::string> resourceFamily(const ResourceFamilyIndex& index, const std::string& key) { return connected(index, key, false); }
std::set<std::string> affectedResources(const ResourceFamilyIndex& index, const std::string& key) { return connected(index, key, true); }

std::vector<std::string> searchAssets(const ResourceFamilyIndex& index, std::string_view query)
{
    const auto needle = sed::text::lowerAscii(sed::text::trimView(query));
    std::vector<std::string> result;
    for (const auto& [key, row] : index.rows)
        if (needle.empty() || sed::text::lowerAscii(row.name).find(needle) != std::string::npos
            || sed::text::lowerAscii(row.kind).find(needle) != std::string::npos) result.push_back(key);
    return result;
}
} // namespace sed::project
