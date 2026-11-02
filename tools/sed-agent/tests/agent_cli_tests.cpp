// End-to-end tests driving the real sed-agent binary. Each ctest entry
// runs one named scenario (allocation-test precedent: own executable,
// WORKING_DIRECTORY repo root, TIMEOUT set by CMake).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SED_AGENT_EXE
#error "SED_AGENT_EXE must name the built sed-agent binary"
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

std::string quote(const std::string& s)
{
    // std::system routes through cmd /c, which mangles a command that
    // starts with a quote; our paths contain no spaces, so quote only
    // when actually needed.
    if (s.find(' ') == std::string::npos) return s;
    return "\"" + s + "\"";
}

int runAgent(const std::vector<std::string>& args, const std::string& stdinData,
             std::string& output, const fs::path& workDir)
{
    const fs::path outFile = workDir / "stdout.txt";
    std::string cmd;
#ifdef _WIN32
    cmd = quote(SED_AGENT_EXE);
    for (const auto& a : args) cmd += " " + quote(a);
    if (!stdinData.empty())
    {
        const fs::path inFile = workDir / "stdin.txt";
        std::ofstream(inFile, std::ios::binary) << stdinData;
        cmd += " < " + quote(inFile.string());
    }
    cmd += " > " + quote(outFile.string()) + " 2>&1";
#else
    cmd = quote(SED_AGENT_EXE);
    for (const auto& a : args) cmd += " " + quote(a);
    cmd += " > " + quote(outFile.string()) + " 2>&1";
#endif
    const int rc = std::system(cmd.c_str());
    std::ifstream in(outFile, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    output = ss.str();
    return rc;
}

void writeFile(const fs::path& p, const std::string& s)
{
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}
std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

bool contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

std::string extractSha(const std::string& json, const std::string& key)
{
    const std::string pat = "\"" + key + "\":\"";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) return {};
    return json.substr(pos + pat.size(), 64);
}

// Minimal synthetic JKL (original fixture in the shape of the core
// sed_vectors input; no retail assets).
const char* kJkl = R"JKL(SECTION: HEADER
VERSION 1
WORLD GRAVITY 4
END

SECTION: MATERIALS
WORLD MATERIALS 1
0: MAT: agent.mat
END

SECTION: GEORESOURCE
WORLD COLORMAPS 1
0: agent.cmp
WORLD VERTICES 3
0: 0 0 0
1: 1 0 0
2: 0 1 0
WORLD TEXTURE VERTICES 3
0: 0 0
1: 1 0
2: 0 1
WORLD ADJOINS 0
WORLD SURFACES 1
0: 0 0x1 0x0 3 0 1 -1 0 3 0,0 1,1 2,2 0 0 1
0: 0 0 1
END

SECTION: SECTORS
WORLD SECTORS 1
SECTOR 0
FLAGS 0
AMBIENT LIGHT 0.25
EXTRA LIGHT 0
COLORMAP 0
SURFACES 0 1
END

SECTION: TEMPLATES
WORLD TEMPLATES 1
agent_template none type=actor model3d=agent.3do
END

SECTION: THINGS
WORLD THINGS 1
0: agent_template agent_actor 1 2 3 0 0 0 0 health=100
END
)JKL";

// Two closed box rooms sharing the wall x=2 (original synthetic fixture;
// room A spans [0,2]^3, room B spans x in [2,4], y/z in [0,2]).
const char* kOutpostJkl = R"JKL(SECTION: HEADER
VERSION 1
WORLD GRAVITY 4
END

SECTION: MATERIALS
WORLD MATERIALS 1
0: MAT: agent.mat
END

SECTION: GEORESOURCE
WORLD COLORMAPS 1
0: agent.cmp
WORLD VERTICES 16
0: 0 0 0
1: 2 0 0
2: 2 2 0
3: 0 2 0
4: 0 0 2
5: 2 0 2
6: 2 2 2
7: 0 2 2
8: 2 0 0
9: 4 0 0
10: 4 2 0
11: 2 2 0
12: 2 0 2
13: 4 0 2
14: 4 2 2
15: 2 2 2
WORLD TEXTURE VERTICES 16
0: 0 0
1: 0 0
2: 0 0
3: 0 0
4: 0 0
5: 0 0
6: 0 0
7: 0 0
8: 0 0
9: 0 0
10: 0 0
11: 0 0
12: 0 0
13: 0 0
14: 0 0
15: 0 0
WORLD ADJOINS 0
WORLD SURFACES 12
0: 0 0x0 0x0 3 0 1 -1 0 4 1,1 5,5 6,6 2,2 0 0 0 0
1: 0 0x0 0x0 3 0 1 -1 0 4 0,0 3,3 7,7 4,4 0 0 0 0
2: 0 0x0 0x0 3 0 1 -1 0 4 3,3 2,2 6,6 7,7 0 0 0 0
3: 0 0x0 0x0 3 0 1 -1 0 4 0,0 4,4 5,5 1,1 0 0 0 0
4: 0 0x0 0x0 3 0 1 -1 0 4 4,4 7,7 6,6 5,5 0 0 0 0
5: 0 0x0 0x0 3 0 1 -1 0 4 0,0 1,1 2,2 3,3 0 0 0 0
6: 0 0x0 0x0 3 0 1 -1 0 4 9,9 13,13 14,14 10,10 0 0 0 0
7: 0 0x0 0x0 3 0 1 -1 0 4 8,8 11,11 15,15 12,12 0 0 0 0
8: 0 0x0 0x0 3 0 1 -1 0 4 11,11 10,10 14,14 15,15 0 0 0 0
9: 0 0x0 0x0 3 0 1 -1 0 4 8,8 12,12 13,13 9,9 0 0 0 0
10: 0 0x0 0x0 3 0 1 -1 0 4 12,12 15,15 14,14 13,13 0 0 0 0
11: 0 0x0 0x0 3 0 1 -1 0 4 8,8 9,9 10,10 11,11 0 0 0 0
END

