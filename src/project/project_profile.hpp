#pragma once

#include "assets/atomic_file_write.hpp"
#include "sed/assets/gob.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sed::project
{

struct Target
{
    std::string game;
    std::string engine;
    std::string renderer;
};

struct ResourceMount
{
    std::string label;
    bool stock{};
    assets::GobArchive archive;
};

struct AssetSources
{
    Target target;
    std::vector<ResourceMount> mounts;
    std::vector<std::pair<std::string, std::string>> levels;
    std::string revision;
};

struct ResourceLocation
{
    std::size_t mount;
    assets::GobEntry entry;
};

std::vector<ResourceLocation> projectResourceLocations(const std::vector<ResourceMount>& mounts, std::string_view name);

/** Capture the same checked mounts, target and exported levels that supply the project package. */
AssetSources captureAssetSources(const std::filesystem::path& profile, const std::filesystem::path& stockRoot);

struct Resource
{
    std::string name;
    std::string winner;
    std::vector<std::string> shadowed;
    bool suppliedByGame{};
};

struct Scenario
{
    std::string name;
    std::string level;
    std::string definition;
    std::vector<std::string> receipts;
};

struct Prepared
{
    Target target;
    std::vector<std::string> levels;
    std::vector<Resource> resources;
    std::vector<Scenario> scenarios;
    std::vector<std::string> warnings;
    std::string revision;
    std::string package;
};

Prepared prepare(const std::filesystem::path& profile, const std::filesystem::path& stockRoot);
std::unique_ptr<assets::detail::StagedAssetFile> stage(const Prepared& current,
    const std::string& reviewedRevision, const std::filesystem::path& destination);
void publish(const std::filesystem::path& profile, const std::filesystem::path& stockRoot,
    const std::string& reviewedRevision, const std::filesystem::path& destination);

} // namespace sed::project
