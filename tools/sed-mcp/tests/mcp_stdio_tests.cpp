// End-to-end stdio protocol tests driving the real sed-mcp binary.
// Each ctest entry runs one named scenario: the test writes newline-
// delimited JSON-RPC to the binary's stdin and reads its stdout.
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SED_MCP_EXE
#error "SED_MCP_EXE must name the built sed-mcp binary"
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
    if (s.find(' ') == std::string::npos) return s;
    return "\"" + s + "\"";
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
bool contains(const std::string& s, const std::string& needle)
{
    return s.find(needle) != std::string::npos;
}

// Runs one stdio session: `session` is fed to the binary's stdin and
// its stdout is returned. The workspace is the scenario root.
std::string runSession(const fs::path& workDir, const fs::path& workspace,
                       const std::string& session)
{
    const fs::path inFile = workDir / "mcp-stdin.txt";
    const fs::path outFile = workDir / "mcp-stdout.txt";
    writeFile(inFile, session);
    std::string cmd = quote(SED_MCP_EXE) + " --workspace " + quote(workspace.string())
        + " < " + quote(inFile.string()) + " > " + quote(outFile.string()) + " 2>&1";
    const int rc = std::system(cmd.c_str());
    check(rc == 0, "sed-mcp session exits 0");
    return readFile(outFile);
}

void writeU32LE(std::ofstream& out, std::uint32_t v)
{
    const char b[4] = { static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
                        static_cast<char>((v >> 16) & 0xff), static_cast<char>((v >> 24) & 0xff) };
    out.write(b, 4);
}

// Synthetic GOB v2 archive (same layout the agent CLI tests write).
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

std::string extractEscapedSha(const std::string& json, const std::string& key)
{
    // Tool payloads ride inside a JSON string, so keys appear escaped.
    const std::string pat = "\\\"" + key + "\\\":\\\"";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) return {};
    return json.substr(pos + pat.size(), 64);
}
std::string extractField(const std::string& json, const std::string& key)
{
    const std::string pat = "\"" + key + "\":\"";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) return {};
    const auto end = json.find('"', pos + pat.size());
    if (end == std::string::npos) return {};
    return json.substr(pos + pat.size(), end - pos - pat.size());
}

std::string callLine(int id, const std::string& tool, const std::string& argumentsJson)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
        + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + tool
        + "\",\"arguments\":" + argumentsJson + "}}\n";
}

// Minimal synthetic JKL (same original fixture shape as the agent tests).
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

