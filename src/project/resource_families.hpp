#pragma once

#include "project_profile.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/assets/tpl.hpp"
#include "sed/level.hpp"

#include <map>
#include <set>

namespace sed::project
{
struct AssetRow
{
    std::string name;
    std::string kind;
    std::string location;
    std::vector<std::string> shadows;
    std::map<std::string, std::string> dependencies;
    std::set<std::string> users;
    std::optional<ResourceLocation> winner;
    std::optional<ThingTemplate> authoringTemplate;
    bool missing{};
    bool stock{};
};

struct ResourceFamilyIndex
{
    AssetSources sources;
    assets::ResourceLocator previewResources;
    std::map<std::string, AssetRow> rows;
};

struct FamilyInsertion
{
    std::string name;
    std::vector<ThingTemplate> templates;
    std::map<std::filesystem::path, std::string> provisions;
};

FamilyInsertion prepareFamily(const ResourceFamilyIndex &index, const std::string &key, const Level &level,
                              const assets::ResourceLocator &live, std::string name,
                              const assets::Templates *global = nullptr);
bool bindFamily(Level &level, const FamilyInsertion &insertion, std::int32_t sector, const TVector &position);
std::string inspectFamilyModel(const ResourceFamilyIndex &index, const std::string &key);
ResourceFamilyIndex indexProjectAssets(AssetSources sources);
/** The selected resource and its transitive dependencies, with cycle-safe traversal. */
std::set<std::string> resourceFamily(const ResourceFamilyIndex &index, const std::string &key);
std::set<std::string> affectedResources(const ResourceFamilyIndex &index, const std::string &key);
std::vector<std::string> searchAssets(const ResourceFamilyIndex &index, std::string_view query);
} // namespace sed::project
