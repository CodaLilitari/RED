#pragma once
#include "sed/assets/episode.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

inline void require(bool ok, const char* message)
{
    if ( !ok ) throw std::runtime_error(message);
}

inline void write(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if ( !stream ) throw std::runtime_error("Fixture write failed");
}

inline std::string createCampaignFixture(const fs::path& workspace, const fs::path& stock)
{
    using namespace sed;
    const auto custom = assets::buildGobBytes({{"mat/wall.mat", "custom"}});
    const auto retail = assets::buildGobBytes({{"mat/wall.mat", "stock"}});
    write(workspace / "art.gob", custom);
    write(stock / "stock.gob", retail);
    assets::EpisodeFile episode{"Two rooms", 1, {
        {10, 0, 1, "LEVEL", "one.jkl", 0, 0, 20, -1},
        {20, 0, 2, "LEVEL", "two.jkl", 0, 0, -1, -1}}};
    write(workspace / "episode.jk", assets::saveToEpisodeJkText(episode));
    const std::string level = "SECTION: MATERIALS\nWorld Materials 1\nwall.mat\nEND\n";
    write(workspace / "jkl/one.jkl", level);
    write(workspace / "jkl/two.jkl", level);
    const auto manifest = std::string(R"({"schemaVersion":1,"episode":"episode.jk","levels":[{"export":"jkl/one.jkl"},{"export":"jkl/two.jkl"}],"target":{"game":"jk","engine":"OpenJKDF2","renderer":"OpenGL"},"mounts":[{"kind":"project","path":"art.gob","sha256":")")
        + assets::jklSourceSha256Hex(custom) + R"("},{"kind":"stock","path":"stock.gob","sha256":")"
        + assets::jklSourceSha256Hex(retail) + R"("}],"scenarios":[]})";
    write(workspace / "red-project.json", manifest);

    return custom;
}
