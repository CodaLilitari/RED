#include "../agent_workspace.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"
#include "sed/values.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
int failures = 0;
int requestNumber = 0;

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const sed_agent::Json& property(const sed_agent::Json& value, const char* name)
{
    const auto* found = value.find(name);
    if ( !found )
    {
        throw std::runtime_error(std::string("missing detail property: ") + name);
    }
    return *found;
}

sed_agent::Json query(const fs::path& root, const fs::path& source, const std::string& body)
{
    const auto path = root / ("detail-request-" + std::to_string(++requestNumber) + ".json");
    std::ofstream output(path, std::ios::binary);
    output << body;
    output.close();
    return sed_agent::runCommand("query", { source.string(), path.string() });
}

void runTests(const fs::path& root)
{
    const auto starter = root / "detail-starter.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    auto level = sed::assets::loadFromJKL(starter);
    level.sectors[0].surfaces[1].texVertices[0] = { -16, 32, { 0.75f, 0.5f, 0.25f, 0.125f } };
    sed::ThingTemplate item{};
    item.name = "query_item";
    item.parent = "none";
    item.values.push_back(sed::deserializeTplValue("move", "path"));
    level.templates.push_back(item);
    auto& thing = level.newThing();
    thing.name = "query_thing";
    thing.templateName = item.name;
    thing.sector = 0;
    thing.x = 0.25;
    thing.pch = 10;
    thing.yaw = 20;
    thing.rol = 30;
    thing.values.push_back(sed::deserializeTplValue("health", "25"));
    thing.values.push_back(sed::deserializeTplValue("health", "75"));
    thing.values.push_back(sed::deserializeTplValue("frame", "(0/0/0:0/0/0)"));
    thing.values.push_back(sed::deserializeTplValue("frame", "(1/0/0:0/0/0)"));
    auto& firstLight = level.newLight();
    firstLight.position = { 0.25, 0.5, 0.75 };
    firstLight.color = { 0.25f, 0.5f, 1.0f, 1.0f };
    firstLight.intensity = 3;
    firstLight.range = 4;
    auto& secondLight = level.newLight();
    secondLight.rgbIntensity = 0;
    secondLight.legacyRgbRange = -4;
    std::vector<sed::assets::SedOptionalSection> sections;
    if ( auto rgb = sed::assets::makeSedLightRgbSection(level) ) sections.push_back(*rgb);
    const auto bytes = sed::assets::saveToSedBytes(level, sections);
    const auto source = root / "query-details.sed";
    std::ofstream output(source, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    const auto before = sed_agent::readFileBytes(source);
    for ( int scenario = 0; scenario < 4; ++scenario )
    {
        try
        {
            if ( scenario == 0 )
            {
                const auto result = query(root, source, R"({"collection":"surfaces","sector":0,"offset":1,"limit":2})");
                const auto& rows = property(result, "items").array;
                check(rows.size() == 2 && property(result, "total").number == 6
                        && property(result, "nextOffset").number == 3, "surface detail pagination uses full collection count");
                const auto& surface = rows[0];
                check(property(surface, "index").number == 1 && property(surface, "sector").number == 0,
                    "surface row preserves its exact local address");
                const auto& corners = property(surface, "corners").array;
                check(corners.size() == 4, "surface row exposes every corner in order");
                check(property(corners[0], "vertex").number == level.sectors[0].surfaces[1].vertices[0]
                        && property(corners[0], "uv").array[0].number == -16
                        && property(corners[0], "uv").array[1].number == 32, "corner vertex and UV identity are readable");
                check(property(corners[0], "mono").number == 0.125
                        && property(corners[0], "rgb").array[0].number == 0.75, "corner mono/RGB readback remains independent");
                check(property(surface, "uvCoordinateSpace").str == "texturePixels", "surface row declares UV units");
                check(property(surface, "materialIndex").number == 0 && surface.find("geometryMode")
                        && surface.find("lightMode") && surface.find("textureMode") && surface.find("adjoin"),
                    "surface row exposes material, modes, and portal state");
                check(property(result, "inputSha256").str == sed_agent::sha256Hex(before), "detail output binds source bytes");
            }
            else if ( scenario == 1 )
            {
                const auto result = query(root, source, R"({"collection":"vertices","sector":0,"offset":2,"limit":3})");
                const auto& rows = property(result, "items").array;
                check(rows.size() == 3 && property(result, "total").number == 8, "vertex detail pagination is exact");
                check(property(rows[0], "index").number == 2 && property(rows[0], "position").array.size() == 3,
                    "vertex row exposes local address and position");
            }
            else if ( scenario == 2 )
            {
                const auto result = query(root, source, R"({"collection":"things"})");
                const auto& row = property(result, "items").array[0];
                check(property(row, "position").array[0].number == 0.25
                        && property(row, "orientation").array[2].number == 30, "Thing pose is readable");
                const auto& records = property(row, "sourceParameters").array;
                check(records.size() == 4 && property(records[0], "name").str == "health"
                        && property(records[1], "name").str == "health"
                        && property(records[1], "value").str == "75"
                        && property(records[3], "name").str == "frame", "Thing local parameter readback preserves duplicates and order");
                check(property(row, "archLightingIndex").number == -1, "Thing ARCH binding is readable");
            }
            else
            {
                const auto result = query(root, source, R"({"collection":"lights"})");
                const auto& rows = property(result, "items").array;
                check(property(rows[0], "position").array[2].number == 0.75
                        && property(rows[0], "rgb").array[0].number == 0.25, "source-light position/color are readable");
                check(property(rows[0], "rgbIntensity").type == sed_agent::Json::Type::Null
                        && property(rows[0], "effectiveRgbIntensity").number == 3, "inherited RGB intensity is explicit in readback");
                check(property(rows[1], "rgbIntensity").number == 0
                        && property(rows[1], "rgbIntensity").type == sed_agent::Json::Type::Number
                        && property(rows[1], "legacyRgbRange").number == -4, "explicit zero and legacy metadata are readable");
            }
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(false, "detail scenario " + std::to_string(scenario) + ": " + error.message);
        }
        catch ( const std::exception& error )
        {
            check(false, "detail scenario " + std::to_string(scenario) + ": " + error.what());
        }
    }
    for ( const std::string requestText : { R"({"collection":"surfaces","sector":0.5})",
        R"({"collection":"vertices","sector":99})", R"({"collection":"surfaces","sector":0,"limit":0})" } )
    {
        try
        {
            (void)query(root, source, requestText);
            check(false, "malformed geometry detail request must refuse");
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == "queryDetailsInvalid", "detail refusal has a stable code");
        }
    }
    check(sed_agent::readFileBytes(source) == before, "all detailed queries leave source bytes unchanged");
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-query-details-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try
    {
        runTests(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "unexpected detail error: " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected detail error: ") + error.what());
    }
    fs::remove_all(root);
    std::cout << "query detail failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
