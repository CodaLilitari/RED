#pragma once

#include "sed/level.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sed::level
{

enum class GameTestEngine { Auto, OpenJKDF2, Retail };

struct GameLaunchRequest
{
    ProjectType game{ ProjectType::JKDF2 };
    GameTestEngine engine{ GameTestEngine::OpenJKDF2 };
    std::filesystem::path gameDirectory;
    std::filesystem::path projectDirectory;
    std::filesystem::path executable;
    std::string levelFileName;
    /** Optional platform-provided short-path alternative; never shell quoted. */
    std::string shortProjectPath;
};

struct GameLaunchPlan
{
    GameTestEngine engine{};
    std::filesystem::path executable;
    std::filesystem::path workingDirectory;
    std::string projectPathToken;
    std::vector<std::string> arguments;
    std::vector<std::filesystem::path> requiredFiles;
    bool directMapStart{};
    bool gameDirectoryExists{};
    bool executableExists{};
    bool levelExists{};
    bool episodeExists{};
    bool installMarkerExists{};
    bool ready{};
};

/** JK episode sequence filenames have a 32-byte field, including terminator. */
void validateGameTestLevelName(std::string_view name);

/** Read-only planning. Never creates files, launches processes or changes CWD.
 * OpenJKDF2 arguments target the verified v0.9.9-compatible command protocol.
 * Retail launches the test override for selection through its ordinary menu.
 * Throws invalid_argument for unsupported game/profile/name/path syntax.
 */
GameLaunchPlan prepareGameLaunch(const GameLaunchRequest& request);

} // namespace sed::level
