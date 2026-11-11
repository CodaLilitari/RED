// End-to-end tests driving the real sed-agent binary. Each ctest entry
// runs one named scenario (allocation-test precedent: own executable,
// WORKING_DIRECTORY repo root, TIMEOUT set by CMake).
#ifndef _WIN32
#include <sys/wait.h>
#endif
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
    if (!stdinData.empty())
    {
        const fs::path inFile = workDir / "stdin.txt";
        std::ofstream(inFile, std::ios::binary) << stdinData;
        cmd += " < " + quote(inFile.string());
    }
    cmd += " > " + quote(outFile.string()) + " 2>&1";
#endif
    int rc = std::system(cmd.c_str());
#ifndef _WIN32
    // POSIX std::system returns a wait status, not the exit code.
    rc = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
#endif
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

// Compact SHA-256 (FIPS 180-4) for hashing bridge fixtures in scenarios.
std::string sha256HexOf(const std::string& data)
{
    static constexpr std::uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    std::vector<std::uint8_t> msg(data.begin(), data.end());
    const std::uint64_t bitLen = static_cast<std::uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>(bitLen >> (i * 8)));
    std::uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                           0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t base = 0; base < msg.size(); base += 64)
    {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (msg[base + i * 4] << 24) | (msg[base + i * 4 + 1] << 16)
                 | (msg[base + i * 4 + 2] << 8) | msg[base + i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3],
                      e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    const char* hex = "0123456789abcdef";
    std::string out;
    for (const std::uint32_t word : h)
        for (int shift = 28; shift >= 0; shift -= 4)
            out += hex[(word >> shift) & 0xF];
    return out;
}

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
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setSectorFlags","sector":0,"flags":16}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0,
              "setSectorFlags accepts legacy flags spelling");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setSectorFlags","sector":0,"value":4294967296}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
              "sector flags cannot wrap past their stored 32-bit range");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"setSectorFlags","sector":1e100,"value":0}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
              "oversized integer selector refuses before narrowing");
        for (const char* invalid : {
            R"({"operations":[{"op":"batchUvTransform","surfaces":[[0.5,0]],"pivot":[0,0],"scale":[1,1]}]})",
            R"({"operations":[{"op":"applyMaskedSurfaceRecipe","surfaces":[[0,0]],"surfaceFlags":{"any":1.5},"setSurfaceFlags":1}]})",
            R"({"operations":[{"op":"buildDome","center":[20,20,20],"radius":2,"headroom":4,"segments":4294967300,"rings":2}]})",
            R"({"operations":[{"op":"applyMaskedSurfaceRecipe","surfaces":[[0,0]],"setSurfaceFlags":4294967296}]})" })
        {
            writeFile(patch, invalid);
            check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
                  "fractional or overflowing integer adapter fields refuse without rounding or wrapping");
        }
        writeFile(patch, R"({"collection":"things","limit":1.5})");
        check(runAgent({ "query", level.string(), patch.string() }, "", out, root) == 2,
              "query page size must be integral");
        for (const auto invalidColor : {
            R"({"operations":[{"op":"setSectorAmbient","sector":0,"value":1e300}]})",
            R"({"operations":[{"op":"placeLightArray","layout":{"kind":"linear","origin":[0,0,0],"step":[1,0,0],"count":1},"color":[1e300,1,1]}]})",
            R"({"operations":[{"op":"bakeLighting","skyIntensity":1,"skyColor":[1e300,1,1]}]})" })
        {
            writeFile(patch, invalidColor);
            check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
                "color values refuse before overflowing their stored float representation");
        }
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

        const fs::path outpost = root / "outpost-valid.jkl";
        writeFile(outpost, kOutpostJkl);
        check(runAgent({ "validate", outpost.string() }, "", out, root) == 0
              && contains(out, "\"valid\":true") && !contains(out, "sealed-seam"),
              "validate matching sealed walls -> valid, no seam issue");

        // Split only room B's shared wall. Room A retains one quad, so the
        // two lower/upper B edges meet the middle of A's matching edges.
        std::string split = kOutpostJkl;
        auto replaceFixture = [&](const std::string& before, const std::string& after) {
            const auto at = split.find(before);
            check(at != std::string::npos, "seam fixture contains replacement: " + before);
            if (at != std::string::npos) split.replace(at, before.size(), after);
        };
        replaceFixture("WORLD VERTICES 16", "WORLD VERTICES 18");
        replaceFixture("15: 2 2 2\nWORLD TEXTURE VERTICES 16",
                       "15: 2 2 2\n16: 2 1 0\n17: 2 1 2\nWORLD TEXTURE VERTICES 18");
        replaceFixture("15: 0 0\nWORLD ADJOINS 0",
                       "15: 0 0\n16: 0 0\n17: 0 0\nWORLD ADJOINS 0");
        replaceFixture("WORLD SURFACES 12", "WORLD SURFACES 13");
        replaceFixture(
            "7: 0 0x0 0x0 3 0 1 -1 0 4 8,8 11,11 15,15 12,12 0 0 0 0\n"
            "8: 0 0x0 0x0 3 0 1 -1 0 4 11,11 10,10 14,14 15,15 0 0 0 0\n"
            "9: 0 0x0 0x0 3 0 1 -1 0 4 8,8 12,12 13,13 9,9 0 0 0 0\n"
            "10: 0 0x0 0x0 3 0 1 -1 0 4 12,12 15,15 14,14 13,13 0 0 0 0\n"
            "11: 0 0x0 0x0 3 0 1 -1 0 4 8,8 9,9 10,10 11,11 0 0 0 0",
            "7: 0 0x0 0x0 3 0 1 -1 0 4 8,8 16,16 17,17 12,12 0 0 0 0\n"
            "8: 0 0x0 0x0 3 0 1 -1 0 4 16,16 11,11 15,15 17,17 0 0 0 0\n"
            "9: 0 0x0 0x0 3 0 1 -1 0 4 11,11 10,10 14,14 15,15 0 0 0 0\n"
            "10: 0 0x0 0x0 3 0 1 -1 0 4 8,8 12,12 13,13 9,9 0 0 0 0\n"
            "11: 0 0x0 0x0 3 0 1 -1 0 4 12,12 15,15 14,14 13,13 0 0 0 0\n"
            "12: 0 0x0 0x0 3 0 1 -1 0 4 8,8 9,9 10,10 11,11 0 0 0 0");
        replaceFixture("SURFACES 6 6", "SURFACES 6 7");
        const fs::path seam = root / "outpost-sealed-seam.jkl";
        writeFile(seam, split);
        check(runAgent({ "validate", seam.string() }, "", out, root) == 0
              && contains(out, "\"valid\":false")
              && contains(out, "\"code\":\"sealed-seam\"")
              && contains(out, "\"otherSector\":1,\"otherSurface\":1")
              && contains(out, "\"sector\":0,\"severity\":\"error\",\"surface\":0"),
              "validate partial collinear sealed seam -> invalid with both surfaces");

        // The same partial edge overlap on an angled wall is not a coplanar
        // T-junction. Keep the split bottom edges on x=2 and move the top
        // edges to x=3; the validator must not report a sealed seam here.
        std::string angled = split;
        auto replaceAngledVertex = [&](const std::string& before, const std::string& after) {
            const auto at = angled.find(before);
            check(at != std::string::npos, "angled seam fixture contains vertex: " + before);
            if (at != std::string::npos) angled.replace(at, before.size(), after);
        };
        replaceAngledVertex("12: 2 0 2", "12: 3 0 2");
        replaceAngledVertex("15: 2 2 2", "15: 3 2 2");
        replaceAngledVertex("17: 2 1 2", "17: 3 1 2");
        const fs::path angledPath = root / "outpost-angled-shared-edges.jkl";
        writeFile(angledPath, angled);
        check(runAgent({ "validate", angledPath.string() }, "", out, root) == 0
              && !contains(out, "\"code\":\"sealed-seam\""),
              "validate non-coplanar partial edge overlap -> no sealed-seam false positive");

        // Fan inspection is exact and report-only: the clean outpost
        // completes with no fan finding.
        check(runAgent({ "validate", outpost.string() }, "", out, root) == 0
              && contains(out, "\"fanDegeneracyStatus\":\"complete\"")
              && !contains(out, "\"code\":\"fan-degeneracy\""),
              "validate clean fans -> complete, no fan-degeneracy issue");

        // Repeat surface 0's first corner: fan triangle (0,1,2) now has two
        // coincident corners and exactly zero area.
        std::string coincidentFan = kOutpostJkl;
        {
            const std::string before = "0: 0 0x0 0x0 3 0 1 -1 0 4 1,1 5,5 6,6 2,2 0 0 0 0";
            const auto at = coincidentFan.find(before);
            check(at != std::string::npos, "fan fixture contains surface 0");
            if (at != std::string::npos)
                coincidentFan.replace(at, before.size(),
                    "0: 0 0x0 0x0 3 0 1 -1 0 4 1,1 1,1 6,6 2,2 0 0 0 0");
        }
        const fs::path coincidentPath = root / "outpost-fan-coincident.jkl";
        writeFile(coincidentPath, coincidentFan);
        check(runAgent({ "validate", coincidentPath.string() }, "", out, root) == 0
              && contains(out, "\"valid\":false")
              && contains(out, "\"code\":\"fan-degeneracy\"")
              && contains(out, "\"kind\":\"coincident-corners\"")
              && contains(out, "\"fanIndex\":1")
              && contains(out, "\"sector\":0,\"severity\":\"error\",\"surface\":0")
              && contains(out, "\"repairAuthorized\":false")
              && contains(out, "\"fanDegeneracyStatus\":\"complete\""),
              "validate coincident fan corners -> invalid with exact fan finding");

        // Move vertex 3 onto the floor diagonal: fan triangle (0,2,3) of
        // sector 0 surface 5 is exactly collinear.
        std::string collinearFan = kOutpostJkl;
        {
            const std::string before = "3: 0 2 0";
            const auto at = collinearFan.find(before);
            check(at != std::string::npos, "fan fixture contains vertex 3");
            if (at != std::string::npos) collinearFan.replace(at, before.size(), "3: 1 1 0");
        }
        const fs::path collinearPath = root / "outpost-fan-collinear.jkl";
        writeFile(collinearPath, collinearFan);
        check(runAgent({ "validate", collinearPath.string() }, "", out, root) == 0
              && contains(out, "\"valid\":false")
              && contains(out, "\"code\":\"fan-degeneracy\"")
              && contains(out, "\"kind\":\"collinear-corners\"")
              && contains(out, "\"fanIndex\":2")
              && contains(out, "\"sector\":0,\"severity\":\"error\",\"surface\":5"),
              "validate collinear fan corners -> invalid with exact fan finding");

        // A zero work budget must report an incomplete scan, never a clean one.
        const fs::path fanBudgetRequest = root / "validate-fan-budget.json";
        writeFile(fanBudgetRequest,
            R"({"schemaVersion":1,"fanDegeneracyLimits":{"maxFanTriangles":0}})");
        check(runAgent({ "validate", outpost.string(), fanBudgetRequest.string() }, "", out, root) == 0
              && contains(out, "\"valid\":false")
              && contains(out, "\"fanDegeneracyStatus\":\"budget-exceeded\"")
              && contains(out, "\"code\":\"fan-degeneracy-scan-incomplete\"")
              && contains(out, "\"fanDegeneracyLimits\":{\"maxFanTriangles\":0,\"maxIssues\":100000}"),
              "validate zero fan budget -> incomplete scan error, limits echoed");

        // Unknown limit fields are usage errors, not silent ignores.
        const fs::path fanBogusRequest = root / "validate-fan-bogus.json";
        writeFile(fanBogusRequest,
            R"({"schemaVersion":1,"fanDegeneracyLimits":{"bogus":1}})");
        check(runAgent({ "validate", outpost.string(), fanBogusRequest.string() }, "", out, root) == 2,
              "validate unknown fanDegeneracyLimits field -> 2 usage");

        check(runAgent({ "validate", (root / "missing.jkl").string() }, "", out, root) == 3,
              "validate missing input -> 3");
        const fs::path created = root / "created.jkl";
        check(runAgent({ "new", created.string(), "mots" }, "", out, root) == 0 && fs::exists(created),
              "new mots -> 0 creates scaffold");
        check(runAgent({ "inspect", created.string() }, "", out, root) == 0 && contains(out, "\"sectors\":1"),
              "new scaffold inspects with one sector");
        check(runAgent({ "statistics", created.string() }, "", out, root) == 0
              && contains(out, "\"surfaces\":6"),
              "new scaffold is a closed six-face room");
        const fs::path interiorRequest = root / "new-interior.json";
        writeFile(interiorRequest, R"({"schemaVersion":1,"target":"sector","point":[0,0,0]})");
        check(runAgent({ "select", created.string(), interiorRequest.string() }, "", out, root) == 0
              && contains(out, "\"match\":\"unique\""),
              "new scaffold has a uniquely selectable interior");
        writeFile(interiorRequest, R"({"schemaVersion":1,"target":"sector","point":[100,100,100]})");
        check(runAgent({ "select", created.string(), interiorRequest.string() }, "", out, root) == 0
              && contains(out, "\"match\":\"none\""),
              "new scaffold has a bounded exterior");
        check(runAgent({ "new", created.string(), "mots" }, "", out, root) == 5,
              "new over existing output -> 5 refusal");
        const fs::path req = root / "new.json";
        writeFile(req, R"({"schemaVersion":1,"kind":"jk"})");
        check(runAgent({ "new", (root / "created2.jkl").string(), req.string() }, "", out, root) == 0,
              "new via JSON request (jk) -> 0");
        const fs::path droidWorks = root / "droidworks.jkl";
        check(runAgent({ "new", droidWorks.string(), "droidworks" }, "", out, root) == 0
              && contains(out, "\"kind\":\"droidworks\"") && fs::exists(droidWorks),
              "new droidworks -> 0 creates a distinct project");
        const std::string droidWorksText = readFile(droidWorks);
        check(contains(droidWorksText, "Lucas Learning Limited")
              && contains(droidWorksText, "dflt.mat")
              && contains(droidWorksText, "mission.cmp")
              && contains(droidWorksText, "walkplayer"),
              "new droidworks uses disc-evidenced copyright, material, palette and player");
        check(runAgent({ "inspect", droidWorks.string() }, "", out, root) == 0
              && contains(out, "\"kind\":3") && contains(out, "\"things\":1"),
              "new droidworks reopens with stable project identity and player");
        writeFile(req, R"({"schemaVersion":1,"kind":"droidworks"})");
        check(runAgent({ "new", (root / "droidworks.sed").string(), req.string() }, "", out, root) == 0,
              "new via JSON request (droidworks) -> 0 native SED");
        check(runAgent({ "new", (root / "bad.txt").string() }, "", out, root) == 2,
              "new with bad extension -> 2 usage");
        check(runAgent({ "new", (root / "wrong.ndy").string(), "mots" }, "", out, root) == 5
              && !fs::exists(root / "wrong.ndy"), "new refuses a MotS level disguised as NDY");

        // Configured starter room: origin/size compose checked core
        // transforms; the far corner lands exactly on origin + size.
        const fs::path roomReq = root / "new-room.json";
        writeFile(roomReq,
            R"({"schemaVersion":1,"kind":"mots","origin":[10,20,30],"size":[4,6,8]})");
        const fs::path room = root / "room.jkl";
        check(runAgent({ "new", room.string(), roomReq.string() }, "", out, root) == 0
              && fs::exists(room)
              && contains(out, "\"origin\":[10,20,30]")
              && contains(out, "\"size\":[4,6,8]"),
              "new with origin/size -> 0 and echoes the starter room");
        const std::string roomText = readFile(room);
        check(contains(roomText, "10.00000000 20.00000000 30.00000000")
              && contains(roomText, "14.00000000 26.00000000 38.00000000"),
              "configured starter room spans exactly origin..origin+size");
        check(runAgent({ "validate", room.string() }, "", out, root) == 0
              && contains(out, "\"valid\":true"),
              "configured starter room validates");
        writeFile(interiorRequest, R"({"schemaVersion":1,"target":"sector","point":[12,23,34]})");
        check(runAgent({ "select", room.string(), interiorRequest.string() }, "", out, root) == 0
              && contains(out, "\"match\":\"unique\""),
              "configured starter room center is selectable");
        writeFile(interiorRequest, R"({"schemaVersion":1,"target":"sector","point":[0,0,0]})");
        check(runAgent({ "select", room.string(), interiorRequest.string() }, "", out, root) == 0
              && contains(out, "\"match\":\"none\""),
              "configured starter room does not cover the legacy center");

        // Omitted geometry keeps the legacy scaffold and reports its defaults.
        check(runAgent({ "new", (root / "plain.jkl").string(), "mots" }, "", out, root) == 0
              && contains(out, "\"origin\":[-1,-1,-1]")
              && contains(out, "\"size\":[2,2,2]"),
              "new without geometry echoes the legacy starter room");

        // Malformed starter geometry is a usage error and writes nothing.
        writeFile(roomReq, R"({"schemaVersion":1,"kind":"mots","size":[0,2,2]})");
        check(runAgent({ "new", (root / "badroom.jkl").string(), roomReq.string() }, "", out, root) == 2
              && !fs::exists(root / "badroom.jkl"),
              "new with zero size component -> 2 usage, no file");
        writeFile(roomReq, R"({"schemaVersion":1,"kind":"mots","origin":[0,0]})");
        check(runAgent({ "new", (root / "badroom2.jkl").string(), roomReq.string() }, "", out, root) == 2
              && !fs::exists(root / "badroom2.jkl"),
              "new with short origin array -> 2 usage, no file");
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
        check(runAgent({ "convert", level.string(), (root / "wrong-convert.ndy").string() }, "", out, root) == 5
              && !fs::exists(root / "wrong-convert.ndy"), "convert refuses a non-IJIM level disguised as NDY");
    }
    else if (scenario == "archive-extract-launch")
    {
        const fs::path gob = root / "resources.gob";
        check(writeGobV2(gob, { { "test.jkl", kJkl }, { "cog\\fixture.cog", kFixtureCog } }),
              "gob fixture written");
        check(runAgent({ "archive", gob.string() }, "", out, root) == 0
              && contains(out, "test.jkl") && contains(out, "\"archiveSha256\":\""),
              "archive lists entries with archive hash");
        const fs::path aliases = root / "aliases.gob";
        check(writeGobV2(aliases, { { "cog\\same.cog", "12345678" }, { "COG/SAME.COG", "" } }),
            "duplicate normalized-name archive fixture written");
        check(runAgent({ "archive", aliases.string() }, "", out, root) == 0
                && contains(out, sha256HexOf("")) && contains(out, sha256HexOf("12345678")),
            "archive listing hashes each physical row consistently with its own offset and size");
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
        for (const auto invalidPage : {
            R"({"collection":"things","offset":-1})",
            R"({"collection":"things","limit":-1})",
            R"({"collection":"things","offset":0.5})",
            R"({"collection":"things","limit":1.5})" })
        {
            writeFile(req, invalidPage);
            check(runAgent({ "workbench-query", level.string(), req.string() }, "", out, root) == 2,
                "workbench page fields reject negative or fractional values before unsigned conversion");
        }
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
        const fs::path outpost = root / "outpost.jkl";
        writeFile(outpost, kOutpostJkl);
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"batchUvCylindrical","surfaces":[[0,0]],"axisOrigin":[0,0,0],"axisDirection":[0,0,1],"texelsPerUnit":8}]})");
        check(runAgent({ "plan", outpost.string(), patch.string() }, "", out, root) == 0,
              "batchUvCylindrical plan -> 0");
        check(runAgent({ "apply", outpost.string(), patch.string(), (root / "uvcyl.jkl").string() }, "", out, root) == 0
              && fs::exists(root / "uvcyl.jkl"), "batchUvCylindrical apply -> 0");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"batchUvCylindrical","surfaces":[[0,0]],"axisOrigin":[0,0,0],"axisDirection":[0,0,0],"texelsPerUnit":8}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidBatchUvInput"),
              "batchUvCylindrical zero axis -> 5 invalidBatchUvInput");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"batchUvSpherical","surfaces":[[0,0]],"center":[0,0,0],"poleDirection":[0,0,1],"texelsPerUnit":8}]})");
        check(runAgent({ "plan", outpost.string(), patch.string() }, "", out, root) == 0,
              "batchUvSpherical plan -> 0");
        check(runAgent({ "apply", outpost.string(), patch.string(), (root / "uvsph.jkl").string() }, "", out, root) == 0
              && fs::exists(root / "uvsph.jkl"), "batchUvSpherical apply -> 0");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"batchUvSpherical","surfaces":[[0,0]],"center":[0,0,0],"poleDirection":[0,0,0],"texelsPerUnit":8}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidBatchUvInput"),
              "batchUvSpherical zero pole -> 5 invalidBatchUvInput");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"applyMaskedSurfaceRecipe","surfaces":[[0,0]],"maxSlopeDegrees":90,"material":"masked.mat"}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0,
              "applyMaskedSurfaceRecipe plan -> 0");
        check(runAgent({ "apply", level.string(), patch.string(), (root / "masked.jkl").string() }, "", out, root) == 0
              && fs::exists(root / "masked.jkl"), "applyMaskedSurfaceRecipe apply -> 0");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"applyMaskedSurfaceRecipe","surfaces":[[0,0]],"minSlopeDegrees":45,"maxSlopeDegrees":10}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5,
              "inverted slope band refuses with core code (exit 5)");
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
        check(runAgent({ "snapshot", level.string(), req.string(), png.string() }, "", out, root) == 0
              && contains(out, "\"renderer\":\"software-v1\"") && fs::exists(png)
              && readFile(png).starts_with("\x89PNG"),
              "snapshot renders a real PNG with the software renderer");
        check(runAgent({ "snapshot", level.string(), req.string(), png.string() }, "", out, root) == 5,
              "snapshot refuses an existing output (5)");
        fs::remove(png);
        check(runAgent({ "snapshot", level.string() }, "", out, root) == 2,
              "snapshot missing args -> 2 usage");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "software-v1"),
              "schema documents the software snapshot renderer");
        // D-08: the request contract (projection exclusivity, bounds)
        // is validated before rendering.
        writeFile(req, R"({"schemaVersion":1,"width":64,"height":64,"projection":"orthographic"})");
        check(runAgent({ "snapshot", level.string(), req.string(), png.string() }, "", out, root) == 2,
              "snapshot orthographic without orthographicHeight -> 2 usage");
        writeFile(req, R"({"schemaVersion":1,"width":64,"height":64,"projection":"perspective","orthographicHeight":10})");
        check(runAgent({ "snapshot", level.string(), req.string(), png.string() }, "", out, root) == 2,
              "snapshot perspective with orthographicHeight -> 2 usage");
        writeFile(req, R"({"schemaVersion":1,"width":32,"height":64})");
        check(runAgent({ "snapshot", level.string(), req.string(), png.string() }, "", out, root) == 2,
              "snapshot width below 64 -> 2 usage");
        writeFile(req, R"({"schemaVersion":1,"width":64,"height":64,"projection":"orthographic","orthographicHeight":10,"fullbright":true})");
        check(runAgent({ "snapshot", level.string(), req.string(), png.string() }, "", out, root) == 0
              && contains(out, "\"renderer\":\"software-v1\"") && fs::exists(png)
              && readFile(png).starts_with("\x89PNG"),
              "snapshot valid D-08 orthographic request renders");
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

        // makeDoor binds the fixed stock door script from the patch
        // resources; without it, or without a resolvable height, it refuses.
        const fs::path doorGob = root / "door.gob";
        const char* doorCog = "symbols\nmessage startup\nthing door\nflex movespeed=2.5\nend\ncode\nstartup:\n return;\nend\n";
        check(writeGobV2(doorGob, { { "cog\\00_door.cog", doorCog } }), "door gob fixture written");
        std::string doorArchiveOut;
        runAgent({ "archive", doorGob.string() }, "", doorArchiveOut, root);
        const std::string doorArchives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(doorGob)
            + "\",\"sha256\":\"" + extractSha(doorArchiveOut, "archiveSha256") + "\"}]";
        writeFile(patch, "{\"schemaVersion\":1," + archives
            + ",\"operations\":[{\"op\":\"makeDoor\",\"thing\":0,\"height\":1.5}]}");
        check(runAgent({ "plan", cogLevel.string(), patch.string() }, "", out, root) == 5
              && contains(out, "doorScriptUnavailable"),
              "makeDoor without the stock door script -> 5 doorScriptUnavailable");
        writeFile(patch, "{\"schemaVersion\":1," + doorArchives
            + ",\"operations\":[{\"op\":\"makeDoor\",\"thing\":0}]}");
        check(runAgent({ "plan", cogLevel.string(), patch.string() }, "", out, root) == 5
              && contains(out, "entityDoorNoHeight"),
              "makeDoor without a resolvable height -> 5 entityDoorNoHeight");
        writeFile(patch, "{\"schemaVersion\":1," + doorArchives
            + ",\"operations\":[{\"op\":\"makeDoor\",\"thing\":99,\"height\":1.5}]}");
        check(runAgent({ "plan", cogLevel.string(), patch.string() }, "", out, root) == 3,
              "makeDoor out-of-range thing -> 3");
        writeFile(patch, "{\"schemaVersion\":1," + doorArchives
            + ",\"operations\":[{\"op\":\"makeDoor\",\"thing\":0,\"height\":1.5}]}");
        const fs::path doorOut = root / "doored.jkl";
        check(runAgent({ "apply", cogLevel.string(), patch.string(), doorOut.string() }, "", out, root) == 0
              && fs::exists(doorOut), "makeDoor apply creates new file");
        const std::string doorText = readFile(doorOut);
        check(contains(doorText, "00_door.cog") && contains(doorText, "numframes"),
              "makeDoor output binds the door script and declares door frames");
        check(runAgent({ "inspect", doorOut.string() }, "", out, root) == 0 && contains(out, "\"cogs\":2"),
              "makeDoor output has the appended door COG");
        check(runAgent({ "plan", doorOut.string(), patch.string() }, "", out, root) == 5
              && contains(out, "entityAlreadyDoor"),
              "makeDoor on an existing door -> 5 entityAlreadyDoor");
    }
    else if (scenario == "thing-append-archives")
    {
        const std::string source = std::string(kOutpostJkl)
            + "\nSECTION: COGS\nWORLD COGS 1\n0: fixture.cog 0 0\nEND\n";
        writeFile(level, source);
        const fs::path gob = root / "placement.gob";
        check(writeGobV2(gob, { { "cog\\fixture.cog", kFixtureCog } }), "placement schema fixture written");
        const std::string archiveHash = sha256HexOf(readFile(gob));
        const std::string archives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(gob)
            + "\",\"sha256\":\"" + archiveHash + "\"}]";
        const std::string single = R"({"op":"addThing","sector":0,"template":"agent_template","name":"fresh","position":[1,1,1]})";
        const std::string array = R"({"op":"placeThingArray","template":"agent_template","namePrefix":"group","sector":0,"layout":{"kind":"linear","origin":[1,1,1],"step":[0.2,0,0],"count":1}})";
        const fs::path request = root / "placement.json";
        int ordinal = 0;
        for (const auto& operation : { single, array })
        {
            const std::string body = "{\"schemaVersion\":1," + archives + ",\"operations\":[" + operation + "]}";
            writeFile(request, body);
            check(runAgent({ "plan", level.string(), request.string() }, "", out, root) == 0,
                "Thing placement consumes the explicitly bound COG schema");
            const std::string plannedHash = extractSha(out, "outputSha256");
            const fs::path output = root / ("placed-" + std::to_string(++ordinal) + ".jkl");
            check(runAgent({ "apply", level.string(), request.string(), output.string(), plannedHash }, "", out, root) == 0
                    && fs::exists(output), "schema-bound Thing placement publishes the planned bytes");
            if (fs::exists(output))
            {
                check(runAgent({ "inspect", output.string() }, "", out, root) == 0
                        && contains(out, "\"things\":2") && contains(out, "\"cogs\":1"),
                    "placed output reopens with one additional Thing and unchanged COG count");
            }
            writeFile(request, "{\"operations\":[" + operation + "]}");
            check(runAgent({ "plan", level.string(), request.string() }, "", out, root) == 5
                    && contains(out, "cogAppendGuardRequired"), "Thing append without schema refuses explicitly");
            writeFile(request, body);
            const fs::path future = root / "future-reference.jkl";
            std::string futureText = source;
            const auto reference = futureText.find("0: fixture.cog 0 0");
            futureText.replace(reference, std::string("0: fixture.cog 0 0").size(), "0: fixture.cog 1 0");
            writeFile(future, futureText);
            check(runAgent({ "plan", future.string(), request.string() }, "", out, root) == 5,
                "schema-bound Thing append refuses a future Thing reference");
            writeFile(gob, readFile(gob) + "changed");
            check(runAgent({ "plan", level.string(), request.string() }, "", out, root) == 4,
                "Thing append refuses changed archive bytes");
            check(writeGobV2(gob, { { "cog\\fixture.cog", kFixtureCog } }), "placement archive restored for independent case");
        }
        check(readFile(level) == source, "all Thing placement routes preserve the source");
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
    else if (scenario == "package")
    {
        const char* packLevel =
            "SECTION: GEORESOURCE\nWorld Colormaps 1\ndflt.cmp\nEND\n"
            "SECTION: MATERIALS\nWorld Materials 1\nwall.mat\nEND\n"
            "SECTION: MODELS\nWorld Models 1\ncrate.3do\nEND\n"
            "SECTION: TEMPLATES\nWORLD TEMPLATES 1\ncrate none model3d=crate.3do\nEND\n"
            "SECTION: COGS\nWORLD COGS 1\n0: gate.cog 0 0\nEND\n";
        const fs::path level = root / "level.jkl";
        writeFile(level, packLevel);
        const fs::path res = root / "res.gob";
        check(writeGobV2(res, {
                  { "3do\\crate.3do", "SECTION: MODEL\nMATERIALS\nwall.mat\n" },
                  { "mat\\wall.mat", "MATBYTES" },
                  { "cog\\gate.cog", "COGBYTES" },
                  { "misc\\cmp\\dflt.cmp", "CMPBYTES" } }),
            "package resource gob written");
        std::string archiveListing;
        runAgent({ "archive", res.string() }, "", archiveListing, root);
        const std::string resSha = extractSha(archiveListing, "archiveSha256");
        check(resSha.size() == 64, "package resource hash captured");

        const fs::path req = root / "package.json";
        writeFile(req, "{\"schemaVersion\":1,\"archives\":[{\"path\":\""
            + jsonPath(res) + "\",\"sha256\":\"" + resSha
            + "\"}],\"episode\":{\"episodeTitle\":\"Pack Test\",\"levelFileName\":\"level.jkl\"}}");
        check(runAgent({ "package-plan", level.string(), req.string() }, "", out, root) == 0
              && contains(out, "\"canBuild\":true")
              && contains(out, "\"runtimeClosureVerified\":false")
              && contains(out, "\"cog-literal-references\""),
            "package-plan builds a reviewed plan and discloses cog literals");
        const std::string outputSha = extractSha(out, "outputSha256");
        const std::string manifestSha = extractSha(out, "manifestSha256");
        check(outputSha.size() == 64 && manifestSha.size() == 64,
            "package-plan reports output and manifest hashes");

        const fs::path packageOut = root / "pack.gob";
        check(runAgent({ "package-apply", level.string(), req.string(),
                         packageOut.string(), outputSha, manifestSha }, "", out, root) == 0
              && fs::exists(packageOut),
            "package-apply writes the reviewed package");
        check(runAgent({ "archive", packageOut.string() }, "", out, root) == 0
              && contains(out, "episode.jk"),
            "package output lists episode.jk");
        check(runAgent({ "package-apply", level.string(), req.string(),
                         packageOut.string(), outputSha, manifestSha }, "", out, root) == 5,
            "package-apply refuses an existing output");

        const fs::path packageOut2 = root / "pack2.gob";
        check(runAgent({ "package-apply", level.string(), req.string(),
                         packageOut2.string(), outputSha, std::string(64, '0') }, "", out, root) == 4
              && !fs::exists(packageOut2),
            "package-apply refuses a stale manifest hash and writes nothing");

        const fs::path shortRes = root / "short.gob";
        check(writeGobV2(shortRes, { { "cog\\gate.cog", "COGBYTES" } }),
            "package short gob written");
        std::string shortListing;
        runAgent({ "archive", shortRes.string() }, "", shortListing, root);
        const fs::path reqShort = root / "package-short.json";
        writeFile(reqShort, "{\"schemaVersion\":1,\"archives\":[{\"path\":\""
            + jsonPath(shortRes) + "\",\"sha256\":\"" + extractSha(shortListing, "archiveSha256")
            + "\"}],\"episode\":{\"episodeTitle\":\"Pack Test\",\"levelFileName\":\"level.jkl\"}}");
        check(runAgent({ "package-plan", level.string(), reqShort.string() }, "", out, root) == 3
              && contains(out, "\"canBuild\":false")
              && contains(out, "MissingResource"),
            "package-plan reports a blocked plan with exit 3");

        const fs::path notJkl = root / "level.sed";
        writeFile(notJkl, packLevel);
        check(runAgent({ "package-plan", notJkl.string(), req.string() }, "", out, root) == 2,
            "package-plan refuses non-JKL input with exit 2");
    }
    else if (scenario == "overview")
    {
        // D-09 framing half: the agent computes the Wren overview
        // camera and embedded snapshot request, but rendering stays
        // refused (renderStatus), with no image created.
        const fs::path req = root / "overview.json";
        auto overview = [&](const std::string& body) {
            writeFile(req, body);
            return runAgent({ "overview", level.string(), req.string() }, "", out, root);
        };
        check(overview(R"({"schemaVersion":1,"view":"overhead","width":64,"height":64})") == 0
              && contains(out, "\"command\":\"overview\"")
              && contains(out, "\"projection\":\"orthographic\"")
              && contains(out, "\"orthographicHeight\":1.05")
              && contains(out, "\"position\":[0.5,0.5,1]")
              && contains(out, "\"renderStatus\":\"snapshotRequiresRenderer\"")
              && contains(out, "\"imageCreated\":false"),
            "overview overhead frames the fixture and refuses rendering honestly");
        check(overview(R"({"schemaVersion":1,"view":"threequarter","framing":"contain","width":64,"height":64})") == 0
              && contains(out, "\"projection\":\"perspective\"")
              && contains(out, "\"cullBackFaces\":true"),
            "overview threequarter contain frames with default backface culling");
        const fs::path pngOut = root / "overview.png";
        check(overview("{\"schemaVersion\":1,\"view\":\"overhead\",\"width\":64,\"height\":64,\"output\":\""
                       + jsonPath(pngOut) + "\"}") == 0
              && contains(out, "\"renderStatus\":\"rendered\"")
              && contains(out, "\"imageCreated\":true")
              && fs::exists(pngOut) && readFile(pngOut).starts_with("\x89PNG"),
            "overview with output renders its framed camera to a real PNG");
        {
            // D-09 replay: the embedded snapshotRequest, fed to snapshot,
            // renders the image overview itself rendered.
            const std::string overviewOut = out;
            const std::string marker = "\"snapshotRequest\":";
            std::string embedded{};
            if (const std::size_t at = overviewOut.find(marker); at != std::string::npos)
            {
                const std::size_t start = at + marker.size();
                int depth = 0;
                for (std::size_t j = start; j < overviewOut.size(); ++j)
                {
                    if (overviewOut[j] == '{') ++depth;
                    else if (overviewOut[j] == '}' && --depth == 0)
                    {
                        embedded = overviewOut.substr(start, j - start + 1);
                        break;
                    }
                }
            }
            const fs::path replayReq = root / "overview_replay.json";
            const fs::path replayPng = root / "overview_replay.png";
            writeFile(replayReq, embedded);
            std::string replayOut;
            check(!embedded.empty()
                      && runAgent({ "snapshot", level.string(), replayReq.string(), replayPng.string() }, "", replayOut, root) == 0
                      && !extractSha(overviewOut, "imageSha256").empty()
                      && extractSha(replayOut, "imageSha256") == extractSha(overviewOut, "imageSha256"),
                "overview's embedded snapshotRequest replays through snapshot to the same image");
        }
        check(overview(R"({"schemaVersion":1,"view":"side","width":64,"height":64})") == 2,
            "overview unknown view -> 2 usage");
        check(overview(R"({"schemaVersion":1,"view":"overhead","width":64,"height":64,"margin":0.5})") == 2,
            "overview margin above 0.25 -> 2 usage");
    }
    else if (scenario == "asset-notices")
    {
        // D-07 receipt plumbing: a short-mip indexed MAT is admitted by
        // the retail mip fallback rule; validate must surface the note in
        // assetNotices instead of failing or staying silent.
        auto push32 = [](std::string& buf, std::int32_t v) {
            const auto u = static_cast<std::uint32_t>(v);
            buf.push_back(static_cast<char>(u & 0xFFu));
            buf.push_back(static_cast<char>((u >> 8) & 0xFFu));
            buf.push_back(static_cast<char>((u >> 16) & 0xFFu));
            buf.push_back(static_cast<char>((u >> 24) & 0xFFu));
        };
        std::string mat = "MAT ";
        push32(mat, 0x32); push32(mat, 2); push32(mat, 1); push32(mat, 1);
        push32(mat, 0); push32(mat, 8); // ColorInfo: Indexed, 8bpp
        for (int i = 0; i < 12; ++i) push32(mat, 0); // rest of ColorInfo
        push32(mat, 8); push32(mat, 37); for (int i = 0; i < 8; ++i) push32(mat, 0); // texture header
        push32(mat, 4); push32(mat, 4); push32(mat, 0); push32(mat, 0); push32(mat, 0); push32(mat, 3);
        for (int i = 0; i < 18; ++i) mat.push_back(static_cast<char>(i)); // mip0 (16) + 2 of 4 bytes of mip1

        const fs::path gob = root / "mats.gob";
        check(writeGobV2(gob, { { "mat\\wall.mat", mat } }), "asset-notices gob fixture written");
        std::string archiveOut;
        runAgent({ "archive", gob.string() }, "", archiveOut, root);
        const std::string archiveSha = extractSha(archiveOut, "archiveSha256");
        check(archiveSha.size() == 64, "asset-notices archive hash captured");
        const fs::path vreq = root / "validate.json";
        writeFile(vreq, "{\"schemaVersion\":1,\"resourceArchives\":[{\"path\":\"" + jsonPath(gob)
            + "\",\"sha256\":\"" + archiveSha + "\"}]}");
        check(runAgent({ "validate", level.string(), vreq.string() }, "", out, root) == 0
              && contains(out, "\"assetNotices\"")
              && contains(out, "Retail mip fallback: mip level 1 of 3 levels")
              && contains(out, "wall.mat"),
            "validate surfaces the retail mip fallback as an asset notice");
        check(runAgent({ "validate", level.string() }, "", out, root) == 0
              && contains(out, "\"assetNotices\":[]"),
            "validate without archives reports empty assetNotices");
    }
    else if (scenario == "thing-frames")
    {
        std::string pathLevel = kJkl;
        const std::string oldTemplates =
            "WORLD TEMPLATES 1\nagent_template none type=actor model3d=agent.3do";
        const std::string newTemplates =
            "WORLD TEMPLATES 3\n"
            "agent_template none type=actor model3d=agent.3do\n"
            "path_template none type=actor move=path model3d=agent.3do\n"
            "capped_template none type=actor move=path numframes=1 frame=(0/0/0:0/0/0) model3d=agent.3do";
        const std::string oldThings =
            "WORLD THINGS 1\n0: agent_template agent_actor 1 2 3 0 0 0 0 health=100";
        const std::string newThings =
            "WORLD THINGS 4\n"
            "0: path_template path_actor 1 2 3 0 0 0 0 health=100 numframes=2 frame=(1/2/3:0/0/0) thingflags=0x400 frame=(1/2/4:0/90/0) customparam=keep\n"
            "1: capped_template capped_actor 2 3 4 0 0 0 0 health=75\n"
            "2: agent_template static_actor 3 4 5 0 0 0 0 health=50\n"
            "3: agent_template other_actor 8 9 10 0 0 0 0 health=25 frame=(9/9/9:0/0/0)";
        const auto templatesAt = pathLevel.find(oldTemplates);
        const auto thingsAt = pathLevel.find(oldThings);
        check(templatesAt != std::string::npos && thingsAt != std::string::npos,
              "thing-frame fixture anchors exist");
        // Replace the later section first so the earlier anchor stays valid.
        if (thingsAt != std::string::npos) pathLevel.replace(thingsAt, oldThings.size(), newThings);
        if (templatesAt != std::string::npos) pathLevel.replace(templatesAt, oldTemplates.size(), newTemplates);
        writeFile(level, pathLevel);

        check(runAgent({ "schema" }, "", out, root) == 0
                  && contains(out, "setThingFrames{thing,frames}"),
              "schema registers setThingFrames as a typed patch operation");
        std::string inspectOut;
        check(runAgent({ "inspect", level.string() }, "", inspectOut, root) == 0
                  && contains(inspectOut, "\"things\":4"),
              "path-mover fixture opens with four Things");
        const std::string inputSha = extractSha(inspectOut, "inputSha256");
        const std::string sourceRevision = extractSha(inspectOut, "sourceRevision");
        check(inputSha.size() == 64 && sourceRevision.size() == 64,
              "thing-frame input hash and revision are available");
        const std::string validOperation =
            R"({"op":"setThingFrames","thing":0,"frames":[{"position":[4,5,6],"orientation":[10,20,30]},{"position":[7,8.5,9],"orientation":[-10,45,180]}]})";
        const fs::path patch = root / "patch.json";
        const std::string boundRequest = "{\"schemaVersion\":1,\"inputSha256\":\"" + inputSha
            + "\",\"sourceRevision\":\"" + sourceRevision + "\",\"operations\":[" + validOperation + "]}";
        writeFile(patch, boundRequest);
        const auto beforePlan = snapshotDir(root);
        const int planResult = runAgent({ "plan", level.string(), patch.string() }, "", out, root);
        check(planResult == 0 && contains(out, "\"command\":\"plan\""),
              "setThingFrames plans two typed position/orientation frames");
        if (planResult != 0) std::cerr << "setThingFrames plan output: " << out << '\n';
        const std::string plannedSha = extractSha(out, "outputSha256");
        check(plannedSha.size() == 64, "setThingFrames plan returns an output hash");
        auto stripRunnerFiles = [](std::vector<std::pair<std::string, std::string>> entries) {
            std::erase_if(entries, [](const auto& entry) {
                return entry.first == "stdout.txt" || entry.first == "stdin.txt";
            });
            return entries;
        };
        check(stripRunnerFiles(snapshotDir(root)) == stripRunnerFiles(beforePlan),
              "setThingFrames plan changes no files");

        const fs::path applied = root / "frames.jkl";
        if (plannedSha.size() == 64)
        {
            check(runAgent({ "apply", level.string(), patch.string(), applied.string(), plannedSha },
                      "", out, root) == 0 && fs::exists(applied),
                  "apply accepts the exact planned hash and creates the output");
        }
        else
        {
            check(false, "planned-hash apply is exercised");
        }
        const std::string appliedText = readFile(applied);
        auto thingLine = [&appliedText](const std::string& prefix) {
            const auto start = appliedText.find(prefix);
            if (start == std::string::npos) return std::string{};
            const auto end = appliedText.find('\n', start);
            return appliedText.substr(start, end - start);
        };
        const std::string target = thingLine("0: path_template path_actor ");
        const std::string other = thingLine("3: agent_template other_actor ");
        const std::string firstFrame = "frame=(4/5/6:10/20/30)";
        const std::string secondFrame = "frame=(7/8.5/9:-10/45/180)";
        check(contains(target, " 1.00000000 2.00000000 3.00000000 0.00000000 0.00000000 0.00000000 0 ")
                  && contains(target, "health=100") && contains(target, "thingflags=")
                  && contains(target, "customparam=keep"),
              "readback preserves the Thing pose and unrelated values");
        const auto healthAt = target.find("health=100");
        const auto flagsAt = target.find("thingflags=");
        const auto customAt = target.find("customparam=keep");
        const auto countAt = target.find("numframes=2");
        const auto firstAt = target.find(firstFrame);
        const auto secondAt = target.find(secondFrame);
        check(healthAt != std::string::npos && healthAt < flagsAt && flagsAt < customAt
                  && customAt < countAt && countAt < firstAt && firstAt < secondAt
                  && secondAt != std::string::npos,
              "readback preserves value order and synchronizes numframes before ordered frames");
        check(!contains(target, "frame=(1/2/3:0/0/0)")
                  && !contains(target, "frame=(1/2/4:0/90/0)"),
              "readback replaces rather than appends the old local frames");
        check(contains(other, "health=25") && contains(other, "frame=(9/9/9:0/0/0)")
                  && contains(appliedText, "World things 4"),
              "other Things, their frames and Thing ordering survive export");

        if (fs::exists(applied))
        {
            const fs::path reopenPatch = root / "reopen.json";
            writeFile(reopenPatch,
                R"({"schemaVersion":1,"operations":[{"op":"setThingFrames","thing":0,"frames":[{"position":[-1,-2,-3],"orientation":[1,2,3]}]}]})");
            const fs::path reopened = root / "reopened.jkl";
            check(runAgent({ "apply", applied.string(), reopenPatch.string(), reopened.string() },
                      "", out, root) == 0 && fs::exists(reopened),
                  "exported frames reopen as typed input for a second apply");
            check(contains(readFile(reopened), "numframes=1 frame=(-1/-2/-3:1/2/3)"),
                  "second apply reads back the synchronized one-frame replacement");
        }

        int refusalIndex = 0;
        auto refusesApply = [&](const std::string& operation, int expectedCode,
                                const std::string& diagnostic, const std::string& label) {
            const fs::path request = root / ("refusal-" + std::to_string(refusalIndex) + ".json");
            const fs::path destination = root / ("refusal-" + std::to_string(refusalIndex) + ".jkl");
            ++refusalIndex;
            writeFile(request, "{\"schemaVersion\":1,\"operations\":[" + operation + "]}");
            check(runAgent({ "apply", level.string(), request.string(), destination.string() },
                      "", out, root) == expectedCode && contains(out, diagnostic)
                      && !fs::exists(destination),
                  label + " refuses without output");
        };
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":[]})", 2,
                     "1 through 256", "empty agent replacement");
        refusesApply(R"({"op":"setThingFrames","thing":0})", 2,
                     "frames", "missing frames");
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":{}})", 2,
                     "frames", "non-array frames");
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":[[]]})", 2,
                     "frame", "non-object frame");
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":[{"position":[1,2],"orientation":[0,0,0]}]})", 2,
                     "position", "short position");
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":[{"position":[1,2,3],"orientation":[0,0,0,0]}]})", 2,
                     "orientation", "long orientation");
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":[{"position":[1,"2",3],"orientation":[0,0,0]}]})", 2,
                     "components", "non-numeric component");
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":[{"position":[1e999,2,3],"orientation":[0,0,0]}]})", 2,
                     "malformed request JSON", "non-finite JSON number");
        refusesApply(R"({"op":"setThingFrames","thing":0,"frames":[{"position":[1000001,2,3],"orientation":[0,0,0]}]})", 5,
                     "invalidThingFrame", "out-of-range coordinate");
        refusesApply(R"({"op":"setThingFrames","thing":99,"frames":[{"position":[1,2,3],"orientation":[0,0,0]}]})", 5,
                     "thingFrameTargetOutOfRange", "unknown Thing");
        refusesApply(R"({"op":"setThingFrames","thing":-1,"frames":[{"position":[1,2,3],"orientation":[0,0,0]}]})", 5,
                     "thingFrameTargetOutOfRange", "negative Thing selector");
        refusesApply(R"({"op":"setThingFrames","thing":1,"frames":[{"position":[1,2,3],"orientation":[0,0,0]}]})", 5,
                     "pathFrameCapacityExceeded", "full inherited Path buffer");
        refusesApply(R"({"op":"setThingFrames","thing":2,"frames":[{"position":[1,2,3],"orientation":[0,0,0]}]})", 5,
                     "thingFrameRequiresPath", "non-path Thing");

        std::string tooMany = R"({"op":"setThingFrames","thing":0,"frames":[)";
        for (int index = 0; index < 257; ++index)
        {
            if (index != 0) tooMany += ',';
            tooMany += R"({"position":[1,2,3],"orientation":[0,0,0]})";
        }
        tooMany += "]}";
        refusesApply(tooMany, 2, "1 through 256", "257-frame replacement");

        std::string archLevel = pathLevel;
        const std::string unbound = "0: path_template path_actor 1 2 3 0 0 0 0 health=100";
        const std::string bound = "0: path_template path_actor 1 2 3 0 0 0 0 0 health=100";
        const auto boundAt = archLevel.find(unbound);
        check(boundAt != std::string::npos, "ARCH fixture Thing anchor exists");
        if (boundAt != std::string::npos) archLevel.replace(boundAt, unbound.size(), bound);
        archLevel +=
            "\nSECTION: ARCHLIGHTING\nNum ArchObjects 1\n"
            "archobject 0\nnummeshes: 1\nmesh 0\nnumvertices: 1\n"
            "0: 0.5 0.25 0.125 0.75\nEND\n";
        const fs::path archPath = root / "arch.jkl";
        writeFile(archPath, archLevel);
        writeFile(patch, "{\"schemaVersion\":1,\"operations\":[" + validOperation + "]}");
        const fs::path archOut = root / "arch-out.jkl";
        check(runAgent({ "apply", archPath.string(), patch.string(), archOut.string() },
                  "", out, root) == 5 && contains(out, "thingFrameArchBound")
                  && !fs::exists(archOut),
              "ARCH-bound path mover refuses without output");

        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
            + "\",\"operations\":[" + validOperation + "]}");
        const fs::path staleHashOut = root / "stale-hash.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), staleHashOut.string() },
                  "", out, root) == 4 && !fs::exists(staleHashOut),
              "stale inputSha256 refuses without output");
        writeFile(patch, "{\"schemaVersion\":1,\"sourceRevision\":\"" + std::string(64, 'f')
            + "\",\"operations\":[" + validOperation + "]}");
        const fs::path staleRevisionOut = root / "stale-revision.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), staleRevisionOut.string() },
                  "", out, root) == 4 && !fs::exists(staleRevisionOut),
              "stale sourceRevision refuses without output");
        writeFile(patch, boundRequest);
        const fs::path wrongHashOut = root / "wrong-hash.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), wrongHashOut.string(), std::string(64, '0') },
                  "", out, root) == 4 && !fs::exists(wrongHashOut),
              "wrong planned-output hash refuses without output");
        const fs::path existing = root / "existing.jkl";
        writeFile(existing, "precious\n");
        check(runAgent({ "apply", level.string(), patch.string(), existing.string(), plannedSha },
                  "", out, root) == 5 && readFile(existing) == "precious\n",
              "existing output is never overwritten");
    }
    else if (scenario == "native-inspect")
    {
        check(sha256HexOf("").size() == 64
              && sha256HexOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "scenario sha256 helper matches the FIPS vector");
        const fs::path bridgeDst = root / "sed-cpp-bridge.exe";
        fs::copy_file(SED_BRIDGE_EXE, bridgeDst, fs::copy_options::overwrite_existing);
        const std::string exeSha = sha256HexOf(readFile(bridgeDst));
        const std::string manifestSha = sha256HexOf(
            readFile(fs::current_path() / "tools" / "sed-cpp-bridge" / "upstream.lock.json"));
        check(exeSha.size() == 64 && manifestSha.size() == 64,
            "native worker identities computed");
        const fs::path config = root / "worker-config.json";
        writeFile(config, "{\"schemaVersion\":1,\"executableSha256\":\"" + exeSha
            + "\",\"sourceManifestSha256\":\"" + manifestSha + "\",\"timeoutSeconds\":10}");

        const fs::path level = root / "level.jkl";
        writeFile(level, kJkl);
#ifndef _WIN32
        // Worker hosting is Windows-only. Elsewhere the agent still verifies
        // the configured identities, then refuses before starting a process.
        check(runAgent({ "native-inspect", level.string(), "jk", "jkl", config.string() },
                  "", out, root) == 5
              && contains(out, "Windows-only"),
            "native-inspect refuses worker hosting outside Windows");
#else
        check(runAgent({ "native-inspect", level.string(), "jk", "jkl", config.string() },
                  "", out, root) == 0
              && contains(out, "\"status\":\"Inspected\"")
              && contains(out, "\"sectors\":1")
              && contains(out, "\"executableSha256\":\"" + exeSha + "\""),
            "native-inspect reports the upstream-core observation");

        const fs::path sedOut = root / "level.sed";
        check(runAgent({ "convert", level.string(), sedOut.string() }, "", out, root) == 0,
            "native-inspect fixture converts to .sed");
        check(runAgent({ "native-inspect", sedOut.string(), "jk", "native-sed", config.string() },
                  "", out, root) == 0
              && contains(out, "\"status\":\"Inspected\"")
              && contains(out, "\"optionalSections\""),
            "native-inspect observes the native .sed document");

        const fs::path droidWorksJkl = root / "droidworks.jkl";
        const fs::path droidWorksSed = root / "droidworks.sed";
        check(runAgent({ "new", droidWorksJkl.string(), "droidworks" }, "", out, root) == 0
              && runAgent({ "native-inspect", droidWorksJkl.string(), "droidworks", "jkl", config.string() },
                  "", out, root) == 0
              && contains(out, "\"projectKind\":\"droidworks\"")
              && runAgent({ "new", droidWorksSed.string(), "droidworks" }, "", out, root) == 0
              && runAgent({ "native-inspect", droidWorksSed.string(), "droidworks", "native-sed", config.string() },
                  "", out, root) == 0
              && contains(out, "\"projectKind\":\"droidworks\"")
              && contains(out, "\"sectors\":1"),
            "native-inspect observes DroidWorks JKL and native SED documents");

        const fs::path motsLevel = root / "mots.jkl";
        std::string motsText(kJkl);
        const std::string archMarker = "SECTION: TEMPLATES";
        motsText.replace(motsText.find(archMarker), archMarker.size(),
            "SECTION: ARCHLIGHTING\nEND\n\nSECTION: TEMPLATES");
        writeFile(motsLevel, motsText);
        check(runAgent({ "native-inspect", motsLevel.string(), "mots", "jkl", config.string() },
                  "", out, root) == 0
              && contains(out, "\"projectKind\":\"mots\""),
            "native-inspect observes a MotS-marked JKL as mots");
        check(runAgent({ "native-inspect", motsLevel.string(), "jk", "jkl", config.string() },
                  "", out, root) == 3
              && contains(out, "\"status\":\"DocumentRejected\""),
            "native-inspect refuses a MotS-marked JKL declared as jk");

        const fs::path dwMarkedLevel = root / "dw-marked.jkl";
        std::string dwText(kJkl);
        const std::string matMarker = "SECTION: MATERIALS";
        dwText.replace(dwText.find(matMarker), matMarker.size(),
            "SECTION: COPYRIGHT\n@ Lucas Learning Limited.\nEND\n\nSECTION: MATERIALS");
        writeFile(dwMarkedLevel, dwText);
        check(runAgent({ "native-inspect", dwMarkedLevel.string(), "jk", "jkl", config.string() },
                  "", out, root) == 3
              && contains(out, "\"status\":\"DocumentRejected\""),
            "native-inspect refuses a DroidWorks-marked JKL declared as jk");

        const fs::path bad = root / "bad.jkl";
        writeFile(bad, "this is not a level\n");
        check(runAgent({ "native-inspect", bad.string(), "jk", "jkl", config.string() },
                  "", out, root) == 3
              && contains(out, "\"status\":\"DocumentRejected\""),
            "native-inspect rejected documents exit 3");
#endif

        const fs::path staleConfig = root / "worker-config-stale.json";
        writeFile(staleConfig, "{\"schemaVersion\":1,\"executableSha256\":\""
            + std::string(64, '0') + "\",\"sourceManifestSha256\":\"" + manifestSha
            + "\",\"timeoutSeconds\":10}");
        check(runAgent({ "native-inspect", level.string(), "jk", "jkl", staleConfig.string() },
                  "", out, root) == 4,
            "native-inspect refuses a stale worker identity with exit 4");

        const fs::path lonely = root / "lonely";
        fs::create_directories(lonely);
        const fs::path lonelyConfig = lonely / "worker-config.json";
        writeFile(lonelyConfig, "{\"schemaVersion\":1,\"executableSha256\":\"" + exeSha
            + "\",\"sourceManifestSha256\":\"" + manifestSha + "\",\"timeoutSeconds\":10}");
        check(runAgent({ "native-inspect", level.string(), "jk", "jkl", lonelyConfig.string() },
                  "", out, root) == 3,
            "native-inspect reports a missing worker with exit 3");
    }
    else if (scenario == "bake-lighting")
    {
        // E-17: bakeLighting resolves FULLYLIT MATs via resourceRoots.
        // No MAT files exist here: unresolvable emissives are omitted
        // (skip policy), the bake still succeeds on point lights alone.
        const fs::path resRoot = root / "res";
        fs::create_directories(resRoot);
        const fs::path patch = root / "patch.json";
        writeFile(patch, "{\"schemaVersion\":1,\"operations\":[{\"op\":\"bakeLighting\","
            "\"resourceRoots\":[\"" + resRoot.generic_string() + "\"],\"intensity\":1,\"range\":10}]}");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "bakeLighting"),
              "schema lists bakeLighting operation");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "bakeLighting plan succeeds with missing MATs omitted");
        check(!fs::exists(root / "out.jkl"), "bakeLighting plan writes nothing");
        const std::string planSha = extractSha(out, "outputSha256");
        const fs::path dest = root / "out.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string() }, "", out, root) == 0
              && fs::exists(dest), "bakeLighting apply writes new file");
        check(extractSha(out, "outputSha256") == sha256HexOf(readFile(dest))
              && extractSha(out, "outputSha256") == planSha,
              "bakeLighting apply outputSha256 matches written bytes and plan");
        std::string inspectOut;
        runAgent({ "inspect", level.string() }, "", inspectOut, root);
        const std::string sha = extractSha(inspectOut, "inputSha256");
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
              + "\",\"operations\":[{\"op\":\"bakeLighting\",\"resourceRoots\":[]}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "bakeLighting stale inputSha256 -> 4");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"bakeLighting","resourceRoots":[],"intensity":0,"range":10}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
              "bakeLighting non-positive intensity -> 2 usage");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"bakeLighting","resourceRoots":[],"bounceStrength":1,"bounceRange":10}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "bakeLighting plan succeeds with the bounce profile enabled");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"bakeLighting","resourceRoots":[],"bounceStrength":1,"bounceRange":0}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
              "bakeLighting non-positive bounceRange -> 2 usage");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"bakeLighting","resourceRoots":[],"skyIntensity":1,"skyRange":10,"skyColor":[0.25,0.5,1]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "bakeLighting plan succeeds with the sky profile enabled");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"bakeLighting","resourceRoots":[],"skyIntensity":1,"skyRange":0}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
              "bakeLighting non-positive skyRange -> 2 usage");
        (void)sha;
    }
    else if (scenario == "terrain")
    {
        // E-14: buildHeightmapTerrain patch operation.
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "buildHeightmapTerrain"),
              "schema lists buildHeightmapTerrain operation");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildHeightmapTerrain","origin":[10,10,0],"cellSize":1,"headroom":2,"columns":2,"rows":2,"heights":[0,0,0,0]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "buildHeightmapTerrain plan succeeds for a 2x2 flat grid");
        const std::string planSha = extractSha(out, "outputSha256");
        check(!fs::exists(root / "terrain.jkl"), "buildHeightmapTerrain plan writes nothing");
        const fs::path dest = root / "terrain.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), planSha }, "", out, root) == 0
              && fs::exists(dest), "buildHeightmapTerrain apply writes the planned file");
        std::string inspectOut;
        check(runAgent({ "inspect", dest.string() }, "", inspectOut, root) == 0
              && contains(inspectOut, "\"sectors\":5"),
              "buildHeightmapTerrain adds four terrain sectors");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildHeightmapTerrain","origin":[0,0,0],"cellSize":1,"headroom":2,"columns":2,"rows":2,"heights":[0,0]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidTerrainParameters"),
              "buildHeightmapTerrain heights-count mismatch -> 5 invalidTerrainParameters");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildHeightmapTerrain","origin":[0,0,0],"cellSize":1,"headroom":2,"columns":129,"rows":1,"heights":[0]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidTerrainParameters"),
              "buildHeightmapTerrain 129 columns -> 5 invalidTerrainParameters");
        std::string freshInspect;
        runAgent({ "inspect", level.string() }, "", freshInspect, root);
        (void)freshInspect;
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
              + "\",\"operations\":[{\"op\":\"buildHeightmapTerrain\",\"origin\":[0,0,0],\"cellSize\":1,\"headroom\":2,\"columns\":1,\"rows\":1,\"heights\":[0]}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "buildHeightmapTerrain stale inputSha256 -> 4");
    }
    else if (scenario == "spiral")
    {
        // E-05: buildSpiralStairs patch operation.
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "buildSpiralStairs"),
              "schema lists buildSpiralStairs operation");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSpiralStairs","center":[0,0,0],"innerRadius":0.5,"outerRadius":1.5,"startAngleDegrees":0,"stepAngleDegrees":30,"rise":0.25,"headroom":1,"steps":4}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "buildSpiralStairs plan succeeds for four steps");
        const std::string planSha = extractSha(out, "outputSha256");
        check(!fs::exists(root / "spiral.jkl"), "buildSpiralStairs plan writes nothing");
        const fs::path dest = root / "spiral.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), planSha }, "", out, root) == 0
              && fs::exists(dest), "buildSpiralStairs apply writes the planned file");
        std::string inspectOut;
        check(runAgent({ "inspect", dest.string() }, "", inspectOut, root) == 0
              && contains(inspectOut, "\"sectors\":5"),
              "buildSpiralStairs adds four wedge sectors");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSpiralStairs","center":[0,0,0],"innerRadius":0.5,"outerRadius":1.5,"startAngleDegrees":0,"stepAngleDegrees":0,"rise":0.25,"headroom":1,"steps":4}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidSpiralStairParameters"),
              "buildSpiralStairs zero step angle -> 5 invalidSpiralStairParameters");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSpiralStairs","center":[0,0,0],"innerRadius":1.5,"outerRadius":0.5,"startAngleDegrees":0,"stepAngleDegrees":30,"rise":0.25,"headroom":1,"steps":4}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidSpiralStairParameters"),
              "buildSpiralStairs inverted radii -> 5 invalidSpiralStairParameters");
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
              + "\",\"operations\":[{\"op\":\"buildSpiralStairs\",\"center\":[0,0,0],\"innerRadius\":0.5,\"outerRadius\":1.5,\"startAngleDegrees\":0,\"stepAngleDegrees\":30,\"rise\":0.25,\"headroom\":1,\"steps\":4}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "buildSpiralStairs stale inputSha256 -> 4");
    }
    else if (scenario == "terrain3do")
    {
        // E-14: terrain-3do command exports the heightmap as a 3DO model.
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "terrain-3do"),
              "schema lists terrain-3do command");
        const fs::path request = root / "terrain.json";
        writeFile(request, R"({"schemaVersion":1,"origin":[0,0,0],"cellSize":1,"headroom":2,"columns":2,"rows":2,"heights":[0,0,0,1],"material":"terrain.mat"})");
        const fs::path model = root / "terrain.3do";
        check(runAgent({ "terrain-3do", request.string(), model.string() }, "", out, root) == 0
              && fs::exists(model) && contains(out, "\"vertices\":9") && contains(out, "\"faces\":8")
              && contains(out, "\"material\":\"terrain.mat\""),
              "terrain-3do writes a 2x2 heightmap model");
        check(extractSha(out, "outputSha256") == sha256HexOf(readFile(model)),
              "terrain-3do outputSha256 matches written bytes");
        check(contains(readFile(model), "terrain.mat"), "terrain-3do file names the material");
        check(runAgent({ "terrain-3do", request.string(), model.string() }, "", out, root) == 5,
              "terrain-3do refuses an existing output -> 5");
        writeFile(request, R"({"schemaVersion":1,"origin":[0,0,0],"cellSize":1,"headroom":2,"columns":2,"rows":2,"heights":[0]})");
        const fs::path refused = root / "refused.3do";
        check(runAgent({ "terrain-3do", request.string(), refused.string() }, "", out, root) == 5
              && contains(out, "invalidTerrainParameters") && !fs::exists(refused),
              "terrain-3do heights-count mismatch -> 5 invalidTerrainParameters, no file");
        check(runAgent({ "terrain-3do", (root / "missing.json").string(), refused.string() },
                  "", out, root) == 3,
              "terrain-3do missing request -> 3");
    }
    else if (scenario == "sectorarray")
    {
        // E-03: buildSectorArray patch operation (geometry arrays).
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "buildSectorArray"),
              "schema lists buildSectorArray operation");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSectorArray","sectors":[0],"layout":{"kind":"linear","origin":[10,0,0],"step":[3,0,0],"count":3}}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "buildSectorArray plan succeeds for three copies");
        const std::string planSha = extractSha(out, "outputSha256");
        check(!fs::exists(root / "array.jkl"), "buildSectorArray plan writes nothing");
        const fs::path dest = root / "array.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), planSha }, "", out, root) == 0
              && fs::exists(dest), "buildSectorArray apply writes the planned file");
        std::string inspectOut;
        check(runAgent({ "inspect", dest.string() }, "", inspectOut, root) == 0
              && contains(inspectOut, "\"sectors\":4"),
              "buildSectorArray adds three copies to the one-sector level");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSectorArray","sectors":[0],"layout":{"kind":"linear","origin":[10,0,0],"step":[0,0,0],"count":3}}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5,
              "buildSectorArray duplicate positions -> 5");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSectorArray","sectors":[9],"layout":{"kind":"linear","origin":[10,0,0],"step":[3,0,0],"count":2}}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "sectorArrayRefused"),
              "buildSectorArray unknown sector -> 5 sectorArrayRefused");
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
              + "\",\"operations\":[{\"op\":\"buildSectorArray\",\"sectors\":[0],\"layout\":{\"kind\":\"linear\",\"origin\":[10,0,0],\"step\":[3,0,0],\"count\":2}}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "buildSectorArray stale inputSha256 -> 4");
    }
    else if (scenario == "dome")
    {
        // E-05: buildDome patch operation.
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "buildDome"),
              "schema lists buildDome operation");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildDome","center":[0,0,0],"radius":1,"headroom":1,"segments":4,"rings":2}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "buildDome plan succeeds for 4x2");
        const std::string planSha = extractSha(out, "outputSha256");
        check(!fs::exists(root / "dome.jkl"), "buildDome plan writes nothing");
        const fs::path dest = root / "dome.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), planSha }, "", out, root) == 0
              && fs::exists(dest), "buildDome apply writes the planned file");
        std::string inspectOut;
        check(runAgent({ "inspect", dest.string() }, "", inspectOut, root) == 0
              && contains(inspectOut, "\"sectors\":9"),
              "buildDome adds eight wedge sectors");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildDome","center":[0,0,0],"radius":1,"headroom":1,"segments":2,"rings":2}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidDomeParameters"),
              "buildDome two segments -> 5 invalidDomeParameters");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildDome","center":[0,0,0],"radius":2,"headroom":1,"segments":4,"rings":2}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidDomeParameters"),
              "buildDome headroom below ring rise -> 5 invalidDomeParameters");
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
              + "\",\"operations\":[{\"op\":\"buildDome\",\"center\":[0,0,0],\"radius\":1,\"headroom\":1,\"segments\":4,\"rings\":2}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "buildDome stale inputSha256 -> 4");
    }
    else if (scenario == "swept")
    {
        // E-05: buildSweptCorridor patch operation.
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "buildSweptCorridor"),
              "schema lists buildSweptCorridor operation");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSweptCorridor","center":[0,0,0],"innerRadius":1,"outerRadius":2,"startAngleDegrees":0,"sweepDegrees":90,"segments":3,"risePerSegment":0,"headroom":1}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "buildSweptCorridor plan succeeds for three segments");
        const std::string planSha = extractSha(out, "outputSha256");
        check(!fs::exists(root / "swept.jkl"), "buildSweptCorridor plan writes nothing");
        const fs::path dest = root / "swept.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), planSha }, "", out, root) == 0
              && fs::exists(dest), "buildSweptCorridor apply writes the planned file");
        std::string inspectOut;
        check(runAgent({ "inspect", dest.string() }, "", inspectOut, root) == 0
              && contains(inspectOut, "\"sectors\":4"),
              "buildSweptCorridor adds three wedge sectors");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildSweptCorridor","center":[0,0,0],"innerRadius":1,"outerRadius":2,"startAngleDegrees":0,"sweepDegrees":0,"segments":3,"risePerSegment":0,"headroom":1}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "invalidSweptCorridorParameters"),
              "buildSweptCorridor zero sweep -> 5 invalidSweptCorridorParameters");
        writeFile(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + std::string(64, '0')
              + "\",\"operations\":[{\"op\":\"buildSweptCorridor\",\"center\":[0,0,0],\"innerRadius\":1,\"outerRadius\":2,\"startAngleDegrees\":0,\"sweepDegrees\":90,\"segments\":3,\"risePerSegment\":0,\"headroom\":1}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 4,
              "buildSweptCorridor stale inputSha256 -> 4");
    }
    else if (scenario == "statistics")
    {
        // E-01: statistics command (read-only aggregation).
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "statistics"),
              "schema lists statistics command");
        check(runAgent({ "statistics", level.string() }, "", out, root) == 0
              && contains(out, "\"sectors\":1") && contains(out, "\"command\":\"statistics\"")
              && contains(out, "\"bounds\":["),
              "statistics reports the one-sector fixture with bounds");
        check(runAgent({ "statistics", (root / "missing.jkl").string() }, "", out, root) == 3,
              "statistics missing level -> 3");
        check(!fs::exists(root / "statistics.jkl"), "statistics writes nothing");
    }
    else if (scenario == "prefab")
    {
        // E-04: prefab-capture command + insertPrefab patch operation.
        writeFile(level, kOutpostJkl);
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "insertPrefab")
              && contains(out, "prefab-capture"),
              "schema lists prefab-capture and insertPrefab");
        const fs::path captureRequest = root / "capture.json";
        writeFile(captureRequest, R"({"schemaVersion":1,"sectors":[0,1],"primary":0})");
        const fs::path prefabFile = root / "rooms.prefab";
        check(runAgent({ "prefab-capture", level.string(), captureRequest.string(), prefabFile.string() },
                  "", out, root) == 0 && fs::exists(prefabFile)
              && contains(readFile(prefabFile), "RED-PREFAB 1"),
              "prefab-capture writes a versioned prefab file");
        check(runAgent({ "prefab-capture", level.string(), captureRequest.string(), prefabFile.string() },
                  "", out, root) == 5,
              "prefab-capture refuses an existing output -> 5");
        // Insert the two-room prefab into the one-sector level.
        writeFile(level, kJkl);
        const fs::path patch = root / "patch.json";
        writeFile(patch, "{\"schemaVersion\":1,\"operations\":[{\"op\":\"insertPrefab\",\"prefabFile\":\""
              + jsonPath(prefabFile) + "\"}]}");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "insertPrefab plan succeeds");
        const std::string planSha = extractSha(out, "outputSha256");
        check(!fs::exists(root / "inserted.jkl"), "insertPrefab plan writes nothing");
        const fs::path dest = root / "inserted.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), planSha }, "", out, root) == 0
              && fs::exists(dest), "insertPrefab apply writes the planned file");
        std::string inspectOut;
        check(runAgent({ "inspect", dest.string() }, "", inspectOut, root) == 0
              && contains(inspectOut, "\"sectors\":3"),
              "insertPrefab adds the two captured sectors");
        // A tampered prefab file is refused and produces no output.
        std::string tampered = readFile(prefabFile);
        const std::size_t hashAt = tampered.find("sha256 ");
        check(hashAt != std::string::npos, "prefab fixture carries a content hash");
        if (hashAt != std::string::npos)
        {
            char& digit = tampered[hashAt + 7];
            digit = digit == '0' ? '1' : '0';
        }
        const fs::path tamperedFile = root / "tampered.prefab";
        writeFile(tamperedFile, tampered);
        writeFile(patch, "{\"schemaVersion\":1,\"operations\":[{\"op\":\"insertPrefab\",\"prefabFile\":\""
              + jsonPath(tamperedFile) + "\"}]}");
        const fs::path refused = root / "refused.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), refused.string() }, "", out, root) == 5
              && contains(out, "prefabInvalidHash") && !fs::exists(refused),
              "insertPrefab refuses a tampered prefab -> 5 without output");
    }
    else if (scenario == "thingpath")
    {
        // E-09: buildThingPath patch operation (Pathfinder slice).
        std::string pathLevel = kJkl;
        const std::string oldTemplates =
            "WORLD TEMPLATES 1\nagent_template none type=actor model3d=agent.3do";
        const std::string newTemplates =
            "WORLD TEMPLATES 2\n"
            "agent_template none type=actor model3d=agent.3do\n"
            "path_template none type=actor move=path model3d=agent.3do";
        const std::string oldThings =
            "WORLD THINGS 1\n0: agent_template agent_actor 1 2 3 0 0 0 0 health=100";
        const std::string newThings =
            "WORLD THINGS 2\n"
            "0: path_template path_actor 1 2 3 0 0 0 0 health=100\n"
            "1: agent_template static_actor 3 4 5 0 0 0 0 health=50";
        const auto templatesAt = pathLevel.find(oldTemplates);
        const auto thingsAt = pathLevel.find(oldThings);
        check(templatesAt != std::string::npos && thingsAt != std::string::npos,
              "thingpath fixture anchors exist");
        if (thingsAt != std::string::npos) pathLevel.replace(thingsAt, oldThings.size(), newThings);
        if (templatesAt != std::string::npos) pathLevel.replace(templatesAt, oldTemplates.size(), newTemplates);
        writeFile(level, pathLevel);
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "buildThingPath"),
              "schema lists buildThingPath operation");
        const fs::path patch = root / "patch.json";
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildThingPath","thing":0,"waypoints":[{"position":[4,5,6],"orientation":[0,0,0]},{"position":[7,8,9],"orientation":[0,90,0]}]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 0,
              "buildThingPath plan succeeds for a path mover");
        const std::string planSha = extractSha(out, "outputSha256");
        check(!fs::exists(root / "path.jkl"), "buildThingPath plan writes nothing");
        const fs::path dest = root / "path.jkl";
        check(runAgent({ "apply", level.string(), patch.string(), dest.string(), planSha }, "", out, root) == 0
              && fs::exists(dest), "buildThingPath apply writes the planned file");
        const std::string appliedText = readFile(dest);
        check(contains(appliedText, "numframes=2")
              && contains(appliedText, "frame=(4/5/6:0/0/0)")
              && contains(appliedText, "frame=(7/8/9:0/90/0)"),
              "buildThingPath writes ordered typed frames with numframes sync");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildThingPath","thing":9,"waypoints":[{"position":[0,0,0],"orientation":[0,0,0]}]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "thingFrameTargetOutOfRange"),
              "buildThingPath unknown Thing -> 5 thingFrameTargetOutOfRange");
        writeFile(patch, R"({"schemaVersion":1,"operations":[{"op":"buildThingPath","thing":1,"waypoints":[{"position":[0,0,0],"orientation":[0,0,0]}]}]})");
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 5
              && contains(out, "thingFrameRequiresPath"),
              "buildThingPath non-path Thing -> 5 thingFrameRequiresPath");
        std::string many = "{\"schemaVersion\":1,\"operations\":[{\"op\":\"buildThingPath\",\"thing\":0,\"waypoints\":[";
        for (int i = 0; i < 257; ++i)
        {
            if (i) many += ",";
            many += "{\"position\":[0,0,0],\"orientation\":[0,0,0]}";
        }
        many += "]}]}";
        writeFile(patch, many);
        check(runAgent({ "plan", level.string(), patch.string() }, "", out, root) == 2,
              "buildThingPath over 256 waypoints -> 2 usage");
    }
    else if (scenario == "dxf")
    {
        // E-13: export-dxf + dxf-inspect commands.
        writeFile(level, kOutpostJkl);
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "export-dxf")
              && contains(out, "dxf-inspect"),
              "schema lists export-dxf and dxf-inspect commands");
        const fs::path dxfFile = root / "level.dxf";
        check(runAgent({ "export-dxf", level.string(), dxfFile.string() }, "", out, root) == 0
              && fs::exists(dxfFile) && contains(out, "\"outputSha256\":\""),
              "export-dxf writes a new DXF file with its hash");
        const std::string dxfText = readFile(dxfFile);
        check(contains(dxfText, "3DFACE") && contains(dxfText, "SECTOR_0") && contains(dxfText, "SECTOR_1"),
              "export-dxf emits sector-layered 3DFACE entities");
        check(runAgent({ "export-dxf", level.string(), dxfFile.string() }, "", out, root) == 5,
              "export-dxf refuses an existing output -> 5");
        check(runAgent({ "dxf-inspect", dxfFile.string() }, "", out, root) == 0
              && contains(out, "\"faces\":24") && contains(out, "\"triangles\":24")
              && contains(out, "\"quads\":0"),
              "dxf-inspect counts the exported triangles");
        const fs::path quadFile = root / "quad.dxf";
        writeFile(quadFile,
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n8\n0\n10\n0\n20\n0\n30\n0\n11\n1\n21\n1\n31\n1\n"
            "0\n3DFACE\n8\nWALL\n10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n"
            "12\n1\n22\n1\n32\n0\n13\n0\n23\n1\n33\n0\n"
            "0\nENDSEC\n0\nEOF\n");
        check(runAgent({ "dxf-inspect", quadFile.string() }, "", out, root) == 0
              && contains(out, "\"faces\":1") && contains(out, "\"quads\":1"),
              "dxf-inspect counts one quad and skips LINE entities");
        check(runAgent({ "dxf-inspect", (root / "missing.dxf").string() }, "", out, root) == 3,
              "dxf-inspect missing file -> 3 input error");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "dxf-import"),
              "schema lists dxf-import command");
        const fs::path imported = root / "imported.jkl";
        check(runAgent({ "dxf-import", quadFile.string(), "1", imported.string() }, "", out, root) == 0
              && fs::exists(imported) && contains(out, "\"sectors\":1")
              && contains(out, "\"skippedFaces\":0"),
              "dxf-import compiles the quad into a sector level");
        check(runAgent({ "inspect", imported.string() }, "", out, root) == 0
              && contains(out, "\"sectors\":1"),
              "dxf-import output reopens with one sector");
        check(runAgent({ "dxf-import", quadFile.string(), "1", imported.string() }, "", out, root) == 5,
              "dxf-import refuses an existing output -> 5");
        check(runAgent({ "dxf-import", quadFile.string(), "0", (root / "bad.jkl").string() },
                  "", out, root) == 5 && contains(out, "invalidDxfImportHeight"),
              "dxf-import zero height -> 5 invalidDxfImportHeight");
        check(runAgent({ "dxf-import", quadFile.string(), "abc", (root / "bad2.jkl").string() },
                  "", out, root) == 2,
              "dxf-import non-numeric height -> 2 usage");
        check(runAgent({ "dxf-import", (root / "missing.dxf").string(), "1", (root / "bad3.jkl").string() },
                  "", out, root) == 3,
              "dxf-import missing DXF -> 3 input error");
    }
    else if (scenario == "cogcheck")
    {
        // E-11: cog-check lexical preflight against measured registries.
        const fs::path cogFile = root / "door.cog";
        writeFile(cogFile,
            "symbols\nmessage startup\nend\ncode\nstartup:\n  GetCurInvWeapon(0);\n  return;\nend\n");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "cog-check"),
              "schema lists cog-check command");
        check(runAgent({ "cog-check", cogFile.string(), "jk" }, "", out, root) == 0
              && contains(out, "\"target\":\"jk\"") && contains(out, "\"measured\":true")
              && contains(out, "\"availabilityFindings\":0"),
              "cog-check reports the JK verb as available");
        check(runAgent({ "cog-check", cogFile.string(), "mots" }, "", out, root) == 0
              && contains(out, "getcurinvweapon"),
              "cog-check reports the JK-only verb as absent for MotS");
        const fs::path badCog = root / "bad.cog";
        writeFile(badCog,
            "symbols\nmessage startup\nend\ncode\nstartup:\n  TeleportPlayerHome(0);\n  return;\nend\n");
        check(runAgent({ "cog-check", badCog.string(), "jk", "--strict" }, "", out, root) == 5
              && contains(out, "cogPreflightFindings"),
              "cog-check strict invented verb -> 5 cogPreflightFindings");
        check(runAgent({ "cog-check", badCog.string(), "jk" }, "", out, root) == 0
              && contains(out, "teleportplayerhome"),
              "cog-check non-strict reports the invented verb without failing");
        check(runAgent({ "cog-check", cogFile.string(), "quake" }, "", out, root) == 2,
              "cog-check unknown target -> 2 usage");
        check(runAgent({ "cog-check", (root / "missing.cog").string(), "jk" }, "", out, root) == 3,
              "cog-check missing file -> 3 input error");
    }
    else if (scenario == "matbridge")
    {
        // E-18: mat-inspect / mat-export-png over a synthetic 2x2 MAT.
        std::string mat = "MAT ";
        const auto pushI32 = [&mat](const std::int32_t v) {
            const std::uint32_t u = static_cast<std::uint32_t>(v);
            for (int shift = 0; shift < 32; shift += 8)
                mat.push_back(static_cast<char>((u >> shift) & 0xFFu));
        };
        pushI32(0x32); pushI32(2); pushI32(1); pushI32(1);
        pushI32(0); pushI32(8); // ColorInfo: Indexed, 8 bpp
        for (int i = 0; i < 12; ++i) pushI32(0);
        pushI32(8); pushI32(37); // sampled texture, solid colour index
        for (int i = 0; i < 8; ++i) pushI32(0);
        pushI32(2); pushI32(2); pushI32(0); pushI32(0); pushI32(0); pushI32(1);
        mat.push_back('\0'); mat.push_back('\1'); mat.push_back('\2'); mat.push_back('\3');
        const fs::path matFile = root / "tex.mat";
        writeFile(matFile, mat);
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "mat-inspect")
              && contains(out, "mat-export-png"),
              "schema lists mat-inspect and mat-export-png commands");
        check(runAgent({ "mat-inspect", matFile.string() }, "", out, root) == 0
              && contains(out, "\"celCount\":1") && contains(out, "\"bitsPerPixel\":8")
              && contains(out, "\"width\":2") && contains(out, "\"height\":2"),
              "mat-inspect reports the MAT cel layout");
        const fs::path pngFile = root / "tex.png";
        check(runAgent({ "mat-export-png", matFile.string(), pngFile.string() }, "", out, root) == 0
              && fs::exists(pngFile) && contains(out, "\"width\":2") && contains(out, "\"height\":2")
              && readFile(pngFile).starts_with("\x89PNG"),
              "mat-export-png writes a PNG of the decoded cel");
        check(runAgent({ "mat-export-png", matFile.string(), pngFile.string() }, "", out, root) == 5,
              "mat-export-png refuses an existing output -> 5");
        check(runAgent({ "mat-export-png", matFile.string(), (root / "x.png").string(), "4" },
                  "", out, root) == 5 && contains(out, "matCelOutOfRange"),
              "mat-export-png cel past the cel list -> 5 matCelOutOfRange");
        check(runAgent({ "mat-inspect", (root / "missing.mat").string() }, "", out, root) == 3,
              "mat-inspect missing file -> 3 input error");
    }
    else if (scenario == "key")
    {
        // E-12: key-plan / key-apply commands over a standalone KEY file.
        const fs::path keyFile = root / "anim.key";
        writeFile(keyFile,
            "SECTION: HEADER\nFLAGS 16\nTYPE 0x3\nFRAMES 100\nFPS 29.97\nJOINTS 4\n"
            "SECTION: MARKERS\nMARKERS 1\n2.5 7\n"
            "SECTION: KEYFRAME\nNODES 1\nNODE 3\nMESH NAME mesh0\nENTRIES 1\n"
            "0: 0.25 0x1 1 2 3 10 20 30\n0.1 0.2 0.3 1 2 3\n");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "key-plan")
              && contains(out, "key-apply"),
              "schema lists key-plan and key-apply commands");
        const fs::path request = root / "key.json";
        writeFile(request, R"({"schemaVersion":1,"operations":[{"op":"insertEntry","node":0,"entry":{"frame":10.5,"flags":0,"position":[4,5,6],"rotation":[0,0,0]}}]})");
        check(runAgent({ "key-plan", keyFile.string(), request.string() }, "", out, root) == 0
              && contains(out, "\"outputSha256\":\""),
              "key-plan succeeds and reports the output hash");
        const std::string planSha = extractSha(out, "outputSha256");
        check(planSha.size() == 64 && !fs::exists(root / "out.key"), "key-plan writes nothing");
        const fs::path dest = root / "out.key";
        check(runAgent({ "key-apply", keyFile.string(), request.string(), planSha, dest.string() },
                  "", out, root) == 0 && fs::exists(dest),
              "key-apply writes the planned KEY file");
        check(sha256HexOf(readFile(dest)) == planSha,
              "key-apply output bytes match the planned hash");
        const std::string written = readFile(dest);
        check(contains(written, "ENTRIES 2")
              && written.find("0: 0.25 ") < written.find("1: 10.5 ")
              && contains(written, "JOINTS 4") && contains(written, "NODE 3")
              && contains(written, "2.5 7"),
              "key-apply retains sparse node IDs, markers and fractional frames");
        writeFile(request, R"({"schemaVersion":1,"operations":[{"op":"insertEntry","node":0,"entry":{"label":"FRAME","frame":101,"flags":0,"position":[0,0,0],"rotation":[0,0,0]}}]})");
        const fs::path refused = root / "refused.key";
        check(runAgent({ "key-apply", keyFile.string(), request.string(), std::string(64, '0'), refused.string() },
                  "", out, root) == 5 && contains(out, "keyFrameOutOfRange") && !fs::exists(refused),
              "key-apply frame past the frame count -> 5 without output");
        writeFile(request, R"({"schemaVersion":1,"operations":[{"op":"insertEntry","node":7,"entry":{"label":"FRAME","frame":1,"flags":0,"position":[0,0,0],"rotation":[0,0,0]}}]})");
        check(runAgent({ "key-plan", keyFile.string(), request.string() }, "", out, root) == 5
              && contains(out, "keyNodeOutOfRange"),
              "key-plan unknown node -> 5 keyNodeOutOfRange");
        writeFile(request, R"({"schemaVersion":1,"operations":[{"op":"insertEntry","node":0,"entry":{"label":"FRAME","frame":10,"flags":0,"position":[4,5,6],"rotation":[0,0,0]}}]})");
        std::string existingPlan;
        runAgent({ "key-plan", keyFile.string(), request.string() }, "", existingPlan, root);
        check(runAgent({ "key-apply", keyFile.string(), request.string(),
                         extractSha(existingPlan, "outputSha256"), dest.string() },
                  "", out, root) == 5,
              "key-apply refuses an existing output -> 5");
        check(runAgent({ "key-plan", (root / "missing.key").string(), request.string() }, "", out, root) == 3,
              "key-plan missing input -> 3 input error");
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "key-inspect"),
              "schema lists key-inspect command");
        check(runAgent({ "key-inspect", keyFile.string() }, "", out, root) == 0
              && contains(out, "\"nodeCount\":1") && contains(out, "\"frames\":100")
              && contains(out, "\"fps\":29.97") && contains(out, "\"meshName\":\"mesh0\"")
              && contains(out, "\"entries\":1") && contains(out, "\"firstFrame\":0.25")
              && contains(out, "\"nodeIndex\":3") && contains(out, "\"joints\":4"),
              "key-inspect reports the KEY timeline inventory");
        writeFile(request, R"({"schemaVersion":1,"operations":[{"op":"insertEntry","node":0,"entry":{"frame":1,"flags":-1,"position":[0,0,0],"rotation":[0,0,0]}}]})");
        check(runAgent({ "key-plan", keyFile.string(), request.string() }, "", out, root) == 2,
              "key-plan refuses flags outside the unsigned 32-bit range");
        writeFile(keyFile, readFile(keyFile) + "SECTION: UNSUPPORTED\nopaque data\n");
        writeFile(request, R"({"schemaVersion":1,"operations":[{"op":"sortNode","node":0}]})");
        check(runAgent({ "key-plan", keyFile.string(), request.string() }, "", out, root) == 5
              && contains(out, "keySerializationUnsupported"),
              "key-plan refuses to discard unsupported imported sections");
        check(runAgent({ "key-inspect", keyFile.string(),
                         "src/core/tests/assets/fixtures/f04_diamond.3do" }, "", out, root) == 0
              && contains(out, "\"modelNodes\":") && contains(out, "\"unmatchedKeyNodes\":[\"mesh0\"]"),
              "key-inspect reports KEY nodes unmatched in the 3DO");
        check(runAgent({ "key-inspect", (root / "missing.key").string() }, "", out, root) == 3,
              "key-inspect missing input -> 3 input error");
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
