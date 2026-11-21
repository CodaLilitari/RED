#include "../agent_workspace.hpp"
#include "sed/assets/episode.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

void write(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    check(static_cast<bool>(file), "fixture writes");
}

Json object(const char* text)
{
    return sed_agent::parseJson(text);
}

Json fixture(const fs::path& root)
{
    sed::assets::EpisodeFile episode{"Contract fixture", 1, {
        {10, 0, 1, "LEVEL", "arrival.jkl", 0, 0, 20, 30},
        {20, 0, 1, "CUT", "relay.smk", 0, 0, 30, -1},
        {30, 0, 2, "LEVEL", "vault.jkl", 0, 0, -1, -1}}};
    write(root / "episode.jk", sed::assets::saveToEpisodeJkText(episode));
    // These files are hashed as authored inputs, not decoded or executed.
    write(root / "arrival.jkl", "synthetic arrival export");
    write(root / "vault.jkl", "synthetic vault export");
    write(root / "relay.smk", "synthetic cutscene input");
    write(root / "shared.cog", "synthetic shared input");
    auto request = object(R"({"schemaVersion":1,"episode":{},"inputs":[],"sequences":[
        {"line":10,"input":"arrival","dependsOn":["shared"],"entry":{},"exits":{"a":{"relay":true},"b":{"relay":true}}},
        {"line":20,"input":"movie","entry":{"relay":true},"exits":{"a":{"relay":true}}},
        {"line":30,"input":"vault","dependsOn":["shared"],"entry":{"relay":true},"exits":{}}]})");
    request.object.at("episode").set("path", Json::makeString((root / "episode.jk").string()));
    for ( const auto& [id, name] : {std::pair{"arrival", "arrival.jkl"}, {"vault", "vault.jkl"},
        {"movie", "relay.smk"}, {"shared", "shared.cog"}} )
    {
        Json file = Json::makeObject();
        file.set("id", Json::makeString(id));
        file.set("path", Json::makeString((root / name).string()));
        request.object.at("inputs").array.push_back(std::move(file));
    }
    return request;
}

Json run(const fs::path& root, const Json& request)
{
    write(root / "contract.json", sed_agent::toJsonString(request));
    return sed_agent::runCommand("campaign-check", {(root / "contract.json").string()});
}

void bindBaseline(const fs::path& root, Json& request, const Json& receipt)
{
    const auto bytes = sed_agent::toJsonString(receipt);
    write(root / "baseline.json", bytes);
    Json descriptor = Json::makeObject();
    descriptor.set("path", Json::makeString((root / "baseline.json").string()));
    descriptor.set("sha256", Json::makeString(sed_agent::sha256Hex(bytes)));
    request.set("baseline", std::move(descriptor));
}

void expectRefusal(const fs::path& root, const Json& request, int code, const char* message)
{
    bool refused = false;
    try
    {
        (void)run(root, request);
    }
    catch ( const sed_agent::AgentError& error )
    {
        refused = error.code == code && sed_agent::isValidJsonUtf8(error.message);
    }
    check(refused, message);
}

