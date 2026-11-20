#include "project_profile.hpp"

#include "agent_json.hpp"
#include "assets/atomic_file_write.hpp"
#include "sed/assets/episode.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/level/package.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>

namespace sed::project
{
namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
constexpr std::size_t kProjectBudget = 512u << 20;
constexpr std::size_t kPackageBudget = 128u << 20;

std::string bytesAt(const fs::path& file, std::size_t limit)
{
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if ( !in ) throw std::runtime_error("Cannot open " + file.filename().string());
    const auto size = in.tellg();
    if ( size < 0 || static_cast<std::uintmax_t>(size) > limit )
        throw std::runtime_error("File exceeds the project read budget: " + file.filename().string());
    std::string data(static_cast<std::size_t>(size), '\0');
    in.seekg(0);
    if ( !in.read(data.data(), size) || in.peek() != std::char_traits<char>::eof() )
        throw std::runtime_error("File changed during capture: " + file.filename().string());
    return data;
}

fs::path utf8Path(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

fs::path relativeFile(const fs::path& root, const std::string& name)
{
    const fs::path rel = utf8Path(name);
    if ( root.empty() || name.empty() || rel.has_root_path() || name.find_first_of(":\\") != std::string::npos
        || name.find('\0') != std::string::npos )
        throw std::runtime_error("Expected a portable relative file name: " + name);
    for ( const auto& part : rel )
        if ( part.empty() || part == "." || part == ".." ) throw std::runtime_error("Invalid project path: " + name);
    const auto path = fs::canonical(root / rel);
    const auto inside = path.lexically_relative(fs::canonical(root));
    if ( inside.empty() || *inside.begin() == ".." || !fs::is_regular_file(path) )
        throw std::runtime_error("Project file lies outside its root: " + name);
    return path;
}

const Json& field(const Json& json, const char* key, Json::Type type)
{
    const auto* item = json.find(key);
    if ( !item || item->type != type ) throw std::runtime_error(std::string("Missing or invalid project field: ") + key);
    return *item;
}

const std::string& stringField(const Json& json, const char* key)
{
    const auto& text = field(json, key, Json::Type::String).str;
    if ( text.empty() ) throw std::runtime_error(std::string("Empty project field: ") + key);
    return text;
}

std::string canonicalEntry(std::string_view name)
{
    auto key = assets::canonicalRuntimeGobEntryName(name);
    std::replace(key.begin(), key.end(), '\\', '/');
    return key;
}

std::vector<std::byte> byteVector(std::string_view data)
{
    const auto bytes = std::as_bytes(std::span(data));
    return {bytes.begin(), bytes.end()};
}

std::string byteString(const std::vector<std::byte>& data)
{
    return data.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(data.data()), data.size());
}



struct Inputs
{
    fs::path root;
    std::string observations;
    std::size_t remaining{kProjectBudget};

    std::string read(const fs::path& base, const std::string& name, const std::string& label,
        std::size_t maximum = kPackageBudget)
    {
        auto data = bytesAt(relativeFile(base, name), std::min(remaining, maximum));
        remaining -= data.size();
        observations += std::to_string(label.size()) + ":" + label + assets::jklSourceSha256Hex(data);
        return data;
    }
};

Target projectTarget(const Json& record)
{
    if (field(record, "schemaVersion", Json::Type::Number).number != 1)
        throw std::runtime_error("Unsupported RED project profile version");
    const auto& target = field(record, "target", Json::Type::Object);
    Target result{stringField(target, "game"), stringField(target, "engine"), stringField(target, "renderer")};
    if (result.game != "jk" && result.game != "mots")
        throw std::runtime_error("Project resources require the jk or mots target");
    return result;
}

std::vector<ResourceMount> captureMounts(const Json& record, Inputs& inputs, const fs::path& stockRoot)
{
    std::vector<ResourceMount> mounts;
    std::set<std::string> labels;
    for ( const auto& mount : field(record, "mounts", Json::Type::Array).array )
    {
        const auto& kind = stringField(mount, "kind");
        if ( kind != "stock" && kind != "project" ) throw std::runtime_error("Unknown mount kind: " + kind);
        const auto& path = stringField(mount, "path");
        const std::string label = kind + ":" + path;
        if ( !labels.insert(label).second ) throw std::runtime_error("Duplicate mount: " + label);
        const auto data = inputs.read(kind == "stock" ? stockRoot : inputs.root, path, label, kProjectBudget);
        if ( assets::jklSourceSha256Hex(data) != stringField(mount, "sha256") )
            throw std::runtime_error("Archive differs from project identity: " + label);
        mounts.push_back({label, kind == "stock", assets::GobArchive::fromBytes(byteVector(data), utf8Path(path))});
    }
    return mounts;
}

class CampaignPackage
{
public:
    void add(const std::string& name, std::string bytes)
    {
        const auto key = canonicalEntry(name);
        if ( const auto it = files_.find(key); it != files_.end() )
        {
            if ( it->second != bytes ) throw std::runtime_error("Conflicting campaign destination: " + key);
            return;
        }
        if ( bytes.size() > kPackageBudget - total_ || files_.size() >= 65535 )
            throw std::runtime_error("Campaign exceeds package budget");
        total_ += bytes.size();
        files_.emplace(key, std::move(bytes));
    }

