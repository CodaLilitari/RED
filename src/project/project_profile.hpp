#pragma once

#include "assets/atomic_file_write.hpp"
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

struct Resource
{
    std::string name;
    std::string winner;
    std::vector<std::string> shadowed;
    bool suppliedByGame{};
    std::string sha256;
};

struct Scenario
{
    std::string name;
    std::string level;
    std::string definition;
    std::vector<std::string> receipts;
    std::string definitionBytes;
};

struct LiveExport
{
    std::filesystem::path source;
    std::string bytes;
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
    std::string liveLevel;
};

Prepared prepare(const std::filesystem::path& profile, const std::filesystem::path& stockRoot,
    const LiveExport* live = nullptr);
std::unique_ptr<assets::detail::StagedAssetFile> stage(const Prepared& current,
    const std::string& reviewedRevision, const std::filesystem::path& destination);
void publish(const std::filesystem::path& profile, const std::filesystem::path& stockRoot,
    const std::string& reviewedRevision, const std::filesystem::path& destination);

} // namespace sed::project
