#include "../agent_workspace.hpp"
#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace
{
namespace fs = std::filesystem;
int failures{};
void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main()
{
    const auto root = fs::temp_directory_path() / ("red-array-rgb-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    struct Cleanup
    {
        fs::path path;
        ~Cleanup()
        {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } cleanup{root};
    try
    {
        struct Case
        {
            const char* token;
            std::optional<double> expected;
            int error;
            sed::ProjectType project;
        };
        const std::array cases{
            Case{"null", {}, 0, sed::ProjectType::MOTS},
            Case{"0", 0.0, 0, sed::ProjectType::MOTS},
            Case{"0.75", 0.75, 0, sed::ProjectType::MOTS},
            Case{"1000", 1000.0, 0, sed::ProjectType::MOTS},
            Case{"-1", {}, 5, sed::ProjectType::MOTS},
            Case{"1000.01", {}, 5, sed::ProjectType::MOTS},
            Case{"\"inherit\"", {}, 2, sed::ProjectType::MOTS},
            Case{"0", {}, 5, sed::ProjectType::JKDF2},
            Case{"null", {}, 0, sed::ProjectType::JKDF2}};
        std::size_t index{};
        for ( const auto& item : cases )
        {
            sed::Level level;
            level.kind = item.project;
            const auto bytes = sed::assets::saveToSedBytes(level);
            const auto input = root / (std::to_string(index) + ".sed");
            const auto request = root / (std::to_string(index) + ".json");
            const auto output = root / (std::to_string(index++) + "-out.sed");
            std::ofstream source(input, std::ios::binary);
            source.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            source.close();
            std::ofstream patch(request);
            patch << R"({"schemaVersion":1,"operations":[{"op":"placeLightArray","layout":{"kind":"linear","count":2,"step":[1,0,0]},"intensity":0.75,"rgbIntensity":)"
                << item.token << "}]}";
            patch.close();
            int error{};
            try
            {
                (void)sed_agent::runCommand("apply", {input.string(), request.string(), output.string()});
            }
            catch ( const sed_agent::AgentError& refusal )
            {
                error = refusal.code;
            }
            check(error == item.error, "agent RGB array has the exact admission outcome");
            if ( item.error != 0 )
            {
                check(!fs::exists(output), "refused RGB array creates no output");
                continue;
            }
            const auto savedBytes = sed_agent::readFileBytes(output);
            auto file = sed::assets::loadSedFileBytes({
                reinterpret_cast<const std::byte*>(savedBytes.data()), savedBytes.size()});
            const bool hasRgbSection = sed::assets::applySedLightRgbSection(file.level, file.optionalSections);
            check(hasRgbSection == item.expected.has_value(),
                "native RGB section distinguishes absent inheritance from every explicit value");
            const auto& saved = file.level;
            check(saved.lights.size() == 2, "RGB array native output retains both lights");
            for ( const auto& light : saved.lights )
            {
                check(light.rgbIntensity == item.expected && light.intensity == 0.75,
                    "RGB array preserves inheritance and explicit metadata independently of white");
            }
        }
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        ++failures;
    }
    catch ( const sed_agent::AgentError& error )
    {
        std::cerr << error.message << '\n';
        ++failures;
    }
    return failures != 0;
}