    std::string finish() const
    {
        std::vector<std::pair<std::string, std::string>> ordered(files_.begin(), files_.end());
        return assets::buildGobBytes(ordered);
    }

private:
    std::map<std::string, std::string> files_;
    std::size_t total_{};
};
} // namespace

std::vector<ResourceLocation> projectResourceLocations(const std::vector<ResourceMount>& mounts, std::string_view name)
{
    static const assets::ResourceLocator aliases;
    std::vector<ResourceLocation> matches;
    for (const auto& candidate : aliases.candidates(name))
        for (std::size_t i = 0; i < mounts.size(); ++i)
            if (auto entry = mounts[i].archive.find(candidate))
                if (std::ranges::none_of(matches, [&](const auto& old) { return old.mount == i && old.entry.name == entry->name; }))
                    matches.push_back({i, std::move(*entry)});
    return matches;
}

AssetSources captureAssetSources(const fs::path& profile, const fs::path& stockRoot)
{
    const auto text = bytesAt(profile, 1u << 20);
    const auto record = sed_agent::parseJson(text);
    AssetSources capture;
    capture.target = projectTarget(record);
    Inputs inputs{fs::absolute(profile).parent_path(), assets::jklSourceSha256Hex(text)};
    capture.mounts = captureMounts(record, inputs, stockRoot);
    for (const auto& item : field(record, "levels", Json::Type::Array).array)
    {
        const auto& path = stringField(item, "export");
        capture.authoringFiles.push_back(relativeFile(inputs.root, path));
        if (item.find("native")) capture.authoringFiles.push_back(relativeFile(inputs.root, stringField(item, "native")));
        capture.levels.emplace_back(path, inputs.read(inputs.root, path, "level:" + path));
    }
    capture.revision = assets::jklSourceSha256Hex(inputs.observations);
    return capture;
}

