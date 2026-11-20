#pragma once

#include "project_fixture.hpp"

inline std::string familyFixtureBytes(const std::string &leaf)
{
    const auto file =
        fs::path(__FILE__).parent_path().parent_path().parent_path() / "tests/fixtures/relay-sentry" / leaf;
    std::ifstream input(file, std::ios::binary);
    if ( !input )
    {
        throw std::runtime_error("Missing original resource family fixture: " + leaf);
    }
    return {std::istreambuf_iterator<char>(input), {}};
}

inline void createRelayProject(const fs::path &workspace, const fs::path &stock, bool includeSound)
{
    using namespace sed;
    std::vector<std::pair<std::string, std::string>> files;
    for ( const char *path :
          {"3do/relay_sentry.3do", "3do/key/relay_point.key", "misc/pup/relay_motion.pup", "cog/relay_logic.cog",
           "misc/snd/relay_audio.snd", "mat/relay_skin.mat", "misc/relay_sentry.tpl"} )
    {
        files.emplace_back(path, familyFixtureBytes(fs::path(path).filename().string()));
    }
    if ( includeSound )
    {
        files.emplace_back("sound/relay_ping.wav", familyFixtureBytes("relay_ping.wav"));
    }
    const auto archive = assets::buildGobBytes(files);
    const auto stockArchive = assets::buildGobBytes({{"mat/relay_skin.mat", "shadowed stock fixture"}});
    write(workspace / "relay.gob", archive);
    write(stock / "stock.gob", stockArchive);
    assets::EpisodeFile episode{"Relay", 1, {{10, 0, 1, "LEVEL", "relay.jkl", 0, 0, -1, -1}}};
    write(workspace / "episode.jk", assets::saveToEpisodeJkText(episode));
    write(workspace / "jkl/relay.jkl",
          "SECTION: TEMPLATES\nWORLD TEMPLATES 1\n" + familyFixtureBytes("relay_sentry.tpl") + "\nEND\n");
    write(
        workspace / "red-project.json",
        std::string(
            R"({"schemaVersion":1,"episode":"episode.jk","levels":[{"export":"jkl/relay.jkl"}],"target":{"game":"jk","engine":"OpenJKDF2","renderer":"OpenGL"},"mounts":[{"kind":"project","path":"relay.gob","sha256":")") +
            assets::jklSourceSha256Hex(archive) + R"("},{"kind":"stock","path":"stock.gob","sha256":")" +
            assets::jklSourceSha256Hex(stockArchive) + R"("}],"scenarios":[]})");
}
