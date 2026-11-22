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

void sourceAdmission(const fs::path& root)
{
    // Minimal authored source tables. Counts apply to the imported representation.
    const std::string jkl = "SECTION: HEADER\nVERSION 1\nEND\nSECTION: SECTORS\nWORLD SECTORS 0\nEND\n";
    const std::string jed = "SED 0.3.0\nSEC: GEO\nSECS 0\nEND\n";
    const std::string asc = "Tri-mesh, Vertices: 0 Faces: 0\nVertex list:\nFace list:\n";
    const std::string lev = "LEV 2.1\nNUMSECTORS 0\n";
    int failures = 0;
    const auto verify = [&](const char* name, const std::string& input, const char* collection, int expected)
    {
        const auto path = root / name;
        std::ofstream(path, std::ios::binary) << input;
        const auto file = root / "source-check.json";
        std::ofstream(file) << R"({"schemaVersion":1,"checks":[{"name":"rows","query":{"collection":")"
            << collection << '"' << (std::string_view(collection) == "surfaces" ? R"(,"sector":0)" : "")
            << R"(},"expect":{"min":0}}]})";
        try
        {
            const auto report = sed_agent::runCommand("map-requirements", { path.string(), file.string() });
            if ( expected >= 0 && report.object.at("checks").array.at(0).object.at("actual").number == expected )
            {
                return;
            }
        }
        catch ( const sed_agent::AgentError& error )
        {
            if ( expected < 0 && error.code == sed_agent::kExitInput && !error.hasPayload )
            {
                return;
            }
        }
        ++failures;
        std::cerr << "Unexpected map-requirements admission/count for " << name << '\n';
    };
    verify("foreign.jkl", "SECTION: NOTES\nNot a map\n", "things", -1);
    verify("header-only.jkl", "SECTION: HEADER\nVERSION 1\n", "things", -1);
    verify("empty.jkl", jkl + "SECTION: THINGS\nWORLD THINGS 64\nEND\n", "things", 0);
    verify("incomplete.jkl", jkl + "SECTION: THINGS\nWORLD THINGS 64\n0: partial\nEND\n", "things", -1);
    verify("unclosed.jkl", jkl + "SECTION: THINGS\nWORLD THINGS 64\n", "things", -1);
    verify("extra.jkl", jkl + "SECTION: THINGS\nWORLD THINGS 0\n0: tpl actor 0 0 0 0 0 0 -1\nEND\n", "things", -1);
    verify("light.ndy", "SECTION: HEADER\nVERSION 3\nEND\nSECTION: SECTORS\nWORLD SECTORS 0\nEND\n"
        "SECTION: LIGHTS\nEDITOR LIGHTS 1\n0: partial\nEND\n", "lights", -1);
    verify("bad-flag.jkl", "SECTION: HEADER\nVERSION 1\nEND\nSECTION: SECTORS\nWORLD SECTORS 1\n"
        "SECTOR 0\nFLAGS x\nSURFACES 0 0\nEND\n", "sectors", -1);
    verify("layers.jkl", jkl + "SECTION: LAYERS\nEDITOR LAYERS 1\nbase\n0:\n1:\nEND\n", "things", -1);
    verify("thing-fields.jkl", jkl + "SECTION: THINGS\nWORLD THINGS 2\n0: tpl actor junk 0 0 0 0 0 -1\nEND\n", "things", -1);
    verify("omitted-layer.jkl", jkl + "SECTION: LAYERS\nEDITOR LAYERS 1\nbase\n0:\n0:\nEND\n", "things", 0);
    const std::string geometry = "SECTION: GEORESOURCE\nWORLD COLORMAPS 0\nWORLD VERTICES 1\n";
    const std::string geometryEnd = "\nWORLD TEXTURE VERTICES 0\nWORLD ADJOINS 0\nWORLD SURFACES 0\nEND\n";
    verify("valid-geometry.jkl", jkl + geometry + "0: 0 0 0" + geometryEnd, "sectors", 0);
    verify("dropped-vertex.jkl", jkl + geometry + "0: partial" + geometryEnd, "sectors", -1);
    verify("label.jkl", jkl + "SECTION: THINGS\nWORLD THINGS 2\ninvalid tpl actor 0 0 0 0 0 0 -1\nEND\n", "things", -1);
    verify("empty.ndy", "SECTION: HEADER\nVERSION 3\nEND\nSECTION: SECTORS\nWORLD SECTORS 0\nEND\n", "lights", 0);
    verify("empty.jed", jed, "sectors", 0);
    verify("blank-geo.jed", "SED 0.3.0\nSEC: GEO\n\nEND\n", "sectors", -1);
    verify("blank-things.jed", jed + "SEC: THINGS\n\nEND\n", "things", -1);
    verify("blank-lights.jed", jed + "SEC: LIGHTS\n\nEND\n", "lights", -1);
    verify("empty-things.jed", jed + "SEC: THINGS\n\nTHS 0\n\nEND\n", "things", 0);
    verify("empty-lights.jed", jed + "SEC: LIGHTS\nLTS 0\nEND\n", "lights", 0);
    verify("extra-layer.jkl", jkl + "SECTION: THINGS\nWORLD THINGS 1\n0: tpl actor 0 0 0 0 0 0 -1\nEND\n"
        "SECTION: LAYERS\nEDITOR LAYERS 1\nbase\n0:\n0:\nextra\n0:\n1: 0\nEND\n", "things", -1);
    verify("visibility.jkl", jkl + "SECTION: LAYERS\nEDITOR LAYERS 1\nbase\n0:\n0:\n1\nEND\n", "sectors", 0);
    verify("unframed-legacy-thing.jed", "JED 0.1\nVERSION 1\nSECTORS 0\nTHINGS 0\nTHING actor\n", "things", -1);
    verify("unframed-legacy-light.jed", "JED 0.1\nVERSION 1\nSECTORS 0\nLIGHTS 0\nLIGHT 0\n", "lights", -1);
    verify("partial.ned", jed + "SEC: THINGS\nTHS 1\npartial\nEND\n", "things", -1);
    verify("foreign.jed", "unrelated\n", "things", -1);
    verify("legacy.jed", "JED 0.1\nVERSION 1\nSECTORS 0\n", "sectors", 0);
    verify("missing-legacy-sector.jed", "JED 0.1\nVERSION 1\nSECTORS 2\nSECTOR 0 0\nVERTICES 0\nSURFACES 0\n", "sectors", -1);
    verify("short-surface.jed", "SED 0.3.0\nSEC: GEO\nSECS 1\n- - 0 0 0 0 0 0 0 0\nVXS 0\nSURFS 2\n"
        "- 0 0 0 0 0 0 0 -1 -1 0 1 1\n\nEND\n", "sectors", -1);
    verify("unread-legacy-things.jed", "JED 0.1\nVERSION 1\nSECTORS 0\nTHINGS 1\nTHING actor\n", "things", -1);
    verify("no-legacy-lights.jed", "JED 0.1\nVERSION 1\nSECTORS 0\nTHINGS 0\nLIGHTS 0\n", "lights", 0);
    verify("valid-actor.ned", jed + "SEC: THINGS\nTHS 1\ntrooper 0 0 0 0 0 0 -1 0\nhealth=20\nEND\n", "things", 1);
    verify("empty.asc", asc, "things", 1);
    const std::string meshVertices = "Tri-mesh, Vertices: 3 Faces: 0\nVertex list:\n"
        "Vertex 0: x 0 y 0 z 0\nVertex 1: x 1 y 0 z 0\nVertex 2: x 0 y 1 z 0\n";
    verify("face-as-header.asc", meshVertices + "Face 0: a 0 b 1 c 2\n", "surfaces", -1);
    verify("vertex-as-header.asc", "Tri-mesh, Vertices: 0 Faces: 0\nVertex 0: x 0 y 0 z 0\nFace list:\n", "sectors", -1);
    verify("unused-vertices.asc", meshVertices + "Face list:\n", "surfaces", 0);
    verify("partial.asc", "Tri-mesh, Vertices: 0 Faces: 1\nVertex list:\nFace list:\n", "sectors", -1);
    verify("foreign.asc", "not a mesh", "things", -1);
    verify("empty.lev", lev, "sectors", 0);
    verify("partial.lev", "LEV 2.1\nNUMSECTORS 1\n", "sectors", -1);
    const std::string walls = "LEV 2.1\nNUMSECTORS 1\nSECTOR 0\nFLOOR ALTITUDE 40\nCEILING ALTITUDE 0\n"
        "VERTICES 4\nX: 0 Z: 0\nX: 40 Z: 0\nX: 40 Z: 40\nX: 0 Z: 40\nWALLS 4\n"
        "WALL LEFT: 0 RIGHT: 3\nWALL LEFT: 3 RIGHT: 2\nWALL LEFT: 2 RIGHT: 1\n";
    verify("converted.lev", walls + "WALL LEFT: 1 RIGHT: 0\n", "sectors", 1);
    verify("surplus-wall.lev", walls + "WALL LEFT: 1 RIGHT: 0\nWALL LEFT: 0 RIGHT: 2\n", "surfaces", -1);
    auto twoSectors = walls;
    twoSectors.replace(twoSectors.find("NUMSECTORS 1"), 12, "NUMSECTORS 2");
    verify("inter-sector-wall.lev", twoSectors + "WALL LEFT: 1 RIGHT: 0\nWALL LEFT: 0 RIGHT: 2\nSECTOR 1\n"
        + walls.substr(walls.find("FLOOR ALTITUDE")) + "WALL LEFT: 1 RIGHT: 0\n", "surfaces", -1);
    verify("unclosed.lev", walls + "WALL LEFT: 1 RIGHT: 3\n", "sectors", -1);
    verify("foreign.lev", "other text", "things", -1);
    check(failures == 0, "only complete map records can produce counts");
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
        sourceAdmission(root);
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