Prepared prepare(const fs::path& profile, const fs::path& stockRoot)
{
    const auto text = bytesAt(profile, 1u << 20);
    const Json record = sed_agent::parseJson(text);
    if ( field(record, "schemaVersion", Json::Type::Number).number != 1 )
        throw std::runtime_error("Unsupported RED project profile version");
    Prepared result{};
    result.target = projectTarget(record);
    Inputs inputs{fs::absolute(profile).parent_path(), assets::jklSourceSha256Hex(text)};
    const auto& episodeName = stringField(record, "episode");
    auto episodeBytes = inputs.read(inputs.root, episodeName, "episode:" + episodeName);
    std::istringstream input(episodeBytes);
    const auto episode = assets::loadFromEpisodeJk(input);
    if ( episodeName != "episode.jk" ) throw std::runtime_error("The campaign editor uses episode.jk beside the profile");
    if ( const auto bad = assets::episodeGraphValidationError(episode) ) throw std::runtime_error(*bad);
    CampaignPackage package;
    package.add("episode.jk", std::move(episodeBytes));

    const auto mounts = captureMounts(record, inputs, stockRoot);
    std::map<std::string, Resource> used;
    const auto resolve = [&](std::string_view name, std::size_t budget) -> std::optional<level::PackageResolvedResource>
    {
        Resource resource;
        resource.name = std::string(name);
        std::optional<level::PackageResolvedResource> selected;
        for (const auto& location : projectResourceLocations(mounts, name))
        {
            const auto& mount = mounts[location.mount];
            if (!selected)
            {
                if (location.entry.size > budget) throw std::runtime_error("Resource exceeds package budget: " + resource.name);
                resource.winner = mount.label;
                resource.suppliedByGame = mount.stock;
                selected.emplace(level::PackageResolvedResource{mount.label, byteString(mount.archive.read(location.entry.name)), mount.stock});
            }
            else if (mount.label != resource.winner && std::ranges::find(resource.shadowed, mount.label) == resource.shadowed.end())
                resource.shadowed.push_back(mount.label);
        }
        if ( selected ) used.insert_or_assign(resource.name, std::move(resource));
        return selected;
    };

    std::set<std::string> expected;
    for ( const auto& row : episode.sequences )
    {
        auto type = row.type;
        std::ranges::transform(type, type.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if ( type == "LEVEL" ) expected.insert(canonicalEntry(row.fileName));
    }
    std::set<std::string> levels;
    for ( const auto& item : field(record, "levels", Json::Type::Array).array )
    {
        const auto& path = stringField(item, "export");
        const auto name = canonicalEntry(utf8Path(path).filename().string());
        if ( !levels.insert(name).second ) throw std::runtime_error("Duplicate level destination: " + name);
        if ( item.find("native") )
        {
            const auto& native = stringField(item, "native");
            (void)relativeFile(inputs.root, native);
        }
        level::PackageRequest request;
        request.inputBytes = inputs.read(inputs.root, path, "level:" + name);
        request.levelFileName = name;
        request.episodeTitle = episode.name;
        request.episodeType = episode.type;
        request.archiveExtension = result.target.game == "jk" ? ".gob" : ".goo";
        request.resolveResource = resolve;
        const auto plan = level::planLevelPackage(request);
        for ( const auto& issue : plan.issues )
        {
            const auto detail = name + ": " + issue.code + " " + issue.reference + " " + issue.message;
            if ( issue.severity == "error" ) throw std::runtime_error(detail);
            result.warnings.push_back(detail);
        }
        if ( !plan.canBuild ) throw std::runtime_error("Could not prepare " + name);
        auto archive = assets::GobArchive::fromBytes(byteVector(plan.outputData));
        for ( const auto& entry : archive.entries() )
            if ( canonicalEntry(entry.name) != "episode.jk" ) package.add(entry.name, byteString(archive.read(entry.name)));
        result.levels.push_back(name);
    }
    if ( expected.empty() || levels != expected ) throw std::runtime_error("Project exports and campaign LEVEL rows disagree");
    if ( record.find("files") )
    {
        for ( const auto& item : field(record, "files", Json::Type::Array).array )
        {
            const auto& path = stringField(item, "path");
            const auto& destination = stringField(item, "destination");
            package.add(destination, inputs.read(inputs.root, path, "extra:" + destination));
        }
    }
    for ( const auto& item : field(record, "scenarios", Json::Type::Array).array )
    {
        Scenario scenario;
        scenario.name = stringField(item, "name");
        scenario.level = canonicalEntry(stringField(item, "level"));
        if ( !levels.contains(scenario.level) ) throw std::runtime_error("Unknown scenario level: " + scenario.level);
        scenario.definition = stringField(item, "definition");
        if ( !sed_agent::parseJson(inputs.read(inputs.root, scenario.definition, "scenario:" + scenario.name, 1u << 20)).isObject() )
            throw std::runtime_error("Expected a JSON scenario definition: " + scenario.name);
        for ( const auto& receipt : field(item, "receipts", Json::Type::Array).array )
        {
            if ( receipt.type != Json::Type::String ) throw std::runtime_error("Expected a receipt path");
            (void)relativeFile(inputs.root, receipt.str);
            scenario.receipts.push_back(receipt.str);
        }
        result.scenarios.push_back(std::move(scenario));
    }
    for ( auto& [key, value] : used ) result.resources.push_back(std::move(value));
    result.package = package.finish();
    result.revision = assets::jklSourceSha256Hex(inputs.observations + assets::jklSourceSha256Hex(result.package));
    return result;
}

std::unique_ptr<assets::detail::StagedAssetFile> stage(const Prepared& current, const std::string& reviewedRevision,
    const fs::path& destination)
{
    if ( current.revision != reviewedRevision ) throw std::runtime_error("Project inputs changed. Prepare the package again.");
    if ( destination.extension() != (current.target.game == "jk" ? ".gob" : ".goo") )
        throw std::runtime_error("Destination extension must match the reviewed game target");
    auto staged = std::make_unique<assets::detail::StagedAssetFile>(destination);
    staged->writeAndClose(std::as_bytes(std::span(current.package)));
    return staged;
}

void publish(const fs::path& profile, const fs::path& stockRoot, const std::string& reviewedRevision, const fs::path& destination)
{
    stage(prepare(profile, stockRoot), reviewedRevision, destination)->publishNew(destination);
}
} // namespace sed::project
