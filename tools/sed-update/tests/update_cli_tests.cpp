// End-to-end tests driving the real sed-update binary with local file
// feeds (no network). Each ctest entry runs one named scenario.
#include "sed/update/updater.hpp"
#include "agent_json.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SED_UPDATE_EXE
#error "SED_UPDATE_EXE must name the built sed-update binary"
#endif

namespace fs = std::filesystem;
namespace
{
int failures = 0;
void check(bool ok, const std::string& what)
{
    if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
    else std::cout << "ok: " << what << "\n";
}
bool contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

void checkJsonString(const std::string& output, const char* key,
                     const std::string& expected, const std::string& what)
{
    try
    {
        const auto value = sed_agent::parseJson(output);
        const auto* field = value.find(key);
        check(field && field->type == sed_agent::Json::Type::String && field->str == expected, what);
    }
    catch (const std::exception& error)
    {
        check(false, what + ": invalid JSON: " + error.what() + "; output=" + output);
    }
}

std::string quote(const std::string& s)
{
    if (s.find(' ') == std::string::npos) return s;
    return "\"" + s + "\"";
}

int runTool(const std::vector<std::string>& args, std::string& output, const fs::path& workDir)
{
    const fs::path outFile = workDir / "stdout.txt";
    std::string cmd = quote(SED_UPDATE_EXE);
    for (const auto& a : args) cmd += " " + quote(a);
    cmd += " > " + quote(outFile.string()) + " 2>&1";
    const int rc = std::system(cmd.c_str());
    std::ifstream in(outFile, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    output = ss.str();
    return rc;
}

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}
std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Fixture
{
    fs::path root;
    fs::path feed;
    fs::path install;
};

Fixture makeFixture(const char* name, const std::string& version, bool corrupt)
{
    Fixture fx;
    fx.root = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(fx.root, ec);
    const fs::path package = fx.root / "pkg";
    writeText(package / "sed.exe", "new editor binary");
    writeText(package / "data" / "needed.dat", "runtime data");
    const std::string hash = corrupt ? std::string(64, '0') : sed::update::sha256Hex("new editor binary");
    writeText(package / "RELEASE_MANIFEST.json",
        "{\n  \"format\": 1,\n  \"version\": \"" + version + "\",\n  \"files\": [\n"
        "    {\"path\":\"sed.exe\",\"bytes\":17,\"sha256\":\"" + hash + "\"},\n"
        "    {\"path\":\"data/needed.dat\",\"bytes\":12,\"sha256\":\""
        + sed::update::sha256Hex("runtime data") + "\"}\n  ]\n}\n");
    fx.feed = fx.root / "feed.json";
    writeText(fx.feed, "{\"format\":1,\"releases\":[{\"version\":\"" + version
        + "\",\"channel\":\"stable\",\"package\":\"pkg\"}]}\n");
    fx.install = fx.root / "SED";
    writeText(fx.install / "sed.exe", "old editor binary");
    return fx;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string scenario = argc > 1 ? argv[1] : "";
    std::string out;
    if (scenario == "equal" || scenario == "downgrade")
    {
        const std::string version = scenario == "equal" ? "0.4.0" : "0.3.0";
        const Fixture fx = makeFixture(scenario == "equal" ? "sed-update-cli-equal" : "sed-update-cli-down", version, false);
        const int rc = runTool({ "check", "--feed", fx.feed.string(), "--current", "0.4.0" }, out, fx.root);
        check(rc != 0, scenario + ": refused with a nonzero exit");
        check(contains(out, scenario == "equal" ? "equal-version" : "downgrade-refused"), scenario + ": stable refusal code");
        const int stageRc = runTool({ "stage", "--feed", fx.feed.string(), "--install", fx.install.string(), "--current", "0.4.0" }, out, fx.root);
        check(stageRc != 0 && readText(fx.install / "sed.exe") == "old editor binary", scenario + ": stage refusal leaves the old install intact");
        fs::remove_all(fx.root);
    }
    else if (scenario == "corrupt-hash")
    {
        const Fixture fx = makeFixture("sed-update-cli-corrupt", "0.5.0", true);
        const int rc = runTool({ "stage", "--feed", fx.feed.string(), "--install", fx.install.string(), "--current", "0.4.0" }, out, fx.root);
        check(rc != 0 && contains(out, "hash-mismatch"), "corrupt-hash: refused");
        check(!fs::exists(sed::update::stagingDirectoryFor(fx.install, "0.5.0")), "corrupt-hash: staging cleaned up");
        check(readText(fx.install / "sed.exe") == "old editor binary", "corrupt-hash: old install intact");
        fs::remove_all(fx.root);
    }
    else if (scenario == "interrupted-staging")
    {
        const Fixture fx = makeFixture("sed-update-cli-interrupted", "0.5.0", false);
        writeText(sed::update::stagingDirectoryFor(fx.install, "0.5.0") / "sed.exe", "partial copy");
        const int rc = runTool({ "stage", "--feed", fx.feed.string(), "--install", fx.install.string(), "--current", "0.4.0" }, out, fx.root);
        check(rc != 0 && contains(out, "interrupted-staging"), "interrupted-staging: refused");
        check(readText(fx.install / "sed.exe") == "old editor binary", "interrupted-staging: old install intact");
        fs::remove_all(fx.root);
    }
    else if (scenario == "stage-activate")
    {
        const Fixture fx = makeFixture("sed-update-cli-happy", "0.5.0", false);
        int rc = runTool({ "check", "--feed", fx.feed.string(), "--current", "0.4.0" }, out, fx.root);
        check(rc == 0 && contains(out, "update-available"), "stage-activate: check offers the update");
        rc = runTool({ "stage", "--feed", fx.feed.string(), "--install", fx.install.string(), "--current", "0.4.0" }, out, fx.root);
        check(rc == 0 && contains(out, "\"staged\""), "stage-activate: stage succeeds");
        const fs::path staging = sed::update::stagingDirectoryFor(fx.install, "0.5.0");
        check(fs::is_directory(staging) && readText(fx.install / "sed.exe") == "old editor binary", "stage-activate: staged beside, old intact");
        rc = runTool({ "verify", "--dir", staging.string(), "--staged" }, out, fx.root);
        check(rc == 0, "stage-activate: staged receipts verify");
        rc = runTool({ "activate", "--staging", staging.string() }, out, fx.root);
        check(rc == 0 && contains(out, "activated"), "stage-activate: activation is a separate step");
        const fs::path activated = sed::update::activatedDirectoryFor(fx.install, "0.5.0");
        check(readText(activated / "sed.exe") == "new editor binary", "stage-activate: activated tree is the new install");
        check(readText(fx.install / "sed.exe") == "old editor binary", "stage-activate: old install remains runnable");
        rc = runTool({ "verify", "--dir", activated.string(), "--staged" }, out, fx.root);
        check(rc == 0, "stage-activate: activated receipts recheck");
        fs::remove_all(fx.root);
    }
    else if (scenario == "json-output")
    {
        const Fixture fx = makeFixture("sed-update-cli-json-output", "0.5.0", false);
        int rc = runTool({ "stage", "--feed", fx.feed.string(), "--install", fx.install.string(), "--current", "0.4.0" }, out, fx.root);
        check(rc == 0, "json-output: stage succeeds");
        const fs::path staging(fx.install.string() + ".staging-0.5.0");
        checkJsonString(out, "staging", staging.string(),
                        "json-output: staged path survives JSON decoding exactly");
        checkJsonString(out, "version", "0.5.0", "json-output: version remains a JSON string");
        rc = runTool({ "activate", "--staging", staging.string() }, out, fx.root);
        check(rc == 0, "json-output: activate succeeds");
        const fs::path activated(fx.install.string() + "-0.5.0");
        checkJsonString(out, "dir", activated.string(),
                        "json-output: activated path survives JSON decoding exactly");
        check(readText(activated / "sed.exe") == "new editor binary"
              && readText(fx.install / "sed.exe") == "old editor binary",
              "json-output: response serialization preserves activation and the old install");

        // The manifest supplies this missing name; it is never passed through a shell
        // or created on disk. Its JSON escapes must survive the diagnostic round trip.
        writeText(fx.root / "bad-package" / "RELEASE_MANIFEST.json",
            "{\"format\":1,\"version\":\"0.5.0\",\"files\":[{\"path\":"
            "\"missing\\\"quoted\\\\branch\\tline\\nreturn\\rback\\bform\\fend\\u0001\","
            "\"bytes\":1,\"sha256\":\"" + std::string(64, '0') + "\"}]}");
        rc = runTool({ "verify", "--dir", (fx.root / "bad-package").string() }, out, fx.root);
        check(rc != 0, "json-output: missing packaged file is refused");
        checkJsonString(out, "code", "missing-file", "json-output: diagnostic keeps its stable code");
        checkJsonString(out, "error",
                        "packaged file is missing: missing\"quoted\\branch\tline\nreturn\rback\bform\fend\x01",
                        "json-output: diagnostic quotes, backslashes and controls survive JSON decoding exactly");
        fs::remove_all(fx.root);
    }
    else
    {
        std::cerr << "unknown scenario: " << scenario << "\n";
        return 2;
    }
    return failures == 0 ? 0 : 1;
}