SECTION: SECTORS
WORLD SECTORS 2
SECTOR 0
FLAGS 0
AMBIENT LIGHT 0.25
EXTRA LIGHT 0
COLORMAP 0
SURFACES 0 6
SECTOR 1
FLAGS 0
AMBIENT LIGHT 0.25
EXTRA LIGHT 0
COLORMAP 0
SURFACES 6 6
END

SECTION: TEMPLATES
WORLD TEMPLATES 1
agent_template none type=actor model3d=agent.3do
END

SECTION: THINGS
WORLD THINGS 1
0: agent_template agent_actor 1 1 1 0 0 0 0
END
)JKL";

std::string extractToken(const std::string& json)
{
    const std::string pat = "\"token\":\"";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) return {};
    const auto end = json.find('"', pos + pat.size());
    if (end == std::string::npos) return {};
    return json.substr(pos + pat.size(), end - pos - pat.size());
}

std::vector<std::pair<std::string, std::string>> snapshotDir(const fs::path& dir)
{
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file())
            entries.emplace_back(e.path().lexically_relative(dir).string(), readFile(e.path()));
    std::sort(entries.begin(), entries.end());
    return entries;
}

void writeU32LE(std::ofstream& out, std::uint32_t v)
{
    const char b[4] = { static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
                        static_cast<char>((v >> 16) & 0xff), static_cast<char>((v >> 24) & 0xff) };
    out.write(b, 4);
}

