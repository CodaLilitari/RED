#include "../agent_workspace.hpp"
#include "sed/level/game_launch.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
int failures = 0;
int requestNumber = 0;

void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

sed_agent::Json request(const fs::path& root, const char* game, const char* engine)
{
    auto value = sed_agent::Json::makeObject();
    value.set("schemaVersion", sed_agent::Json::makeNumber(1));
    value.set("game", sed_agent::Json::makeString(game));
    value.set("engine", sed_agent::Json::makeString(engine));
    value.set("gameDirectory", sed_agent::Json::makeString((root / "game").string()));
    value.set("projectDirectory", sed_agent::Json::makeString((root / "stage").string()));
    value.set("levelFileName", sed_agent::Json::makeString("map.jkl"));
    value.set("executable", sed_agent::Json::makeString("engine.exe"));
    return value;
}

sed_agent::Json plan(const fs::path& root, const sed_agent::Json& request)
{
    const auto path = root / ("request-" + std::to_string(++requestNumber) + ".json");
    std::ofstream(path, std::ios::binary) << sed_agent::toJsonString(request);
    return sed_agent::runCommand("launch-plan", { path.string() });
}

bool equalsStrings(const sed_agent::Json* value, const std::vector<std::string>& expected)
{
    if ( !value || value->type != sed_agent::Json::Type::Array || value->array.size() != expected.size() )
    {
        return false;
    }
    for ( std::size_t i = 0; i < expected.size(); ++i )
    {
        if ( value->array[i].type != sed_agent::Json::Type::String || value->array[i].str != expected[i] )
        {
            return false;
        }
    }
    return true;
}

void runTests(const fs::path& root)
{
    fs::create_directories(root / "game" / "resource");
    fs::create_directories(root / "stage" / "jkl");
    std::ofstream(root / "game" / "engine.exe") << "synthetic executable; never executed";
    std::ofstream(root / "game" / "resource" / "jk_.cd") << "synthetic marker";
    std::ofstream(root / "stage" / "jkl" / "map.jkl") << "synthetic JKL; planning never parses it";
    std::ofstream(root / "stage" / "episode.jk") << "synthetic episode; planning never parses it";
    const std::string pathToken = (fs::path("..") / "stage").string();
    for ( const char* game : { "JediKnight", "MysteriesOfTheSith" } )
    {
        for ( const char* engine : { "Retail", "OpenJKDF2" } )
        {
#ifndef _WIN32
            if ( std::string(engine) == "Retail" )
            {
                bool refused = false;
                try { (void)plan(root, request(root, game, engine)); }
                catch ( const sed_agent::AgentError& error ) { refused = error.code == sed_agent::kExitUsage; }
                check(refused, "native Unix testing refuses retail executables explicitly");
                continue;
            }
#endif
            const auto result = plan(root, request(root, game, engine));
            std::vector<std::string> arguments{ "-devmode", "-debug", "log" };
            if ( std::string(engine) == "OpenJKDF2"
                && std::string(game) == "MysteriesOfTheSith" )
            {
                arguments.push_back("-motsCompat");
            }
            arguments.insert(arguments.end(), { "-path", pathToken });
            if ( std::string(engine) == "OpenJKDF2" )
            {
                arguments.insert(arguments.end(), {
                    "-autostart", "-singleplayer", "-episode", "redtest", "-map", "map.jkl" });
            }
            check(equalsStrings(result.find("arguments"), arguments),
                "each game/backend receives its complete supported argument vector");
            const auto* direct = result.find("directMapStart");
            check(direct && direct->type == sed_agent::Json::Type::Bool
                    && direct->boolean == (std::string(engine) == "OpenJKDF2"),
                "plan states whether it starts the map directly or requires the retail menu");
            const auto* ready = result.find("ready");
            check(ready && ready->type == sed_agent::Json::Type::Bool && ready->boolean,
                "complete synthetic loose test layout is ready");
            const auto* launches = result.find("launches");
            check(launches && launches->type == sed_agent::Json::Type::Bool && !launches->boolean,
                "planning never starts a process");
            const auto* working = result.find("workingDirectory");
            check(working && working->str == (root / "game").string(),
                "game installation remains process working directory");
        }
    }

    for ( const char* name : { "../other.jkl", "map with spaces.jkl", "map.ndy",
                               "abcdefghijklmnopqrstuvwxyz12.jkl" } )
    {
        auto input = request(root, "JediKnight", "OpenJKDF2");
        input.set("levelFileName", sed_agent::Json::makeString(name));
        bool refused = false;
        try { (void)plan(root, input); }
        catch ( const sed_agent::AgentError& error ) { refused = error.code == sed_agent::kExitUsage; }
        check(refused, "invalid, traversing, or overlong engine map filename is rejected");
    }
    auto unknown = request(root, "JediKnight", "Auto");
    bool refused = false;
    try { (void)plan(root, unknown); }
    catch ( const sed_agent::AgentError& error ) { refused = error.code == sed_agent::kExitUsage; }
    check(refused, "Auto refuses an unrecognized executable instead of guessing its protocol");

    fs::remove(root / "stage" / "jkl" / "map.jkl");
    const auto missing = plan(root, request(root, "JediKnight", "OpenJKDF2"));
    const auto* ready = missing.find("ready");
    check(ready && ready->type == sed_agent::Json::Type::Bool && !ready->boolean,
        "missing staged map is a reviewable not-ready plan");
    check(!fs::exists(root / "game" / "episode"),
        "planning does not install anything into the game episode directory");

    fs::create_directories(root / "stage with spaces");
    fs::create_directories(root / "unrelated");
    sed::level::GameLaunchRequest alternate;
    alternate.gameDirectory = root / "game";
    alternate.projectDirectory = root / "stage with spaces";
    alternate.executable = "engine.exe";
    alternate.levelFileName = "map.jkl";
    alternate.shortProjectPath = (root / "unrelated").string();
    refused = false;
    try { (void)sed::level::prepareGameLaunch(alternate); }
    catch ( const std::invalid_argument& ) { refused = true; }
    check(refused, "alternate resource token must name the same directory as the checked stage");
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path()
        / ("red-launch-plan-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        fs::create_directories(root);
        runTests(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        ++failures;
        std::cerr << error.message << '\n';
    }
    catch ( const std::exception& error )
    {
        ++failures;
        std::cerr << error.what() << '\n';
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << "launch plan failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
