#include "../agent_workspace.hpp"
#include "../agent_query_details.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/values.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
int failures{};
int requestNumber{};

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
    const auto path = root / ("query-" + std::to_string(++requestNumber) + ".json");
    std::ofstream output(path, std::ios::binary);
    output << body;
    output.close();
    return sed_agent::runCommand("query", { source.string(), path.string() });
}

void save(const sed::Level& level, const fs::path& path)
{
    const auto bytes = sed::assets::saveToSedBytes(level);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

template <typename Function>
void scenario(const char* name, Function function)
{
    try
    {
        function();
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, std::string(name) + ": " + error.errorCode + ": " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string(name) + ": " + error.what());
    }
}

void runTests(const fs::path& root)
{
    const auto starter = root / "starter.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    auto level = sed::assets::loadFromJKL(starter);
    level.templates.clear();
    level.things.clear();
    level.cogs.clear();
    auto& sector = level.sectors[0];
    sector.flags = -2147483647;
    sector.sound = "room.wav";
    sector.soundVolume = 0.25;
    sector.thrust = { 1, 2, 3 };
    sector.ambient = { 0.125f, 0.25f, 0.5f, 1.0f };
    for ( const auto* name : { "DoorBase", "other", "doorChild" } )
    {
        sed::ThingTemplate item{};
        item.name = name;
        item.parent = std::string(name) == "doorChild" ? "DoorBase" : "none";
        item.values.push_back(sed::deserializeTplValue("health", "25"));
        item.values.push_back(sed::deserializeTplValue("health", "75"));
        level.templates.push_back(std::move(item));
    }
    auto& cog = level.newCog();
    cog.name = "door.cog";
    sed::CogValue first{};
    first.name = "target";
    first.type = sed::ValueType::integer;
    first.intValue = 17;
    cog.values.push_back(first);
    first.intValue = 23;
    cog.values.push_back(first);
    auto& inside = level.newThing();
    inside.name = "DoorInside";
    inside.templateName = "doorChild";
    inside.sector = 0;
    inside.archLightingIndex = 0;
    auto& boundary = level.newThing();
    boundary.name = "boundary";
    boundary.templateName = "other";
    boundary.sector = 0;
    boundary.x = 1;
    auto& outside = level.newThing();
    outside.name = "outside";
    outside.templateName = "other";
    outside.sector = 0;
    outside.x = 5;
    sed::MotsArchLightObject object{};
    object.meshes.resize(1);
    object.meshes[0].vertices = { { 0.125f, 0.25f, 0.5f, 0.75f }, { 0.5f, 0.75f, 0.25f, 0.125f } };
    level.motsArchLighting.push_back(object);
    const auto source = root / "records.sed";
    save(level, source);
    const auto before = sed_agent::readFileBytes(source);

    scenario("sector details", [&]
    {
        const auto result = query(root, source, R"({"collection":"sectors"})");
        const auto& row = property(result, "items").array.at(0);
        check(property(row, "flags").number == -2147483647, "sector flags retain signed legacy representation");
        check(property(row, "sound").str == "room.wav" && property(row, "soundVolume").number == 0.25,
            "sector authored sound is readable");
        check(property(row, "thrust").array.at(2).number == 3
                && property(row, "ambientRgb").array.at(1).number == 0.25,
            "sector vector and color fields remain independent");
        check(property(property(row, "geometryBounds"), "min").array.at(0).number == -1
                && property(property(row, "geometryBounds"), "max").array.at(2).number == 1,
            "sector bounds describe saved geometry");
        check(property(row, "neighbors").array.empty() && property(row, "neighborReferencesValid").boolean,
            "unadjoined room exposes an empty valid neighbor list");
        check(property(result, "inputSha256").str == sed_agent::sha256Hex(before), "rich query binds exact source bytes");
    });
    scenario("template details and filtered pagination", [&]
    {
        const auto page = query(root, source, R"({"collection":"templates","search":"DOOR","limit":1})");
        check(property(page, "total").number == 2 && property(page, "nextOffset").number == 1,
            "case-insensitive filter precedes pagination");
        const auto next = query(root, source, R"({"collection":"templates","search":"DOOR","offset":1,"limit":1})");
        const auto& row = property(next, "items").array.at(0);
        check(property(row, "index").number == 2 && property(row, "parent").str == "DoorBase",
            "filtered row retains document index and declared parent");
        const auto& records = property(row, "sourceParameters").array;
        check(records.size() == 2 && property(records[0], "name").str == "health"
                && property(records[1], "name").str == "health" && property(records[1], "value").str == "75",
            "template declarations preserve duplicate keys and order");
        check(property(row, "parameterScope").str == "declared" && property(next, "nextOffset").type == sed_agent::Json::Type::Null,
            "template values are declarations and final pagination is explicit");
    });
    scenario("COG details", [&]
    {
        const auto result = query(root, source, R"({"collection":"cogs","search":"DOOR"})");
        const auto& row = property(result, "items").array.at(0);
        const auto& parameters = property(row, "sourceParameters").array;
        check(parameters.size() == 2 && property(parameters[0], "name").str == "target"
                && property(parameters[1], "value").str == "23", "COG parameters retain duplicate symbol records");
        check(property(row, "values").array.at(0).str == "17" && property(row, "values").array.at(1).str == "23",
            "compatible COG positional values remain available");
        check(property(row, "bindingStatus").str == "unverified-use-bindings-with-explicit-resources",
            "stored COG records do not imply validated bindings");
    });
    scenario("ARCH summaries and sample page", [&]
    {
        const auto result = query(root, source, R"({"collection":"architecture"})");
        const auto& row = property(result, "items").array.at(0);
        check(property(row, "meshVertexCounts").array.at(0).number == 2
                && property(row, "boundThings").array.at(0).number == 0, "ARCH summary exposes stored topology and owners");
        check(property(row, "samplesSha256").str.size() == 64
                && property(row, "modelCompatibility").str == "unverified-use-arch-validate-with-explicit-resources",
            "ARCH samples have a digest without claiming model compatibility");
        const auto samples = query(root, source,
            R"({"collection":"architecturalVertices","lightingIndex":0,"mesh":0,"offset":1,"limit":1})");
        const auto& sample = property(samples, "items").array.at(0);
        check(property(samples, "total").number == 2 && property(sample, "index").number == 1
                && property(sample, "mono").number == 0.5 && property(sample, "rgb").array.at(2).number == 0.125,
            "sample pagination preserves independent mono and RGB channels");
    });
    scenario("point containment", [&]
    {
        const auto result = query(root, source, R"({"collection":"thingContainment"})");
        const auto& rows = property(result, "items").array;
        check(rows.size() == 3 && property(rows[0], "status").str == "inside"
                && property(rows[1], "status").str == "boundary" && property(rows[2], "status").str == "outside",
            "inside, boundary and outside have distinct semantics");
        check(property(rows[0], "assignedSectorMatches").boolean && property(rows[1], "onBoundary").boolean
                && !property(rows[2], "assignedSectorMatches").boolean, "containment reports assignment independently");
        const auto filtered = query(root, source, R"({"collection":"thingContainment","search":"doorchild"})");
        check(property(filtered, "total").number == 1, "containment search includes template names");
        auto overlapping = level;
        overlapping.sectors.push_back(overlapping.sectors[0]);
        const auto overlapPath = root / "overlap.sed";
        save(overlapping, overlapPath);
        const auto ambiguous = query(root, overlapPath, R"({"collection":"thingContainment","limit":1})");
        const auto& row = property(ambiguous, "items").array.at(0);
        check(property(row, "status").str == "ambiguous"
                && property(row, "assignedSectorMatches").type == sed_agent::Json::Type::Null
                && property(row, "onBoundary").type == sed_agent::Json::Type::Null,
            "overlapping rooms do not invent a unique or matching assignment");
    });
    for ( const auto* body : { R"({"collection":"templates","offset":0.5})",
        R"({"collection":"cogs","search":7})", R"({"collection":"sectors","limit":0})",
        R"({"collection":"architecturalVertices","lightingIndex":0})",
        R"({"collection":"architecturalVertices","lightingIndex":99,"mesh":0})",
        R"({"collection":"templates","mesh":0})" } )
    {
        try
        {
            (void)query(root, source, body);
            check(false, std::string("malformed rich query must refuse: ") + body);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == "queryDetailsInvalid", "rich query refusal has a stable usage code");
        }
    }
    check(sed_agent::readFileBytes(source) == before, "rich queries never modify the source file");
}

