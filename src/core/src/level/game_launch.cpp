#include "sed/level/game_launch.hpp"

#include "sed/text.hpp"

#include <algorithm>
#include <stdexcept>

namespace sed::level
{
namespace
{
bool validPathToken(const std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](const unsigned char c)
    {
        return c >= '!' && c <= '~' && c != '"';
    });
}

bool regularFile(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}
} // namespace

void validateGameTestLevelName(const std::string_view name)
{
    if ( name.size() <= 4 || name.size() > 31
        || !text::iequalsAscii(name.substr(name.size() - 4), ".jkl")
        || !std::all_of(name.begin(), name.end() - 4, [](const unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }) )
    {
        throw std::invalid_argument(
            "Use up to 27 ASCII letters, digits, underscores or hyphens followed by .jkl for the test map.");
    }
}

GameLaunchPlan prepareGameLaunch(const GameLaunchRequest& request)
{
    namespace fs = std::filesystem;
    if ( request.game != ProjectType::JKDF2 && request.game != ProjectType::MOTS )
    {
        throw std::invalid_argument("Game testing supports Jedi Knight and Mysteries of the Sith only.");
    }
    if ( request.engine != GameTestEngine::Auto && request.engine != GameTestEngine::OpenJKDF2
        && request.engine != GameTestEngine::Retail )
    {
        throw std::invalid_argument("Unknown test engine profile.");
    }
    validateGameTestLevelName(request.levelFileName);
    if ( request.gameDirectory.empty() || request.projectDirectory.empty() || request.executable.empty() )
    {
        throw std::invalid_argument("Game folder, test folder and executable are required.");
    }
    GameLaunchPlan plan;
    plan.engine = request.engine;
    plan.workingDirectory = fs::absolute(request.gameDirectory).lexically_normal();
    const auto project = fs::absolute(request.projectDirectory).lexically_normal();
    plan.executable = (request.executable.is_absolute() ? request.executable
        : plan.workingDirectory / request.executable).lexically_normal();
    const std::string executableName = text::lowerAscii(plan.executable.filename().string());
    if ( plan.engine == GameTestEngine::Auto )
    {
        if ( executableName == "openjkdf2.exe" || executableName == "openjkdf2-64.exe"
            || executableName == "openjkdf2-32.exe" || executableName == "openjkdf2"
            || executableName == "openjkdf2-64" )
        {
            plan.engine = GameTestEngine::OpenJKDF2;
        }
        else if ( executableName == (request.game == ProjectType::MOTS ? "jkm.exe" : "jk.exe") )
        {
            plan.engine = GameTestEngine::Retail;
        }
        else
        {
            throw std::invalid_argument("Choose OpenJKDF2 or Retail explicitly for a renamed executable.");
        }
    }
#ifndef _WIN32
    if ( plan.engine == GameTestEngine::Retail )
    {
        throw std::invalid_argument("Direct retail testing requires Windows; select native OpenJKDF2 on this platform.");
    }
#endif
    const auto relative = project.lexically_relative(plan.workingDirectory);
    std::vector<std::string> candidates{ relative.string(), project.string() };
    if ( !request.shortProjectPath.empty() )
    {
        const fs::path alternate(request.shortProjectPath);
        std::error_code aliasError;
        if ( alternate.is_absolute() && fs::equivalent(alternate, project, aliasError) && !aliasError )
        {
            candidates.push_back(request.shortProjectPath);
        }
    }
    const std::size_t suffixLength = std::max(std::size_t{ 11 }, request.levelFileName.size() + 4);
    for ( const auto& candidate : candidates )
    {
        if ( validPathToken(candidate) && candidate.size() + 1 + suffixLength <= 127
            && (plan.projectPathToken.empty() || candidate.size() < plan.projectPathToken.size()) )
        {
            plan.projectPathToken = candidate;
        }
    }
    if ( plan.projectPathToken.empty() )
    {
        throw std::invalid_argument(
            "The engine needs a shorter ASCII test path without spaces or quotes; the path and resource filename must fit 127 bytes.");
    }
    plan.arguments = { "-devmode", "-debug", "log" };
    plan.directMapStart = plan.engine == GameTestEngine::OpenJKDF2;
    if ( plan.directMapStart && request.game == ProjectType::MOTS )
    {
        plan.arguments.push_back("-motsCompat");
    }
    plan.arguments.insert(plan.arguments.end(), { "-path", plan.projectPathToken });
    if ( plan.directMapStart )
    {
        plan.arguments.insert(plan.arguments.end(), {
            "-autostart", "-singleplayer", "-episode", "redtest", "-map", request.levelFileName });
    }
    const auto level = project / "jkl" / request.levelFileName;
    const auto episode = project / "episode.jk";
    const auto marker = plan.workingDirectory / "resource" / "jk_.cd";
    plan.requiredFiles = { plan.executable, level, episode };
    if ( plan.directMapStart )
    {
        plan.requiredFiles.push_back(marker);
    }
    std::error_code error;
    plan.gameDirectoryExists = fs::is_directory(plan.workingDirectory, error) && !error;
    plan.executableExists = regularFile(plan.executable);
    plan.levelExists = regularFile(level);
    plan.episodeExists = regularFile(episode);
    plan.installMarkerExists = !plan.directMapStart || regularFile(marker);
    plan.ready = plan.gameDirectoryExists && plan.executableExists && plan.levelExists
        && plan.episodeExists && plan.installMarkerExists;
    return plan;
}
} // namespace sed::level