void exercise(const fs::path& root)
{
    auto request = fixture(root);
    const auto before = sed_agent::readFileBytes(root / "arrival.jkl");
    const auto first = run(root, request);
    check(first.object.at("declaredStateCompatible").boolean && first.object.at("inputsComplete").boolean
        && !first.object.at("runtimeVerified").boolean && first.object.at("totalTransitions").number == 3,
        "valid declared handoffs have source receipts without a runtime claim");
    bindBaseline(root, request, first);
    const auto current = run(root, request);
    check(current.object.at("sequences").array[0].object.at("freshness").str == "current",
        "the same input and contract match the prior audit");
    request.object.at("inputs").array[3].set("resource", Json::makeString("renamed-shared.cog"));
    const auto renamed = run(root, request);
    const auto& renamedRows = renamed.object.at("sequences").array;
    check(renamedRows[0].object.at("freshness").str == "stale"
        && renamedRows[2].object.at("freshness").str == "stale"
        && renamedRows[1].object.at("freshness").str == "current"
        && sed_agent::toJsonString(renamedRows[0].object.at("changedInputs")) == R"(["shared"])",
        "renaming a logical resource with identical bytes invalidates each dependent audit");
    request.object.at("inputs").array[3].object.erase("resource");
    for ( int change = 0; change < 5; ++change )
    {
        auto inconsistent = first;
        if ( change == 1 )
        {
            inconsistent.set("episodeSha256", Json::makeString(std::string(64, '0')));
        }
        else if ( change == 0 )
        {
            inconsistent.object.at("inputs").array[2].set("sha256", Json::makeString(std::string(64, '0')));
        }
        else if ( change == 2 )
        {
            inconsistent.object.at("sequences").array[0].set("revision", Json{});
        }
        else if ( change == 3 )
        {
            auto& row = inconsistent.object.at("sequences").array[0];
            row.set("revision", Json{});
            row.set("inputStatus", Json::makeString("unavailable"));
            inconsistent.set("inputsComplete", Json::makeBool(false));
        }
        else
        {
            inconsistent.set("sequences", Json::makeArray());
        }
        bindBaseline(root, request, inconsistent);
        expectRefusal(root, request, sed_agent::kExitUsage,
            "a correctly hash-bound baseline with inconsistent row fingerprints refuses");
    }
    bindBaseline(root, request, first);
    write(root / "shared.cog", "changed shared input");
    const auto changed = run(root, request);
    const auto& rows = changed.object.at("sequences").array;
    check(rows[0].object.at("freshness").str == "stale" && rows[2].object.at("freshness").str == "stale"
        && rows[1].object.at("freshness").str == "current"
        && sed_agent::toJsonString(rows[0].object.at("changedInputs")) == R"(["shared"])",
        "shared-input changes identify both affected levels while retaining the unrelated cutscene receipt");
    request.object.at("sequences").array[1].object.at("entry").set("relay", Json::makeBool(false));
    const auto mismatch = run(root, request);
    check(mismatch.object.at("declaredStateCompatible").type == Json::Type::Bool
        && !mismatch.object.at("declaredStateCompatible").boolean
        && mismatch.object.at("sequences").array[0].object.at("rulesChanged").boolean,
        "changing the successor's assumptions also invalidates its producer's handoff receipt");
    request.object.at("sequences").array[1].object.at("entry").set("relay", Json::makeBool(true));
    fs::remove(root / "relay.smk");
    const auto missing = run(root, request);
    check(!missing.object.at("inputsComplete").boolean
        && missing.object.at("sequences").array[1].object.at("freshness").str == "unavailable",
        "a missing cutscene source cannot have current input evidence");
    request.object.at("sequences").array[0].object.at("exits").object.erase("b");
    const auto unknown = run(root, request);
    check(unknown.object.at("declaredStateCompatible").type == Json::Type::Null,
        "an undeclared branch is unknown");
    request.set("offset", Json::makeNumber(999));
    const auto page = run(root, request);
    check(page.object.at("transitions").array.empty() && page.object.at("totalTransitions").number == 3
        && page.object.at("sequences").array.size() == 3,
        "empty transition pages still retain whole-campaign verdicts and freshness evidence");
    check(sed_agent::readFileBytes(root / "arrival.jkl") == before, "campaign check preserves authored inputs");
    for ( const char* field : {"typo", "offset"} )
    {
        auto bad = request;
        bad.set(field, Json::makeBool(true));
        bool refused = false;
        try
        {
            (void)run(root, bad);
        }
        catch ( const sed_agent::AgentError& error )
        {
            refused = error.code == sed_agent::kExitUsage;
        }
        check(refused, "unknown fields and typed pagination mistakes refuse");
    }
    request.object.at("baseline").set("sha256", Json::makeString(std::string(64, '0')));
    bool stale = false;
    try
    {
        (void)run(root, request);
    }
    catch ( const sed_agent::AgentError& error )
    {
        stale = error.code == sed_agent::kExitConflict;
    }
    check(stale, "the prior audit's own bytes are hash-bound before comparison");
}