sed_agent::Json directQuery(const sed::Level& level, const std::string& body)
{
    auto result = sed_agent::queryRecordDetails(level, sed_agent::parseJson(body));
    if ( !result )
    {
        throw std::runtime_error("rich collection was not dispatched");
    }
    return std::move(*result);
}

void expectTooLarge(const sed::Level& level, const std::string& body, const std::string& description)
{
    try
    {
        (void)directQuery(level, body);
        check(false, description + " must refuse before expensive row construction");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == sed_agent::kExitRefusal && error.errorCode == "queryDetailsTooLarge",
            description + " must have the stable detail-budget refusal");
    }
}

void readbackLimits(const fs::path& root)
{
    sed::Level level{};
    level.templates.resize(1);
    level.templates[0].name = "large";
    level.templates[0].values.assign(4097, sed::deserializeTplValue("health", "1"));
    expectTooLarge(level, R"({"collection":"templates"})", "nested parameter budget");
    auto empty = directQuery(level, R"({"collection":"templates","offset":1})");
    check(property(empty, "items").array.empty(), "unselected large parameter records are not materialized");
    level.templates[0].values.clear();
    sed::TplValue text{};
    text.name = "text";
    text.type = sed::ValueType::str;
    text.stringValue.assign(1024 * 1024, 'a');
    level.templates[0].values.push_back(std::move(text));
    expectTooLarge(level, R"({"collection":"templates"})", "authored text budget");

    level = {};
    level.cogs.resize(1);
    level.cogs[0].name = "large.cog";
    sed::CogValue value{};
    value.name = "text";
    value.type = sed::ValueType::str;
    value.stringValue.assign(600 * 1024, 'x');
    level.cogs[0].values.push_back(std::move(value));
    expectTooLarge(level, R"({"collection":"cogs"})", "two COG value representations share text budget");

    level = {};
    level.motsArchLighting.resize(1);
    level.motsArchLighting[0].meshes.resize(1);
    level.motsArchLighting[0].meshes[0].vertices.resize(1'000'001);
    expectTooLarge(level, R"({"collection":"architecture"})", "ARCH sample digest budget");
    const auto sample = directQuery(level,
        R"({"collection":"architecturalVertices","lightingIndex":0,"mesh":0,"offset":1000000,"limit":1})");
    check(property(sample, "items").array.size() == 1 && property(sample, "total").number == 1'000'001,
        "one sample remains readable without hashing the whole ARCH object");
    level = {};
    level.motsArchLighting.resize(101);
    level.things.resize(10'000);
    expectTooLarge(level, R"({"collection":"architecture","limit":200})", "cumulative ARCH owner scan budget");

    const auto starter = root / "nonfinite-source.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    level = sed::assets::loadFromJKL(starter);
    level.things.clear();
    level.newThing().sector = 0;
    level.sectors[0].vertices[0].x = std::numeric_limits<double>::quiet_NaN();
    const auto unknown = directQuery(level, R"({"collection":"thingContainment"})");
    const auto& row = property(unknown, "items").array.at(0);
    check(property(row, "status").str == "unknown"
            && property(row, "assignedSectorMatches").type == sed_agent::Json::Type::Null
            && property(row, "candidates").type == sed_agent::Json::Type::Null,
        "nonfinite geometry cannot certify outside or a sector assignment");
    const auto sectors = directQuery(level, R"({"collection":"sectors"})");
    check(property(property(sectors, "items").array.at(0), "geometryBounds").type == sed_agent::Json::Type::Null,
        "nonfinite sector bounds remain null");

    level = sed::assets::loadFromJKL(starter);
    const auto cube = level.sectors[0];
    level.sectors.assign(400, cube);
    level.things.clear();
    level.things.resize(200);
    // All rooms are valid but overlap, so the selector must classify them all.
    // Each precision plane comparison also walks the face's stored corners.
    expectTooLarge(level, R"({"collection":"thingContainment","limit":200})",
        "containment admission includes precision face-corner work across the page");
}

void filterAndContainmentWork()
{
    sed::Level level{};
    level.templates.resize(33);
    for ( auto& item : level.templates )
    {
        item.name.assign(256 * 1024, 'a');
    }
    // No rows match, so an output-page allowance alone cannot bound this scan.
    // The proposed cumulative eight-MiB search allowance must reject it.
    expectTooLarge(level, R"({"collection":"templates","search":"Z","limit":1})",
        "unmatched names share a cumulative search budget");
    level = {};
    level.templates.resize(2);
    level.templates[0].name = std::string(8192, 'A') + "b";
    level.templates[1].name = std::string(8192, 'A') + "c";
    const auto pattern = std::string(512, 'a') + "B";
    const auto result = directQuery(level,
        "{\"collection\":\"templates\",\"search\":\"" + pattern + "\"}");
    check(property(result, "total").number == 1
            && property(property(result, "items").array.at(0), "index").number == 0,
        "long repetitive prefixes preserve ASCII case-insensitive substring semantics");

    level = {};
    level.sectors.resize(100'001);
    level.things.resize(200);
    // Empty sectors still cost one classification attempt per queried Thing.
    expectTooLarge(level, R"({"collection":"thingContainment","limit":200})",
        "empty-sector scans count toward cumulative containment work");
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-extended-query-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    scenario("extended query fixture", [&] { runTests(root); });
    scenario("readback limits", [&] { readbackLimits(root); });
    scenario("filter and containment work", filterAndContainmentWork);
    fs::remove_all(root);
    std::cout << "extended query failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
