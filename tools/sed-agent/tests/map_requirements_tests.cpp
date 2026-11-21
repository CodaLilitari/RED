#include "../agent_workspace.hpp"
#include "../utf8_path.hpp"
#include "sed/assets/sed.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
using sed_agent::parseJson;

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

void run(const fs::path& root)
{
    // Original synthetic authored records. More matches than a full Workbench page.
    sed::Level level;
    level.layers = { "base", "mission" };
    level.materials = { "wall.mat", "other.mat" };
    level.sectors.resize(1);
    level.sectors.front().layer = 1;
    level.sectors.front().flags = 9;
    level.sectors.front().surfaces.resize(2);
    level.sectors.front().surfaces.front().material = "wall.mat";
    level.lights.resize(1);
    level.lights.front().flags = 5;
    for ( int i = 0; i < 205; ++i )
    {
        auto& thing = level.newThing();
        thing.templateName = "guard";
        thing.name = "sentry";
    }
    auto& beacon = level.newThing();
    beacon.name = "Relay";
    beacon.templateName = "beacon";
    beacon.layer = 1;
    const auto bytes = sed::assets::saveToSedBytes(level);
    const auto source = root / "records.sed";
    std::ofstream(source, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    const auto file = root / "checks.json";
    const auto evaluate = [&](const Json& request)
    {
        std::ofstream(file, std::ios::binary) << sed_agent::toJsonString(request);
        return sed_agent::runCommand("map-requirements", { sed_agent::pathToUtf8(source), sed_agent::pathToUtf8(file) });
    };
    const auto request = parseJson(R"({"schemaVersion":1,"checks":[
        {"name":"relay","query":{"collection":"things","filter":{"name":"relay","templateName":"BEACON","layer":1}},"expect":{"exactly":1}},
        {"name":"no placeholders","query":{"collection":"things","search":"placeholder"},"expect":{"max":0}},
        {"name":"reinforcements","query":{"collection":"things","filter":{"templateName":"guard"}},"expect":{"min":206}},
        {"name":"light flags","query":{"collection":"lights","filter":{"flags":{"all":5,"none":8}}},"expect":{"exactly":1}},
        {"name":"sector flags","query":{"collection":"sectors","filter":{"layer":1,"flags":{"any":8}}},"expect":{"min":1,"max":1}},
        {"name":"walls","query":{"collection":"surfaces","sector":0,"filter":{"materialIndex":0}},"expect":{"exactly":1}}
    ]})");
    const auto report = evaluate(request);
    check(report.object.at("ok").boolean && !report.object.at("passed").boolean, "completed failing report");
    check(report.object.at("inputSha256").str == sed_agent::sha256Hex(bytes), "evaluated source hash");
    const auto& rows = report.object.at("checks").array;
    check(rows.size() == 6, "all checks reported after a mismatch");
    for ( std::size_t i = 0; i < rows.size(); ++i )
    {
        const auto& supplied = request.object.at("checks").array[i];
        check(rows[i].object.at("name").str == supplied.object.at("name").str
                && sed_agent::toJsonString(rows[i].object.at("expect")) == sed_agent::toJsonString(supplied.object.at("expect")),
            "ordered names and expectations");
        check(rows[i].object.at("passed").boolean == (i != 2), "individual check status");
    }
    check(rows[1].object.at("actual").number == 0, "valid unmatched predicate counts zero");
    check(rows[2].object.at("actual").number == 205, "total independent of page size");
    check(sed_agent::toJsonString(evaluate(request)) == sed_agent::toJsonString(report), "deterministic replay");
    const auto one = parseJson(R"({"schemaVersion":1,"checks":[{"name":"faces","query":{"collection":"surfaces","sector":0,"filter":{"materialIndex":0}},"expect":{"min":1,"max":1}}]})");
    const auto refused = [&](const Json& invalid, int exitCode, std::string_view expectedCode = {})
    {
        try
        {
            (void)evaluate(invalid);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.code == exitCode && !error.hasPayload, "refusal has correct exit and no successful rows");
            check(expectedCode.empty() || expectedCode == error.errorCode, "refusal has the specific error code");
            return;
        }
        throw std::runtime_error("unexpected successful evaluation: " + sed_agent::toJsonString(invalid));
    };
    Json bounded = one;
    bounded.set("maxWorkUnits", Json::makeNumber(6));
    check(evaluate(bounded).object.at("passed").boolean, "exact work budget includes material lookups");
    bounded.set("maxWorkUnits", Json::makeNumber(5));
    refused(bounded, 5, "mapRequirementWorkLimit");
    bounded = one;
    Json second = bounded.object.at("checks").array.front();
    second.set("name", Json::makeString("again"));
    bounded.object.at("checks").array.push_back(second);
    bounded.set("maxWorkUnits", Json::makeNumber(11));
    refused(bounded, 5, "mapRequirementWorkLimit");
    bounded.set("maxWorkUnits", Json::makeNumber(12));
    check(evaluate(bounded).object.at("passed").boolean, "exact shared budget");
    for ( const auto* bad : { "-1", "4000001", "1.1", "false" } )
    {
        Json invalid = one;
        invalid.set("maxWorkUnits", parseJson(bad));
        refused(invalid, 2, "invalidMapRequirements");
    }
    for ( const auto* bad : { "{}", R"({"exactly":1,"min":0})", R"({"min":2,"max":1})", R"({"min":false})",
            R"({"min":"1"})", R"({"max":1.5})", R"({"exactly":-1})", R"({"max":2147483648})", R"({"exact":1})" } )
    {
        Json invalid = one;
        invalid.object.at("checks").array.front().set("expect", parseJson(bad));
        refused(invalid, 2);
    }
    for ( const auto* bad : { R"({"collection":"things","limit":1})", R"({"collection":"things","filter":{"layer":0,"naem":"x"}})",
            R"({"collection":"sectors","filter":{"flags":{"any":1,"al":1}}})", R"({"collection":"lights","filter":{"flags":{"any":4294967296}}})",
            R"({"collection":"thingContainment"})", R"({"collection":"lights","search":""})" } )
    {
        Json invalid = one;
        invalid.object.at("checks").array.front().set("query", parseJson(bad));
        refused(invalid, 2);
    }
    for ( const auto* bad : { R"({"collection":"surfaces","sector":2})", R"({"collection":"surfaces"})",
            R"({"collection":"sectors","filter":{"name":"missing"}})", R"({"collection":"lights","filter":{}})",
            R"({"collection":"sectors","filter":{"flags":{}}})" } )
    {
        Json invalid = bounded;
        invalid.object.at("checks").array.back().set("query", parseJson(bad));
        refused(invalid, 5);
    }
    for ( const auto* bad : { R"({"checks":[]})", R"({"schemaVersion":2,"checks":[]})", R"({"schemaVersion":1,"checks":[]})" } )
    {
        refused(parseJson(bad), 2);
    }
    Json invalid = one;
    invalid.set("unexpected", Json{});
    refused(invalid, 2);
    invalid = one;
    invalid.object.at("checks").array.front().set("output", Json{});
    refused(invalid, 2);
    invalid = one;
    invalid.object.at("checks").array.push_back(one.object.at("checks").array.front());
    refused(invalid, 2);
    for ( const auto& name : { std::string(), std::string("   "), std::string(129, 'a') } )
    {
        invalid = one;
        invalid.object.at("checks").array.front().set("name", Json::makeString(name));
        refused(invalid, 2);
    }
    invalid = one;
    invalid.object.at("checks").array.front().set("name", Json::makeString(std::string(128, 'a')));
    check(evaluate(invalid).object.at("passed").boolean, "128-byte check name is admitted");
    invalid.object.at("checks").array.front().set("name", Json::makeString(std::string(1, static_cast<char>(0xff))));
    refused(invalid, 2, "invalidMapRequirements");
    Json many = one;
    auto& checks = many.object.at("checks").array;
    for ( int i = 1; i < 64; ++i )
    {
        Json item = one.object.at("checks").array.front();
        item.set("name", Json::makeString(std::to_string(i)));
        checks.push_back(std::move(item));
    }
    check(evaluate(many).object.at("checks").array.size() == 64, "maximum check count");
    checks.push_back(second);
    refused(many, 2);
    check(sed_agent::sha256Hex(sed_agent::readFileBytes(source)) == sed_agent::sha256Hex(bytes), "source unchanged");
}
}

int main()
{
    const auto root = fs::temp_directory_path() / ("sed-map-requirements-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    int result{};
    try
    {
        run(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        std::cerr << error.message << '\n';
        result = 1;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fs::remove_all(root);
    return result;
}