void admission(const fs::path& root)
{
    auto request = fixture(root);
    write(root / "episode.jk", "\"Oversized declaration\"\nTYPE 1\nSEQ 129\n");
    expectRefusal(root, request, sed_agent::kExitRefusal,
        "an oversized SEQ declaration refuses before reserving or parsing its absent records");
    request = fixture(root);
    const auto bytes = sed_agent::readFileBytes(root / "episode.jk");
    auto invalidFilename = bytes;
    invalidFilename.replace(invalidFilename.find("arrival.jkl"), 11, "caf\xe9.jkl");
    write(root / "episode.jk", invalidFilename);
    expectRefusal(root, request, sed_agent::kExitRefusal,
        "legacy non-UTF-8 episode resource names refuse with a valid JSON error");
    write(root / "episode.jk", "# legacy comment caf\xe9\n" + bytes);
    check(run(root, request).object.at("inputsComplete").boolean,
        "non-UTF-8 legacy comments do not prevent an otherwise representable receipt");
    request.object.at("inputs").array[0].set("resource", Json::makeString(std::string(256, 'x')));
    expectRefusal(root, request, sed_agent::kExitUsage, "logical resource names obey their byte limit");
    request = fixture(root);
    request.object.at("inputs").array[0].set("path", Json::makeString("bad-\xff.jkl"));
    expectRefusal(root, request, sed_agent::kExitUsage, "request paths require valid UTF-8");
}

void baselineAvailability(const fs::path& root)
{
    auto request = fixture(root);
    const sed::assets::EpisodeFile episode{"Decision fixture", 1, {
        {10, 0, 1, "LEVEL", "arrival.jkl", 0, 0, 20, 30},
        {20, 0, 1, "DECIDE", "none", 0, 0, 30, -1},
        {30, 0, 2, "LEVEL", "vault.jkl", 0, 0, -1, -1}}};
    write(root / "episode.jk", sed::assets::saveToEpisodeJkText(episode));
    request.object.at("sequences").array[1].object.erase("input");
    const auto first = run(root, request);
    bindBaseline(root, request, first);
    check(run(root, request).object.at("sequences").array[1].object.at("freshness").str == "current",
        "a declared decision row needs no primary source to have a valid revision");
    request.object.erase("baseline");
    request.object.at("sequences").array.erase(request.object.at("sequences").array.begin() + 1);
    const auto unmodeled = run(root, request);
    check(unmodeled.object.at("sequences").array[1].object.at("revision").type == Json::Type::Null,
        "an unmodeled decision row legitimately has no revision");
    bindBaseline(root, request, unmodeled);
    check(run(root, request).object.at("sequences").array[1].object.at("freshness").str == "unavailable",
        "a genuine unmodeled decision baseline remains admissible");
    request.object.erase("baseline");
    fs::remove(root / "shared.cog");
    const auto missing = run(root, request);
    bindBaseline(root, request, missing);
    check(!run(root, request).object.at("inputsComplete").boolean,
        "a genuine baseline with missing dependency evidence remains admissible");
}

void wireNumbers(const fs::path& root)
{
    auto request = fixture(root);
    request.object.at("sequences").array[0].object.at("exits").object.at("a")
        .set("score", Json::makeNumber(4503599627370496.0));
    request.object.at("sequences").array[1].object.at("entry")
        .set("score", Json::makeString("WIRE_NUMBER"));
    const auto base = sed_agent::toJsonString(request);
    const auto runWire = [&](const std::string& token)
    {
        auto wire = base;
        wire.replace(wire.find("\"WIRE_NUMBER\""), 13, token);
        write(root / "wire.json", wire);
        return sed_agent::runCommand("campaign-check", {(root / "wire.json").string()});
    };
    const auto integer = runWire("4503599627370496");
    check(integer.object.at("declaredStateCompatible").boolean,
        "a large representable integer matches its exact guarantee");
    for ( const char* token : {"4503599627370496.0", "450359962737049600e-2"} )
    {
        check(runWire(token).object.at("contractSha256").str == integer.object.at("contractSha256").str,
            "mathematically identical integer spellings retain the same campaign identity");
    }
    for ( const char* token : {"4503599627370496.25", "45035996273704960.1e-1", "1.0000000000000001"} )
    {
        bool refused = false;
        try
        {
            (void)runWire(token);
        }
        catch ( const sed_agent::AgentError& error )
        {
            refused = error.code == sed_agent::kExitUsage;
        }
        check(refused, "a fractional wire fact cannot round into an integer handoff");
    }
}
}

int main()
{
    const auto root = fs::temp_directory_path() / ("red-campaign-check-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        fs::create_directories(root);
        exercise(root);
        admission(root);
        baselineAvailability(root);
        wireNumbers(root);
        fs::remove_all(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        std::cerr << error.message << '\n';
        fs::remove_all(root);
        return 1;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