// Synthetic GOB v2 archive (same layout the core gob_tests fixture writes).
bool writeGobV2(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("GOB ", 4);
    writeU32LE(out, 0x14u);
    writeU32LE(out, 0x0Cu);
    writeU32LE(out, static_cast<std::uint32_t>(entries.size()));
    std::uint32_t dataOffset = 16u + static_cast<std::uint32_t>(entries.size()) * 136u;
    for (const auto& [name, data] : entries)
    {
        writeU32LE(out, dataOffset);
        writeU32LE(out, static_cast<std::uint32_t>(data.size()));
        std::array<char, 128> nameBytes{};
        std::copy(name.begin(), name.end(), nameBytes.begin());
        out.write(nameBytes.data(), static_cast<std::streamsize>(nameBytes.size()));
        dataOffset += static_cast<std::uint32_t>(data.size());
    }
    for (const auto& [name, data] : entries) out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

std::string jsonPath(const fs::path& p)
{
    std::string s = p.string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

const char* kFixtureCog =
    "symbols\nmessage startup\nthing gate\nsector room\nend\ncode\nstartup:\n return;\nend\n";
const char* kBrokenCog = "symbols\nthing gate\n";

std::string cogLevelText()
{
    std::string text = kJkl;
    text += "\nSECTION: COGS\nWORLD COGS 1\n0: fixture.cog 0 0\nEND\n";
    return text;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: agent_cli_tests <scenario>\n"; return 2; }
    const std::string scenario = argv[1];
    const fs::path root = fs::temp_directory_path() / ("sed-agent-test-" + scenario);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path level = root / "level.jkl";
    writeFile(level, kJkl);
    std::string out;

    if (scenario == "exit-codes")
    {
        check(runAgent({ "help" }, "", out, root) == 0 && contains(out, "\"ok\":true"), "help -> 0");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "does not claim to implement MCP"),
              "schema -> 0, no MCP claim");
        check(runAgent({ "inspect", level.string() }, "", out, root) == 0 && contains(out, "\"sectors\":1"),
              "inspect -> 0 with counts");
        check(runAgent({ "bogus" }, "", out, root) == 2, "unknown command -> 2 usage");
        check(runAgent({ "inspect" }, "", out, root) == 2, "missing arg -> 2 usage");
        check(runAgent({ "inspect", (root / "missing.jkl").string() }, "", out, root) == 3,
              "missing input file -> 3 input error");
        writeFile(root / "bad.jkl", "not a level\n");
        check(runAgent({ "inspect", (root / "bad.jkl").string() }, "", out, root) == 3,
              "unparseable input -> 3 input error");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":9}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""), "plan -> 0 with outputSha256");
        check(runAgent({ "apply", level.string(), patch.string(), (root / "out.jkl").string() }, "", out, root) == 0
              && fs::exists(root / "out.jkl"), "apply -> 0 creates new file");
    }
    else if (scenario == "stale-hash")
    {
        std::string inspectOut;
        runAgent({ "inspect", level.string() }, "", inspectOut, root);
        const std::string sha = extractSha(inspectOut, "inputSha256");
        check(sha.size() == 64, "inspect returns inputSha256");
        const fs::path patch = root / "patch.json";
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
              + "\",\"operations\":[{\"op\":\"setGravity\",\"value\":9}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "stale inputSha256 -> 4");
        writeFile(patch, "{\"schemaVersion\":1,\"sourceRevision\":\"" + std::string(64, 'f')
              + "\",\"operations\":[{\"op\":\"setGravity\",\"value\":9}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "stale sourceRevision -> 4");
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + sha
              + "\",\"operations\":[{\"op\":\"setGravity\",\"value\":9}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", inspectOut, root) == 0,
              "matching hash plans ok (numeric selectors bound to that input)");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setSectorAmbient","sector":99,"value":1}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 3,
              "out-of-range numeric selector -> 3 input error");
        // Wrong expected apply hash -> 4, and nothing is written.
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":9}]})");
        const fs::path dest = root / "out.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), std::string(64, '0') },
                       "", out, root) == 4 && !fs::exists(dest), "expected-hash mismatch -> 4, no file");
    }
    else if (scenario == "existing-output")
    {
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":9}]})");
        const fs::path dest = root / "out.jkl";
        writeFile(dest, "precious\n");
        check(runAgent({ "apply", level.string(), patch.string(), dest.string() }, "", out, root) == 5
              && readFile(dest) == "precious\n", "existing output refused (5), bytes untouched");
        check(runAgent({ "apply", level.string(), patch.string(), level.string() }, "", out, root) == 5,
              "output == input refused (5)");
        check(runAgent({ "apply", level.string(), patch.string(), (root / "nope" / "out.jkl").string() },
                       "", out, root) == 5, "missing output directory refused (5)");
    }
    else if (scenario == "request-cap")
    {
        std::string big = "{\"schemaVersion\":1,\"operations\":[{\"op\":\"setGravity\",\"value\":9}],\"pad\":\"";
        big.append(1024 * 1024, 'x');
        big += "\"}";
        const fs::path patch = root / "big.json";
        writeFile(patch, big);
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
              "request over 1 MiB -> 2 usage");
        check(runAgent({ "plan", level.string(), "-" }, big, out, root) == 2,
              "stdin request over 1 MiB -> 2 usage");
    }
    else if (scenario == "plan-writes-nothing")
    {
        const auto before = snapshotDir(root);
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":7}]})");
        const auto withPatch = snapshotDir(root);
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""), "plan succeeds");
        const auto after = snapshotDir(root);
        // patch.json itself was created by the test; compare level dir state
        // excluding it: plan must add/remove/change nothing else.
        auto strip = [](std::vector<std::pair<std::string, std::string>> v) {
            std::erase_if(v, [](const auto& e) { return e.first == "patch.json"
                        || e.first == "stdout.txt" || e.first == "stdin.txt"; });
            return v;
        };
        check(strip(after) == strip(withPatch), "directory listing+contents identical after plan");
        check(withPatch.size() == before.size() + 1 && strip(withPatch) == strip(before),
              "only the patch file was added by the test");
    }
    else if (scenario == "query")
    {
        const fs::path q = root / "q.json";
        writeFile(q, R"({"schemaVersion":1,"collection":"summary"})");
        check(runAgent({ "query", level.string(), q.string() }, "", out, root) == 0
              && contains(out, "\"inputSha256\":\""), "query summary -> 0");
        writeFile(q, R"({"schemaVersion":1,"collection":"sectors"})");
        check(runAgent({ "query", level.string(), q.string() }, "", out, root) == 0
              && contains(out, "\"num\":0"), "query sectors -> 0");
        writeFile(q, R"({"schemaVersion":1,"collection":"things","search":"AGENT_ACTOR"})");
        check(runAgent({ "query", level.string(), q.string() }, "", out, root) == 0
              && contains(out, "agent_actor"), "query search is case-insensitive");
        check(runAgent({ "query", level.string(), "-" },
                       R"({"schemaVersion":1,"collection":"summary"})", out, root) == 0,
              "query via stdin -> 0");
    }
    else if (scenario == "select")
    {
        const fs::path outpost = root / "outpost.jkl";
        writeFile(outpost, kOutpostJkl);
        const fs::path req = root / "select.json";
        auto select = [&](const std::string& body) {
            writeFile(req, body);
            return runAgent({ "select", outpost.string(), req.string() }, "", out, root);
        };

        check(select(R"({"schemaVersion":1,"target":"sector","point":[1,1,1]})") == 0
              && contains(out, "\"match\":\"unique\"") && contains(out, "\"sector\":0"),
              "select sector interior -> unique room A with token");
        const std::string token = extractToken(out);
        check(!token.empty(), "unique select returns a token");

        check(select(R"({"schemaVersion":1,"target":"sector","point":[2,1,1]})") == 0
              && contains(out, "\"match\":\"ambiguous\"") && contains(out, "\"token\":null"),
              "select on the shared wall -> ambiguous, null token (success)");
        check(select(R"({"schemaVersion":1,"target":"sector","point":[9,9,9]})") == 0
              && contains(out, "\"match\":\"none\"") && contains(out, "\"token\":null"),
              "select outside every room -> none, null token (success)");
        check(select(R"({"schemaVersion":1,"target":"surface","point":[2,1,1],"direction":[1,0,0]})") == 0
              && contains(out, "\"match\":\"unique\"") && contains(out, "\"sector\":0"),
              "select surface facing +x -> room A face");
        check(select(R"({"schemaVersion":1,"target":"surface","point":[2,1,1],"direction":[-1,0,0]})") == 0
              && contains(out, "\"match\":\"unique\"") && contains(out, "\"sector\":1"),
              "select surface facing -x -> room B face");
        check(select(R"({"schemaVersion":1,"target":"thing","point":[1,1,1]})") == 0
              && contains(out, "\"match\":\"unique\"") && contains(out, "\"thing\":0"),
              "select thing at its position -> unique");

        writeFile(req, "{\"schemaVersion\":1,\"resolve\":\"" + token + "\"}");
        check(runAgent({ "select", outpost.string(), req.string() }, "", out, root) == 0
              && contains(out, "\"resolved\":true") && contains(out, "\"sector\":0"),
              "fresh token resolves against the same document");

        // Same token, changed document (one vertex moved): stale conflict.
        std::string changed = kOutpostJkl;
        const std::string from = "6: 2 2 2";
        const auto at = changed.find(from);
        check(at != std::string::npos, "outpost fixture carries the vertex to move");
        if (at != std::string::npos) changed.replace(at, from.size(), "6: 2 2 3");
        const fs::path changedPath = root / "outpost-changed.jkl";
        writeFile(changedPath, changed);
        check(runAgent({ "select", changedPath.string(), req.string() }, "", out, root) == 4
              && contains(out, "staleSelectionToken"),
              "token replayed against changed geometry -> exit 4 stale code");

        writeFile(req, R"({"schemaVersion":1,"resolve":"bogus"})");
        check(runAgent({ "select", outpost.string(), req.string() }, "", out, root) == 2,
              "malformed token -> exit 2 usage");
    }
    else if (scenario == "validate-new-convert")
    {
        check(runAgent({ "validate", level.string() }, "", out, root) == 0
              && contains(out, "\"valid\":true"), "validate -> 0 valid");
        check(runAgent({ "validate", (root / "missing.jkl").string() }, "", out, root) == 3,
              "validate missing input -> 3");
        const fs::path created = root / "created.jkl";
        check(runAgent({ "new", created.string(), "mots" }, "", out, root) == 0 && fs::exists(created),
              "new mots -> 0 creates scaffold");
        check(runAgent({ "inspect", created.string() }, "", out, root) == 0 && contains(out, "\"sectors\":1"),
              "new scaffold inspects with one sector");
        check(runAgent({ "new", created.string(), "mots" }, "", out, root) == 5,
              "new over existing output -> 5 refusal");
        const fs::path req = root / "new.json";
        writeFile(req, R"({"schemaVersion":1,"kind":"jk"})");
        check(runAgent({ "new", (root / "created2.jkl").string(), req.string() }, "", out, root) == 0,
              "new via JSON request (jk) -> 0");
        check(runAgent({ "new", (root / "bad.txt").string() }, "", out, root) == 2,
              "new with bad extension -> 2 usage");
        const fs::path sedOut = root / "converted.sed";
        check(runAgent({ "convert", level.string(), sedOut.string() }, "", out, root) == 0 && fs::exists(sedOut),
              "convert jkl->sed -> 0");
        check(runAgent({ "inspect", sedOut.string() }, "", out, root) == 0 && contains(out, "\"sectors\":1"),
              "converted sed inspects");
        check(runAgent({ "convert", level.string(), sedOut.string() }, "", out, root) == 5,
              "convert over existing output -> 5 refusal");
        check(runAgent({ "convert", level.string(), level.string() }, "", out, root) == 5,
              "convert output == input -> 5 refusal");
        const fs::path hashOut = root / "hashcheck.jkl";
        check(runAgent({ "convert", level.string(), hashOut.string(), std::string(64, '0') }, "", out, root) == 4
              && !fs::exists(hashOut), "convert wrong expected hash -> 4, no file");
        check(runAgent({ "convert", level.string(), (root / "nope" / "x.jkl").string() }, "", out, root) == 5,
              "convert missing output dir -> 5 refusal");
    }
    else if (scenario == "archive-extract-launch")
    {
        const fs::path gob = root / "resources.gob";
        check(writeGobV2(gob, { { "test.jkl", kJkl }, { "cog\\fixture.cog", kFixtureCog } }),
              "gob fixture written");
        check(runAgent({ "archive", gob.string() }, "", out, root) == 0
              && contains(out, "test.jkl") && contains(out, "\"archiveSha256\":\""),
              "archive lists entries with archive hash");
        check(runAgent({ "archive", (root / "missing.gob").string() }, "", out, root) == 3,
              "archive missing file -> 3");
        check(runAgent({ "archive", level.string() }, "", out, root) == 2
              && contains(out, "invalid_request")
              && contains(out, "Resource archive is not a readable GOB/GOO archive")
              && contains(out, "level.jkl"),
              "archive non-gob input -> 2 invalid_request naming file");
        const fs::path extracted = root / "extracted.jkl";
        check(runAgent({ "extract", gob.string(), "test.jkl", extracted.string() }, "", out, root) == 0
              && readFile(extracted) == std::string(kJkl), "extract recovers entry byte-for-byte");
        check(runAgent({ "extract", gob.string(), "test.jkl", extracted.string() }, "", out, root) == 5,
              "extract over existing output -> 5 refusal");
        check(runAgent({ "extract", gob.string(), "nope.jkl", (root / "x.jkl").string() }, "", out, root) == 3,
              "extract missing entry -> 3");
        const std::string launchJson = "{\"schemaVersion\":1,\"game\":\"MysteriesOfTheSith\",\"gameDirectory\":\""
            + jsonPath(root) + "\",\"projectDirectory\":\"" + jsonPath(root)
            + "\",\"levelFileName\":\"level.jkl\",\"executable\":\"game.exe\",\"engine\":\"OpenJKDF2\"}";
        check(runAgent({ "launch-plan", "-" }, launchJson, out, root) == 0
              && contains(out, "\"launches\":false") && contains(out, "game.exe"),
              "launch-plan via stdin -> 0, launches nothing");
        check(runAgent({ "launch-plan", "-" },
                       R"({"schemaVersion":1,"game":"Quake","gameDirectory":"x","projectDirectory":"y","levelFileName":"a.jkl","executable":"e.exe"})",
                       out, root) == 2, "launch-plan bad game -> 2 usage");
        check(runAgent({ "launch-plan", "-" }, R"({"schemaVersion":1,"game":"JediKnight"})", out, root) == 2,
              "launch-plan missing fields -> 2 usage");
    }
    else if (scenario == "light-rgb")
    {
        std::string lightLevel = kJkl;
        lightLevel += "\nSECTION: LIGHTS\nEDITOR LIGHTS 1\n0: 0x1 0 1 2 3 2 4 0.25 0.5 0.75\nEND\n";
        writeFile(level, lightLevel);
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setLightRgb","light":0,"rgbIntensity":0.5}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""), "setLightRgb plan -> 0");
        check(!fs::exists(root / "out.jkl"), "setLightRgb plan writes nothing");
        check(runAgent({ "apply", level.string(), patch.string(), (root / "out.jkl").string() }, "", out, root) == 0
              && fs::exists(root / "out.jkl"), "setLightRgb apply -> 0");
        check(contains(readFile(root / "out.jkl"), "SED_SOURCE_LIGHTS"),
              "applied output carries SED_SOURCE_LIGHTS");
        check(contains(readFile(root / "out.jkl"), "0.5"),
              "applied output carries the rgb value");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setLightRgb","light":9,"rgbIntensity":0.5}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 3,
              "out-of-range light selector -> 3");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setLightRgb","light":0,"rgbIntensity":null}]})");
        check(runAgent({ "apply", level.string(), patch.string(), (root / "out2.jkl").string() }, "", out, root) == 0,
              "null clears to inherited");
    }
    else if (scenario == "workbench")
    {
        const fs::path req = root / "req.json";
        writeFile(req, R"({"schemaVersion":1,"collection":"things"})");
        check(runAgent({ "workbench-query", level.string(), req.string() }, "", out, root) == 0
              && contains(out, "\"total\":1") && contains(out, "\"indices\":[0]"),
              "workbench-query things lists the one thing");
        writeFile(req, R"({"schemaVersion":1,"collection":"things","filter":{}})");
        check(runAgent({ "workbench-query", level.string(), req.string() }, "", out, root) == 5,
              "empty filter refuses (exit 5, core code)");
        writeFile(req, R"({"schemaVersion":1,"collection":"sectors","sector":0})");
        check(runAgent({ "workbench-query", level.string(), req.string() }, "", out, root) == 5,
              "sector on non-surfaces collection refuses");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"batchUvTransform","surfaces":[[0,0]],"pivot":[0,0],"scale":[2,2]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0,
              "batchUvTransform plan -> 0");
        check(runAgent({ "apply", level.string(), patch.string(), (root / "uv.jkl").string() }, "", out, root) == 0
              && fs::exists(root / "uv.jkl"), "batchUvTransform apply -> 0");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"batchUvTransform","surfaces":[[0,0]],"pivot":[0,0],"scale":[0,1]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5,
              "zero scale refuses with core code (exit 5)");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"placeLightArray","layout":{"kind":"linear","origin":[0,0,1],"step":[1,0,0],"count":3}}]})");
        check(runAgent({ "apply", level.string(), patch.string(), (root / "lights.jkl").string() }, "", out, root) == 0,
              "placeLightArray apply -> 0");
        check(runAgent({ "inspect", (root / "lights.jkl").string() }, "", out, root) == 0
              && contains(out, "\"lights\":3"), "placed lights reopen (3)");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"placeLightArray","layout":{"kind":"linear","origin":[0,0,1],"step":[1,0,0],"count":300}}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5,
              "array count 300 refuses, never truncates");
        writeFile(req, R"({"schemaVersion":1,"models":[{"thing":0,"name":"f04_diamond.3do","path":"src/core/tests/assets/fixtures/f04_diamond.3do"}]})");
        check(runAgent({ "arch-validate", level.string(), req.string() }, "", out, root) == 0
              && contains(out, "\"usable\":true"), "arch-validate reports (no ARCH things bound)");
        writeFile(req, R"({"schemaVersion":1,"models":[{"thing":0,"name":"f04_diamond.3do","path":"src/core/tests/assets/fixtures/f04_diamond.3do","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}]})");
        check(runAgent({ "arch-validate", level.string(), req.string() }, "", out, root) == 4,
              "stale model sha256 -> 4");
    }
    else if (scenario == "snapshot-refusal")
    {
        const fs::path req = root / "snap.json";
        writeFile(req, R"({"schemaVersion":1,"width":64,"height":64})");
        const fs::path png = root / "snap.png";
        check(runAgent({ "snapshot", level.string(), req.string(), png.string() }, "", out, root) == 5
              && contains(out, "snapshotRequiresRenderer") && !fs::exists(png),
              "snapshot refused snapshotRequiresRenderer (5), no fake PNG");
        check(runAgent({ "snapshot", level.string() }, "", out, root) == 2,
              "snapshot missing args -> 2 usage");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "snapshotRequiresRenderer"),
              "schema documents the snapshot refusal");
    }
    else if (scenario == "cog-bindings")
    {
        const fs::path cogLevel = root / "coglevel.jkl";
        writeFile(cogLevel, cogLevelText());
        const fs::path gob = root / "cogs.gob";
        check(writeGobV2(gob, { { "cog\\fixture.cog", kFixtureCog },
                                { "cog\\newdoor.cog", kFixtureCog },
                                { "cog\\broken.cog", kBrokenCog } }), "cog gob fixture written");
        std::string archiveOut;
        runAgent({ "archive", gob.string() }, "", archiveOut, root);
        const std::string archiveSha = extractSha(archiveOut, "archiveSha256");
        check(archiveSha.size() == 64, "archive hash captured for binding requests");
        const std::string archives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(gob)
            + "\",\"sha256\":\"" + archiveSha + "\"}]";
        const fs::path req = root / "bindings.json";
        auto bindings = [&](const std::string& body) {
            writeFile(req, body);
            return runAgent({ "bindings", cogLevel.string(), req.string() }, "", out, root);
        };
        const auto before = snapshotDir(root);
        check(bindings("{\"schemaVersion\":1,\"cog\":0," + archives + "}") == 0
              && contains(out, "\"schemaComplete\":true") && contains(out, "gate")
              && contains(out, "\"total\":2"), "bindings paged discovery -> 0 typed rows");
        const std::string scriptSha = extractSha(out, "scriptSha256");
        check(scriptSha.size() == 64, "bindings returns scriptSha256 (cogSchemaSha256Hex)");
        check(bindings("{\"schemaVersion\":1,\"cog\":0,\"limit\":1," + archives + "}") == 0
              && contains(out, "\"nextOffset\":1"), "bindings limit=1 pages with nextOffset");
        check(bindings("{\"schemaVersion\":1,\"cog\":0,\"expectedScriptSha256\":\"" + std::string(64, '0')
                       + "\"," + archives + "}") == 4, "bindings stale script hash -> 4");
        check(bindings("{\"schemaVersion\":1,\"cog\":0,\"expectedScriptSha256\":\"" + scriptSha
                       + "\"," + archives + "}") == 0, "bindings reviewed script hash -> 0");
        check(bindings(R"({"schemaVersion":1,"cog":99})") == 3, "bindings cog out of range -> 3 (before archives)");
        check(bindings("{\"schemaVersion\":1,\"cog\":0,\"offset\":-1," + archives + "}") == 2,
              "bindings bad offset -> 2 usage");
        check(bindings(R"({"schemaVersion":1,"cog":0})") == 2, "bindings without archives -> 2 usage");
        check(bindings("{\"schemaVersion\":1,\"cog\":0,\"resourceArchives\":[{\"path\":\"" + jsonPath(gob)
                       + "\",\"sha256\":\"" + std::string(64, '0') + "\"}]}") == 4,
              "bindings stale archive hash -> 4");
        const auto after = snapshotDir(root);
        auto strip = [](std::vector<std::pair<std::string, std::string>> v) {
            std::erase_if(v, [](const auto& e) { return e.first == "bindings.json"
                        || e.first == "stdout.txt" || e.first == "stdin.txt"; });
            return v;
        };
        check(strip(after) == strip(before), "bindings writes nothing");
        const fs::path vreq = root / "validate.json";
        writeFile(vreq, "{\"schemaVersion\":1," + archives + "}");
        check(runAgent({ "validate", cogLevel.string(), vreq.string() }, "", out, root) == 0
              && contains(out, "\"cogReferenceStatus\":\"checked\""), "validate with archives checks COG references");
    }
    else if (scenario == "cog-add-rebind")
    {
        const fs::path cogLevel = root / "coglevel.jkl";
        writeFile(cogLevel, cogLevelText());
        const std::string levelBefore = readFile(cogLevel);
        const fs::path gob = root / "cogs.gob";
        check(writeGobV2(gob, { { "cog\\fixture.cog", kFixtureCog },
                                { "cog\\newdoor.cog", kFixtureCog },
                                { "cog\\broken.cog", kBrokenCog } }), "cog gob fixture written");
        std::string archiveOut;
        runAgent({ "archive", gob.string() }, "", archiveOut, root);
        const std::string archiveSha = extractSha(archiveOut, "archiveSha256");
        const std::string archives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(gob)
            + "\",\"sha256\":\"" + archiveSha + "\"}]";
        const fs::path req = root / "cogreq.json";
        const std::string addBindings = "\"bindings\":[{\"symbol\":\"gate\",\"kind\":\"thing\",\"targetIndex\":0},"
                                        "{\"symbol\":\"room\",\"kind\":\"sector\",\"targetIndex\":0}]";
        auto addCog = [&](const std::string& body, const std::vector<std::string>& extra) {
            writeFile(req, body);
            std::vector<std::string> args = { "addCog", cogLevel.string(), req.string() };
            args.insert(args.end(), extra.begin(), extra.end());
            return runAgent(args, "", out, root);
        };
        check(addCog("{\"schemaVersion\":1,\"script\":\"newdoor.cog\"," + archives + "," + addBindings + "}", {}) == 0
              && contains(out, "\"mode\":\"plan\"") && contains(out, "\"newCogIndex\":1"),
              "addCog plan -> 0 with new index");
        check(readFile(cogLevel) == levelBefore, "addCog plan mutates nothing (input bytes unchanged)");
        check(addCog("{\"schemaVersion\":1,\"script\":\"newdoor.cog\",\"scriptSha256\":\"" + std::string(64, '0')
                     + "\"," + archives + "," + addBindings + "}", {}) == 4,
              "addCog stale script hash -> 4");
        check(addCog("{\"schemaVersion\":1,\"script\":\"broken.cog\"," + archives + "," + addBindings + "}", {}) == 5
              && contains(out, "incompleteCogSchema"), "addCog incomplete schema refusal code passes through (5)");
        check(addCog("{\"schemaVersion\":1,\"script\":\"newdoor.cog\"," + archives
                     + ",\"bindings\":[{\"symbol\":\"gate\",\"kind\":\"thing\",\"targetIndex\":0}]}", {}) == 5
              && contains(out, "cogParameterCountMismatch"), "addCog missing binding refusal passes through (5)");
        const fs::path added = root / "added.jkl";
        check(addCog("{\"schemaVersion\":1,\"script\":\"newdoor.cog\"," + archives + "," + addBindings + "}",
                     { added.string() }) == 0 && fs::exists(added), "addCog apply creates new file");
        check(runAgent({ "inspect", added.string() }, "", out, root) == 0 && contains(out, "\"cogs\":2"),
              "addCog apply output has two placed COGs");
        check(addCog("{\"schemaVersion\":1,\"script\":\"newdoor.cog\"," + archives + "," + addBindings + "}",
                     { added.string() }) == 5, "addCog apply over existing output -> 5 refusal");

        auto rebind = [&](const std::string& body, const std::vector<std::string>& extra,
                          const fs::path& src) {
            writeFile(req, body);
            std::vector<std::string> args = { "rebindCogReference", src.string(), req.string() };
            args.insert(args.end(), extra.begin(), extra.end());
            return runAgent(args, "", out, root);
        };
        const std::string rebindBody = "{\"schemaVersion\":1,\"cog\":0,\"parameter\":0,\"symbol\":\"gate\","
                                       "\"kind\":\"thing\",\"targetIndex\":-1," + archives + "}";
        check(rebind(rebindBody, {}, cogLevel) == 0 && contains(out, "\"mode\":\"plan\""),
              "rebindCogReference plan -> 0");
        check(readFile(cogLevel) == levelBefore, "rebind plan mutates nothing");
        check(rebind("{\"schemaVersion\":1,\"cog\":0,\"parameter\":0,\"symbol\":\"gate\",\"kind\":\"thing\","
                     "\"targetIndex\":-1,\"scriptSha256\":\"" + std::string(64, '0') + "\"," + archives + "}",
                     {}, cogLevel) == 4, "rebind stale script hash -> 4");
        check(rebind("{\"schemaVersion\":1,\"cog\":0,\"parameter\":0,\"symbol\":\"nope\",\"kind\":\"thing\","
                     "\"targetIndex\":-1," + archives + "}", {}, cogLevel) == 5
              && contains(out, "cogSymbolMismatch"), "rebind wrong symbol refusal passes through (5)");
        const fs::path rebound = root / "rebound.jkl";
        check(rebind(rebindBody, { rebound.string() }, cogLevel) == 0 && fs::exists(rebound),
              "rebind apply creates new file");
        const fs::path breq = root / "bindings.json";
        writeFile(breq, "{\"schemaVersion\":1,\"cog\":0," + archives + "}");
        check(runAgent({ "bindings", rebound.string(), breq.string() }, "", out, root) == 0
              && contains(out, "\"rawValue\":\"-1\""), "rebound output reads back null target");

        // Same operations through the ordinary plan/apply patch protocol.
        const fs::path patch = root / "patch.json";
        writeFile(patch, "{\"schemaVersion\":1," + archives + ",\"operations\":[{\"op\":\"addCog\","
              "\"script\":\"newdoor.cog\"," + addBindings + "}]}");
        const auto dirBefore = snapshotDir(root);
        check(runAgent({ "plan", cogLevel.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""), "plan with addCog op -> 0");
        auto strip = [](std::vector<std::pair<std::string, std::string>> v) {
            std::erase_if(v, [](const auto& e) { return e.first == "stdout.txt" || e.first == "stdin.txt"; });
            return v;
        };
        check(strip(snapshotDir(root)) == strip(dirBefore), "plan with addCog writes nothing");
        const fs::path planned = root / "planned.jkl";
        check(runAgent({ "apply", cogLevel.string(), patch.string(), planned.string() }, "", out, root) == 0
              && fs::exists(planned), "apply with addCog op creates new file");
        check(runAgent({ "inspect", planned.string() }, "", out, root) == 0 && contains(out, "\"cogs\":2"),
              "apply with addCog output has two placed COGs");
    }
    else if (scenario == "invalid-archive")
    {
        const fs::path cogLevel = root / "coglevel.jkl";
        writeFile(cogLevel, cogLevelText());
        const fs::path notGob = root / "JKMRES.GOO";
        writeFile(notGob, "not a gob");
        // SHA-256 of the exact bytes "not a gob" (no newline).
        const std::string notGobSha = "29342e2fbb5571d45a1f08050774858ba943254983fd0a249a03746fdd5be9b6";
        const std::string archives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(notGob)
            + "\",\"sha256\":\"" + notGobSha + "\"}]";
        const fs::path req = root / "bindings.json";
        auto bindings = [&](const std::string& body) {
            writeFile(req, body);
            return runAgent({ "bindings", cogLevel.string(), req.string() }, "", out, root);
        };
        check(bindings("{\"schemaVersion\":1,\"cog\":0," + archives + "}") == 2
              && contains(out, "\"code\":\"invalid_request\"")
              && contains(out, "\"errorCode\":\"invalid_request\"")
              && contains(out, "Resource archive is not a readable GOB/GOO archive")
              && contains(out, "JKMRES.GOO"),
              "bindings with non-GOB resource archive -> 2 invalid_request naming file");
        // Unreadable file stays exit 3.
        const std::string missingArchives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(root / "missing.goo")
            + "\",\"sha256\":\"" + std::string(64, '0') + "\"}]";
        check(bindings("{\"schemaVersion\":1,\"cog\":0," + missingArchives + "}") == 3,
              "bindings with missing archive file -> 3 input error");
        // SHA mismatch stays exit 4.
        const std::string staleArchives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(notGob)
            + "\",\"sha256\":\"" + std::string(64, '0') + "\"}]";
        check(bindings("{\"schemaVersion\":1,\"cog\":0," + staleArchives + "}") == 4,
              "bindings with stale archive hash -> 4 conflict");
        // Valid archive still passes.
        const fs::path gob = root / "cogs.gob";
        check(writeGobV2(gob, { { "cog\\fixture.cog", kFixtureCog } }), "valid gob fixture written for invalid-archive scenario");
        std::string archiveOut;
        runAgent({ "archive", gob.string() }, "", archiveOut, root);
        const std::string archiveSha = extractSha(archiveOut, "archiveSha256");
        check(archiveSha.size() == 64, "valid archive hash captured");
        const std::string validArchives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(gob)
            + "\",\"sha256\":\"" + archiveSha + "\"}]";
        check(bindings("{\"schemaVersion\":1,\"cog\":0," + validArchives + "}") == 0,
              "bindings with valid GOB archive -> 0");
    }
    else
    {
        std::cerr << "unknown scenario: " << scenario << "\n";
        return 2;
    }

    fs::remove_all(root, ec);
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "scenario " << scenario << " passed\n";
    return 0;
}