const char* kPatch = R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":9}]})";

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: mcp_stdio_tests <scenario>\n"; return 2; }
    const std::string scenario = argv[1];
    const fs::path root = fs::temp_directory_path() / ("sed-mcp-test-" + scenario);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path level = root / "level.jkl";
    writeFile(level, kJkl);
    const std::string patch(kPatch);

    if (scenario == "protocol")
    {
        std::string session;
        session += "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\"}}\n";
        session += "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n";
        session += "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n";
        session += "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\n";
        session += callLine(4, "sed_nope", "{}");
        session += "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"bogus/method\"}\n";
        session += "this is not json\n";
        const std::string out = runSession(root, root, session);
        check(contains(out, "\"protocolVersion\":\"2024-11-05\""), "initialize returns protocolVersion");
        check(contains(out, "\"name\":\"sed-mcp\""), "initialize names sed-mcp");
        for (const char* tool : { "sed_discover", "sed_query", "sed_select", "sed_diagnose",
                                  "sed_plan", "sed_apply", "sed_new", "sed_convert",
                                  "sed_archive", "sed_snapshot", "sed_outcome",
                                  "sed_package_plan", "sed_package_apply" })
            check(contains(out, std::string("\"name\":\"") + tool + "\""),
                  std::string("tools/list contains ") + tool);
        check(contains(out, "\"id\":4") && contains(out, "-32602") && contains(out, "unknown tool"),
              "unknown tool -> JSON-RPC -32602 error");
        check(contains(out, "\"id\":5") && contains(out, "-32601"),
              "unknown method -> JSON-RPC -32601 error");
        check(contains(out, "-32700"), "malformed line -> JSON-RPC -32700 parse error");
    }
    else if (scenario == "writes")
    {
        // plan first (separate session so its hash can be fed to apply).
        std::string session;
        session += "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n";
        session += callLine(2, "sed_discover", "{\"path\":\"level.jkl\"}");
        session += callLine(3, "sed_plan", "{\"path\":\"level.jkl\",\"request\":" + patch + "}");
        std::string out = runSession(root, root, session);
        check(contains(out, "\\\"command\\\":\\\"inspect\\\"") && contains(out, "\\\"sectors\\\":1"),
              "sed_discover inspects the level");
        check(contains(out, "\\\"command\\\":\\\"plan\\\""), "sed_plan plans");
        const std::string plannedSha = extractEscapedSha(out, "outputSha256");
        check(plannedSha.size() == 64, "plan returns outputSha256 in tool text");
        check(!fs::exists(root / "out.jkl"), "plan writes nothing");

        session.clear();
        session += callLine(1, "sed_apply", "{\"path\":\"level.jkl\",\"request\":" + patch
            + ",\"output\":\"out.jkl\",\"expectedOutputSha256\":\"" + plannedSha
            + "\",\"operationId\":\"op-apply-1\"}");
        session += callLine(2, "sed_outcome", "{\"operationId\":\"op-apply-1\"}");
        out = runSession(root, root, session);
        check(fs::exists(root / "out.jkl"), "sed_apply creates the new output");
        check(contains(out, "\\\"outcome\\\":\\\"completed\\\""),
              "sed_outcome reports the journaled completion");
        check(contains(out, "\\\"state\\\":\\\"completed\\\""), "outcome state is completed");

        // Re-issuing the same operationId replays the journal, not the
        // write (a real retry would be refused: out.jkl already exists).
        session = callLine(1, "sed_apply", "{\"path\":\"level.jkl\",\"request\":" + patch
            + ",\"output\":\"out.jkl\",\"expectedOutputSha256\":\"" + plannedSha
            + "\",\"operationId\":\"op-apply-1\"}");
        out = runSession(root, root, session);
        check(contains(out, "\\\"journaled\\\":true") && !contains(out, "\"isError\":true"),
              "same operationId returns the journaled result without retrying");

        // A different operationId targeting the existing file is refused.
        session = callLine(1, "sed_apply", "{\"path\":\"level.jkl\",\"request\":" + patch
            + ",\"output\":\"out.jkl\",\"operationId\":\"op-apply-2\"}");
        out = runSession(root, root, session);
        check(contains(out, "\"isError\":true"), "apply over an existing output is an error (no overwrite)");

        // Writes escaping the workspace are refused and create nothing.
        const fs::path outside = root.parent_path() / "sed-mcp-escape.jkl";
        fs::remove(outside, ec);
        session = callLine(1, "sed_apply", "{\"path\":\"level.jkl\",\"request\":" + patch
            + ",\"output\":\"../sed-mcp-escape.jkl\",\"operationId\":\"op-escape\"}");
        out = runSession(root, root, session);
        check(contains(out, "outsideWorkspace") && !fs::exists(outside),
              "write outside the workspace is refused, nothing created");
    }
    else if (scenario == "thing-frames")
    {
        std::string pathLevel = kJkl;
        const std::string oldTemplate = "agent_template none type=actor model3d=agent.3do";
        const std::string pathTemplate = "agent_template none type=actor move=path model3d=agent.3do";
        const auto templateAt = pathLevel.find(oldTemplate);
        check(templateAt != std::string::npos, "MCP path-template fixture anchor exists");
        if (templateAt != std::string::npos)
            pathLevel.replace(templateAt, oldTemplate.size(), pathTemplate);
        const std::string oldThing = "0: agent_template agent_actor 1 2 3 0 0 0 0 health=100";
        const std::string pathThing =
            "0: agent_template agent_actor 1 2 3 0 0 0 0 health=100 numframes=1 frame=(1/2/3:0/0/0) customparam=keep";
        const auto thingAt = pathLevel.find(oldThing);
        check(thingAt != std::string::npos, "MCP path-Thing fixture anchor exists");
        if (thingAt != std::string::npos) pathLevel.replace(thingAt, oldThing.size(), pathThing);
        writeFile(level, pathLevel);
        const std::string framePatch =
            R"({"schemaVersion":1,"operations":[{"op":"setThingFrames","thing":0,"frames":[{"position":[4,5,6],"orientation":[10,20,30]},{"position":[7,8.5,9],"orientation":[-10,45,180]}]}]})";
        std::string session = callLine(1, "sed_plan", "{\"path\":\"level.jkl\",\"request\":" + framePatch + "}");
        std::string out = runSession(root, root, session);
        check(contains(out, "\\\"command\\\":\\\"plan\\\"") && !contains(out, "\"isError\":true"),
              "sed_plan accepts setThingFrames through the generic operation transport");
        const std::string plannedSha = extractEscapedSha(out, "outputSha256");
        check(plannedSha.size() == 64 && !fs::exists(root / "frames.jkl"),
              "MCP frame plan returns its output hash and writes no level");

        session = callLine(1, "sed_apply", "{\"path\":\"level.jkl\",\"request\":" + framePatch
            + ",\"output\":\"frames.jkl\",\"expectedOutputSha256\":\"" + plannedSha
            + "\",\"operationId\":\"op-frames-1\"}");
        session += callLine(2, "sed_outcome", "{\"operationId\":\"op-frames-1\"}");
        out = runSession(root, root, session);
        check(fs::exists(root / "frames.jkl") && !contains(out, "\"isError\":true"),
              "MCP apply creates the frame-authored output with the planned hash");
        const std::string applied = readFile(root / "frames.jkl");
        check(contains(applied, "health=100 customparam=keep numframes=2")
                  && contains(applied, "frame=(4/5/6:10/20/30) frame=(7/8.5/9:-10/45/180)")
                  && !contains(applied, "frame=(1/2/3:0/0/0)"),
              "MCP output readback preserves unrelated values and ordered typed frames");
        check(contains(out, "\\\"operationId\\\":\\\"op-frames-1\\\"")
                  && contains(out, "\\\"outcome\\\":\\\"completed\\\"")
                  && contains(out, "\\\"state\\\":\\\"completed\\\""),
              "MCP frame apply has a completed journaled outcome");
    }
    else if (scenario == "outcome")
    {
        // A completed write, then an interrupted one: seed the journal
        // with the completed write's fingerprint but a "started" state
        // under a fresh operationId, as a crashed process would leave it.
        std::string session = callLine(1, "sed_apply", "{\"path\":\"level.jkl\",\"request\":" + patch
            + ",\"output\":\"out.jkl\",\"operationId\":\"op-done\"}");
        std::string out = runSession(root, root, session);
        check(fs::exists(root / "out.jkl"), "initial apply completes");
        const fs::path journal = root / ".sed-mcp" / "journal.jsonl";
        check(fs::exists(journal), "journal file exists after a write");
        const std::string fingerprint = extractField(readFile(journal), "fingerprint");
        check(fingerprint.size() == 64, "journal records the request fingerprint");

        std::ofstream append(journal, std::ios::binary | std::ios::app);
        append << "{\"fingerprint\":\"" << fingerprint
               << "\",\"operationId\":\"op-interrupted\",\"state\":\"started\",\"tool\":\"sed_apply\"}\n";
        append.close();

        session = callLine(1, "sed_outcome", "{\"operationId\":\"op-interrupted\"}");
        session += callLine(2, "sed_outcome", "{\"operationId\":\"op-never-seen\"}");
        out = runSession(root, root, session);
        check(contains(out, "\\\"outcome\\\":\\\"uncertain\\\""),
              "interrupted write -> sed_outcome reports uncertain");
        check(contains(out, "\\\"state\\\":\\\"unknown\\\""),
              "unknown operationId -> sed_outcome reports unknown");

        // Re-issuing the interrupted call (same intent as the seeded
        // fingerprint) must not retry the write.
        session = callLine(1, "sed_apply", "{\"path\":\"level.jkl\",\"request\":" + patch
            + ",\"output\":\"out.jkl\",\"operationId\":\"op-interrupted\"}");
        out = runSession(root, root, session);
        check(contains(out, "\\\"outcome\\\":\\\"uncertain\\\"") && contains(out, "\"isError\":true"),
              "interrupted operationId is not silently retried");
        check(!fs::exists(root / "out2.jkl"), "no blind replay created a new file");
        check(contains(readFile(journal), "op-interrupted"),
              "journal retains the interrupted record for later lookup");
    }
    else if (scenario == "snapshot-policy")
    {
        std::string session = callLine(1, "sed_snapshot",
            "{\"path\":\"level.jkl\",\"output\":\"snap.png\",\"request\":{\"schemaVersion\":1,\"width\":64,\"height\":64}}");
        session += callLine(2, "sed_query",
            "{\"path\":\"level.jkl\",\"request\":{\"schemaVersion\":1,\"collection\":\"summary\"}}");
        session += callLine(3, "sed_diagnose", "{\"path\":\"level.jkl\"}");
        session += callLine(4, "sed_new", "{\"output\":\"fresh.jkl\",\"kind\":\"mots\",\"operationId\":\"op-new-1\"}");
        session += callLine(5, "sed_convert",
            "{\"path\":\"level.jkl\",\"output\":\"converted.sed\",\"operationId\":\"op-conv-1\"}");
        session += callLine(6, "sed_new",
            "{\"output\":\"room.jkl\",\"request\":{\"schemaVersion\":1,\"kind\":\"mots\","
            "\"origin\":[10,20,30],\"size\":[4,6,8]},\"operationId\":\"op-new-room\"}");
        session += callLine(7, "sed_diagnose", "{\"path\":\"room.jkl\"}");
        const std::string out = runSession(root, root, session);
        check(contains(out, "\\\"renderer\\\":\\\"software-v1\\\"")
                && fs::exists(root / "snap.png")
                && readFile(root / "snap.png").starts_with("\x89PNG"),
              "sed_snapshot renders a real software-v1 PNG");
        check(contains(out, "\\\"command\\\":\\\"query\\\""), "sed_query runs over MCP");
        check(contains(out, "\\\"valid\\\":true"), "sed_diagnose validates over MCP");
        check(fs::exists(root / "fresh.jkl"), "sed_new creates a scaffold over MCP");
        check(fs::exists(root / "converted.sed"), "sed_convert writes a new file over MCP");
        check(fs::exists(root / "room.jkl")
                && contains(out, "\\\"origin\\\":[10,20,30]")
                && contains(out, "\\\"size\\\":[4,6,8]"),
            "sed_new forwards starter-room origin/size over MCP");
        check(contains(readFile(root / "room.jkl"), "14.00000000 26.00000000 38.00000000"),
            "MCP starter room spans exactly origin..origin+size");

        // Reads outside workspace+resource roots are refused.
        const std::string outsideSession = callLine(1, "sed_discover",
            "{\"path\":\"../outside.jkl\"}");
        const std::string outsideOut = runSession(root, root, outsideSession);
        check(contains(outsideOut, "outsideWorkspace"),
              "read outside the workspace is refused");
    }
    else if (scenario == "package")
    {
        // sed_package_plan forwards the agent package-plan contract,
        // including unverified-scope disclosure, unchanged.
        writeFile(level, std::string(kJkl)
            + "\nSECTION: COGS\nWORLD COGS 1\n0: gate.cog 0 0\nEND\n");
        const fs::path res = root / "res.gob";
        check(writeGobV2(res, {
                  { "mat\\agent.mat", "MATBYTES" },
                  { "misc\\cmp\\agent.cmp", "CMPBYTES" },
                  { "3do\\agent.3do", "3DOBYTES" },
                  { "cog\\gate.cog", "symbols\nsound alarmSnd=alarm.wav\nend\n" },
                  { "sound\\alarm.wav", "WAVBYTES" } }),
            "MCP package gob fixture written");
        std::string session = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n";
        session += callLine(2, "sed_archive", "{\"path\":\"res.gob\"}");
        std::string out = runSession(root, root, session);
        const std::string resSha = extractEscapedSha(out, "archiveSha256");
        check(resSha.size() == 64, "MCP package archive hash captured");

        const std::string request = "{\"schemaVersion\":1,\"archives\":[{\"path\":\""
            + jsonPath(res) + "\",\"sha256\":\"" + resSha
            + "\"}],\"episode\":{\"episodeTitle\":\"Pack Test\",\"levelFileName\":\"level.jkl\"}}";
        session = callLine(1, "sed_package_plan",
            "{\"path\":\"level.jkl\",\"request\":" + request + "}");
        out = runSession(root, root, session);
        check(!contains(out, "\"isError\":true")
                && contains(out, "\\\"canBuild\\\":true"),
            "sed_package_plan builds a reviewed plan over MCP");
        check(contains(out, "cog-literal-references"),
            "sed_package_plan discloses cog literal references over MCP");
        check(contains(out, "\\\"runtimeClosureVerified\\\":false"),
            "sed_package_plan keeps runtime closure unverified over MCP");
    }
    else if (scenario == "diagnose-fan")
    {
        // sed_diagnose forwards the exact fan-degeneracy section of the
        // agent validate contract, including request work limits.
        std::string collinear = kJkl;
        {
            const std::string before = "2: 0 1 0";
            const auto at = collinear.find(before);
            check(at != std::string::npos, "MCP fan fixture contains vertex 2");
            if (at != std::string::npos) collinear.replace(at, before.size(), "2: 2 0 0");
        }
        writeFile(root / "fan-collinear.jkl", collinear);
        std::string session = callLine(1, "sed_diagnose", "{\"path\":\"level.jkl\"}");
        session += callLine(2, "sed_diagnose", "{\"path\":\"fan-collinear.jkl\"}");
        const std::string out = runSession(root, root, session);
        check(contains(out, "\\\"fanDegeneracyStatus\\\":\\\"complete\\\""),
            "sed_diagnose reports completed fan inspection over MCP");
        check(contains(out, "\\\"valid\\\":false")
                && contains(out, "\\\"code\\\":\\\"fan-degeneracy\\\"")
                && contains(out, "\\\"kind\\\":\\\"collinear-corners\\\"")
                && contains(out, "\\\"repairAuthorized\\\":false"),
            "sed_diagnose forwards the exact fan finding over MCP");
        session = callLine(1, "sed_diagnose",
            "{\"path\":\"fan-collinear.jkl\",\"request\":{\"schemaVersion\":1,"
            "\"fanDegeneracyLimits\":{\"maxFanTriangles\":0}}}");
        const std::string bounded = runSession(root, root, session);
        check(contains(bounded, "\\\"fanDegeneracyStatus\\\":\\\"budget-exceeded\\\"")
                && contains(bounded, "\\\"code\\\":\\\"fan-degeneracy-scan-incomplete\\\""),
            "sed_diagnose forwards fan work limits over MCP");
    }
    else
    {
        std::cerr << "unknown scenario: " << scenario << "\n";
        return 2;
    }

    if (failures == 0) std::cout << "PASS " << scenario << "\n";
    return failures == 0 ? 0 : 1;
}


