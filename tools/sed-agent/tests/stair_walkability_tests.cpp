#include "../agent_workspace.hpp"
#include "sed/assets/sed.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
int failures{};

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const Json& field(const Json& value, const char* name)
{
    const auto* result = value.find(name);
    if ( !result )
    {
        throw std::runtime_error(std::string("missing field: ") + name);
    }
    return *result;
}

struct Fixture
{
    fs::path root;
    fs::path source;
    std::string original;
    int sequence{};

    Fixture()
        : root(fs::temp_directory_path() / ("red-stair-advisory-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          source(root / "source.sed")
    {
        fs::create_directory(root);
        sed::Level level;
        const auto bytes = sed::assets::saveToSedBytes(level);
        original.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        sed_agent::writeNewFileAtomic(source, original);
    }
    ~Fixture()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path request(const std::string& operations)
    {
        const auto path = root / ("request-" + std::to_string(sequence++) + ".json");
        sed_agent::writeNewFileAtomic(path, "{\"schemaVersion\":1,\"operations\":[" + operations + "]}");
        return path;
    }
};

std::string stair(const std::string& direction = "+x", const int steps = 9,
    const std::string& policy = "", const bool switchback = false, const int x = 10)
{
    return "{\"op\":\"" + std::string(switchback ? "buildSwitchbackStairs" : "buildStairs")
        + "\",\"origin\":[" + std::to_string(x) + ",0,0],\"direction\":\"" + direction
        + "\",\"width\":0.5,\"run\":0.16666666666666666,\"rise\":0.0625,\"headroom\":0.5,"
        + (switchback ? "\"landingRun\":0.5,\"stepsPerFlight\":" : "\"steps\":")
        + std::to_string(steps) + (policy.empty() ? "" : ",\"walkability\":" + policy) + "}";
}

Json plan(Fixture& f, const std::string& op)
{
    const auto request = f.request(op);
    return sed_agent::runCommand("plan", { f.source.string(), request.string() });
}

void checkReport(const Json& result, const double limit, const int risers, const bool exceeds,
    const std::string& operation = "buildStairs", const int index = 0)
{
    const auto& reports = field(result, "stairWalkabilityReports");
    check(reports.array.size() == 1, "one explicit stair policy produces one report");
    check(reports.type == Json::Type::Array, "reports have an array type");
    const auto& report = reports.array.at(0);
    for ( const char* name : { "operationIndex", "requestedRise", "comparedRise", "internalRiserCount", "maxStepRise" } )
    {
        check(field(report, name).type == Json::Type::Number, std::string("numeric report field: ") + name);
    }
    for ( const char* name : { "exceedsConfiguredLimit", "gameplayVerified" } )
    {
        check(field(report, name).type == Json::Type::Bool, std::string("Boolean report field: ") + name);
    }
    check(field(report, "operationIndex").number == index, "report identifies the patch operation");
    check(field(report, "operation").str == operation, "report identifies the stair recipe");
    check(field(report, "scope").str == "recipe-internal-risers-at-creation", "report excludes later edits and external joins");
    check(field(report, "measurementBasis").str == "recipe-parameters", "report does not pretend to measure final serialized geometry");
    check(field(report, "requestedRise").number == 0.0625, "report retains requested rise");
    check(field(report, "comparedRise").number == (risers ? 0.0625 : 0.0), "only internal risers are compared");
    check(field(report, "internalRiserCount").number == risers, "internal riser count includes landing transitions");
    check(field(report, "maxStepRise").number == limit, "report preserves the explicit caller limit");
    check(field(report, "exceedsConfiguredLimit").boolean == exceeds, "limit comparison is strictly greater");
    check(!field(report, "gameplayVerified").boolean, "no advisory outcome claims gameplay verification");
    const auto& warnings = field(report, "warnings");
    check(warnings.type == Json::Type::Array, "warnings have an array type");
    check(warnings.array.size() == (exceeds ? 1u : 0u), "only an exceeded limit emits a warning");
    if ( exceeds )
    {
        const auto& warning = warnings.array.at(0);
        check(field(warning, "code").str == "stairRiseExceedsConfiguredLimit", "warning has a stable code");
        check(field(warning, "severity").str == "warning", "advisory is not a geometry refusal");
        check(field(warning, "message").str.find("maxStepRise") != std::string::npos,
            "warning identifies the caller-supplied constraint");
    }
}

void validContracts()
{
    Fixture f;
    const auto plain = plan(f, stair());
    check(!plain.find("stairWalkabilityReports"), "old requests do not receive a new report field");
    for ( const auto* direction : { "+x", "-x", "+y", "-y" } )
    {
        const auto result = plan(f, stair(direction, 9, R"({"maxStepRise":0.03125})"));
        checkReport(result, 0.03125, 8, true);
        const auto ordinary = plan(f, stair(direction));
        check(field(result, "outputSha256").str == field(ordinary, "outputSha256").str,
            "advisory does not change geometry or output identity in any direction");
    }
    checkReport(plan(f, stair("+x", 9, R"({"maxStepRise":0.0625})")), 0.0625, 8, false);
    checkReport(plan(f, stair("+x", 9, R"({"maxStepRise":0.125})")), 0.125, 8, false);
    checkReport(plan(f, stair("+x", 9, R"({"maxStepRise":100})")), 100, 8, false);
    checkReport(plan(f, stair("+x", 1, R"({"maxStepRise":0.03125})")), 0.03125, 0, false);
    checkReport(plan(f, stair("+x", 1, R"({"maxStepRise":0.03125})", true)),
        0.03125, 2, true, "buildSwitchbackStairs");
    checkReport(plan(f, stair("-y", 2, R"({"maxStepRise":0.03125})", true)),
        0.03125, 4, true, "buildSwitchbackStairs");
    checkReport(plan(f, R"({"op":"setGravity","value":3},)"
        + stair("+x", 9, R"({"maxStepRise":0.03125})")), 0.03125, 8, true, "buildStairs", 1);
    const auto multi = plan(f, stair("+x", 9, R"({"maxStepRise":0.03125})") + ","
        + stair("+x", 9, "", false, 20) + ","
        + stair("+x", 9, R"({"maxStepRise":0.125})", false, 30));
    const auto& multiReports = field(multi, "stairWalkabilityReports");
    check(multiReports.array.size() == 2
        && field(multiReports.array[0], "operationIndex").number == 0
        && field(multiReports.array[1], "operationIndex").number == 2,
        "multiple policies preserve ordered operation association");

    const auto transformed = plan(f, stair("+x", 9, R"({"maxStepRise":0.03125})")
        + R"(,{"op":"scaleSectors","sectors":[0,1,2,3,4,5,6,7,8],"pivot":[0,0,0],"factor":2})");
    checkReport(transformed, 0.03125, 8, true);
    check(field(transformed, "outputSha256").str != field(plain, "outputSha256").str,
        "later geometry edits change output while report remains creation-scoped");

    const auto request = f.request(stair("+x", 9, R"({"maxStepRise":0.03125})"));
    const auto output = f.root / "with-policy.sed";
    const auto planned = sed_agent::runCommand("plan", { f.source.string(), request.string() });
    check(!fs::exists(output), "planning publishes no level");
    const auto applied = sed_agent::runCommand("apply", { f.source.string(), request.string(), output.string(),
        field(planned, "outputSha256").str });
    check(sed_agent::toJsonString(field(planned, "stairWalkabilityReports"))
        == sed_agent::toJsonString(field(applied, "stairWalkabilityReports")), "plan/apply repeat the same advisory");
    const auto plainRequest = f.request(stair());
    const auto plainOutput = f.root / "without-policy.sed";
    (void)sed_agent::runCommand("apply", { f.source.string(), plainRequest.string(), plainOutput.string(),
        field(plain, "outputSha256").str });
    check(sed_agent::readFileBytes(output) == sed_agent::readFileBytes(plainOutput),
        "exceeded policy leaves persisted native bytes exactly unchanged");
    check(sed_agent::readFileBytes(f.source) == f.original, "all plans/applies preserve source bytes");
}

void invalidContracts()
{
    Fixture f;
    for ( const auto* policy : { "null", "true", "[]", "0.03125", "\"profile\"", "{}",
        R"({"maxStepRise":0})", R"({"maxStepRise":-1})", R"({"maxStepRise":100.01})",
        R"({"maxStepRise":null})", R"({"maxStepRise":true})", R"({"maxStepRise":"0.03"})",
        R"({"maxStepRise":1e309})", R"({"maxStepRise":0.03,"maxRise":1})" } )
    {
        const auto request = f.request(stair("+x", 9, policy));
        const auto output = f.root / "refused.sed";
        int error{};
        try
        {
            (void)sed_agent::runCommand("apply", { f.source.string(), request.string(), output.string() });
        }
        catch ( const sed_agent::AgentError& e )
        {
            error = e.code;
        }
        check(error == sed_agent::kExitUsage, std::string("invalid policy is usage2: ") + policy);
        check(!fs::exists(output), "invalid policy publishes no output");
        check(sed_agent::readFileBytes(f.source) == f.original, "invalid policy preserves source");
    }
    const auto atomicRequest = f.request(R"({"op":"setGravity","value":3},)"
        + stair("+x", 9, R"({"maxStepRise":0})"));
    const auto atomicOutput = f.root / "atomic-refused.sed";
    int atomicError{};
    try
    {
        (void)sed_agent::runCommand("apply", { f.source.string(), atomicRequest.string(), atomicOutput.string() });
    }
    catch ( const sed_agent::AgentError& e )
    {
        atomicError = e.code;
    }
    check(atomicError == sed_agent::kExitUsage && !fs::exists(atomicOutput)
        && sed_agent::readFileBytes(f.source) == f.original,
        "valid operation followed by invalid policy publishes nothing and preserves source");

    const auto request = f.request(stair("+x", 0, R"({"maxStepRise":0.03125})"));
    int error{};
    try
    {
        (void)sed_agent::runCommand("plan", { f.source.string(), request.string() });
    }
    catch ( const sed_agent::AgentError& e )
    {
        error = e.code;
    }
    check(error == sed_agent::kExitRefusal, "structurally invalid stairs retain refusal5 independent of advisory");
}
}

int main()
{
    try
    {
        validContracts();
        invalidContracts();
    }
    catch ( const sed_agent::AgentError& e )
    {
        ++failures;
        std::cerr << "Unexpected agent error " << e.code << ": " << e.message << '\n';
    }
    catch ( const std::exception& e )
    {
        ++failures;
        std::cerr << e.what() << '\n';
    }
    return failures != 0;
}
