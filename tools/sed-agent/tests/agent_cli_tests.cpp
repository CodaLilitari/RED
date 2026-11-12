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
#include "model_lighting_fixture.hpp"

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
// A switch-style script: references plus typed values, one without a default.
const char* kValueCog =
    "symbols\nmessage activated\nthing door\nsurface button linkid=2\nflex speed=8.0\n"
    "flex wait\nsound click=lgclick1.wav local\nsound beep=set_hi2.wav\nend\ncode\nactivated:\n return;\nend\n";

// Every typed kind the strict value rules cover. The engine reads the int
// default 0x10 as 0 (atoi), and ignores a vector's initializer.
const char* kTypedCog =
    "symbols\nmessage activated\nint count=0x10\nint lives=3\nvector dir=(1/2/3)\nflex f=1.5\n"
    "sound s=set_hi2.wav\nend\ncode\nactivated:\n return;\nend\n";

// The default `red-agent new` room saved by RED's core JED writer.
const char* kScaffoldJed = R"JED(SED 0.3.0

SEC: HEADER
HDR 1 4.000000 15.000000 2.000000 2.000000 - 320.000000
HOR 100.000000 768.000000 0.000000 0.000000
CEIL 0.000000 0.000000
MIP 1.000000 2.000000 3.000000 4.000000
LOD 0.300000 0.600000 0.900000 1.200000
END

SEC: EXPORTBUFFERS
SOUNDS 0
MATERIALS 0
AICLASSES 64
MODELS 1024
SPRITES 128
KEYFRAMES 512
PUPPETS 64
SOUNDCLASSES 128
COGSCRIPTS 128
COGS 0
TEMPLATES 0
THINGS 256
END

SEC: GEO
SECS 1
scaffold.cmp - 0.000000 0x1000 0.250000 0.000000 1.000000 1.000000 1.000000 0
VXS 8
-1.000000 -1.000000 -1.000000
1.000000 -1.000000 -1.000000
1.000000 1.000000 -1.000000
-1.000000 1.000000 -1.000000
-1.000000 1.000000 1.000000
1.000000 1.000000 1.000000
1.000000 -1.000000 1.000000
-1.000000 -1.000000 1.000000
SURFS 6
scaffold.mat 0x5 0x80000000 4 3 1 0.000000 4 -1 -1 0x0 1.000000 1.000000
 0 0.00 0.00 1.0000 1 640.00 0.00 1.0000 2 640.00 -640.00 1.0000 3 0.00 -640.00 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000
scaffold.mat 0x4 0x80000000 4 3 1 0.000000 4 -1 -1 0x0 1.000000 1.000000
 4 0.00 0.00 1.0000 5 640.00 0.00 1.0000 6 640.00 -640.00 1.0000 7 0.00 -640.00 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000
scaffold.mat 0x4 0x80000000 4 3 1 0.000000 4 -1 -1 0x0 1.000000 1.000000
 0 0.00 0.00 1.0000 3 640.00 0.00 1.0000 4 640.00 -640.00 1.0000 7 0.00 -640.00 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000
scaffold.mat 0x4 0x80000000 4 3 1 0.000000 4 -1 -1 0x0 1.000000 1.000000
 1 0.00 0.00 1.0000 0 640.00 0.00 1.0000 7 640.00 -640.00 1.0000 6 0.00 -640.00 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000
scaffold.mat 0x4 0x80000000 4 3 1 0.000000 4 -1 -1 0x0 1.000000 1.000000
 2 0.00 0.00 1.0000 1 640.00 0.00 1.0000 6 640.00 -640.00 1.0000 5 0.00 -640.00 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000
scaffold.mat 0x4 0x80000000 4 3 1 0.000000 4 -1 -1 0x0 1.000000 1.000000
 3 0.00 0.00 1.0000 2 640.00 0.00 1.0000 5 640.00 -640.00 1.0000 4 0.00 -640.00 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000 1.0000
END

SEC: THINGS
THS 0
END

SEC: LIGHTS
LTS 0
END

SEC: COGS
COGS 0
END

SEC: LAYERS
LAYERS 0
END

SEC: LVIS

END

SEC: HIERARCHYDEF
NODES 0
END
)JED";

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
    else if (scenario == "validate-strict")
    {
        const auto original = readFile(level);
        check(runAgent({ "validate", level.string(), "--strict" }, "", out, root) == 0
              && contains(out, "\"valid\":true")
              && contains(out, "\"rawStructureStatus\":\"checked\""),
              "strict validation accepts complete synthetic JKL");
        check(runAgent({ "validate", "--strict", level.string() }, "", out, root) == 0
              && contains(out, "\"valid\":true"), "strict option may precede the path");

        std::string missingEnd = original;
        const auto section = missingEnd.find("SECTION: MATERIALS");
        const auto terminator = missingEnd.find("END", section);
        check(terminator != std::string::npos, "strict fixture contains material terminator");
        if (terminator != std::string::npos) missingEnd.erase(terminator, 3);
        const fs::path incomplete = root / "missing-material-end.jkl";
        writeFile(incomplete, missingEnd);
        check(runAgent({ "validate", incomplete.string() }, "", out, root) == 0,
              "compatibility validation still accepts missing redundant section terminator");
        check(runAgent({ "validate", incomplete.string(), "--strict" }, "", out, root) == 0
              && contains(out, "\"valid\":false")
              && contains(out, "\"code\":\"missing-terminator\"")
              && contains(out, "\"section\":\"MATERIALS\"")
              && contains(out, "\"line\":"),
              "strict validation reports raw material framing with line evidence");
        check(runAgent({ "validate", incomplete.string(), "-" },
                       R"({"schemaVersion":1,"strict":true})", out, root) == 0
              && contains(out, "\"valid\":false")
              && contains(out, "\"code\":\"missing-terminator\""),
              "strict JSON request supports MCP without another domain path");
        check(runAgent({ "validate", level.string(), "-" },
                       R"({"schemaVersion":1,"strict":"true"})", out, root) == 2,
              "strict request rejects non-boolean option");
        check(runAgent({ "validate", level.string(), "--strict", "--strict" }, "", out, root) == 2,
              "strict option cannot be repeated");
        const fs::path native = root / "strict-native.sed";
        check(runAgent({ "convert", level.string(), native.string() }, "", out, root) == 0,
              "strict native fixture converts");
        check(runAgent({ "validate", native.string(), "--strict" }, "", out, root) == 2
              && contains(out, "strict"), "raw JKL strict mode refuses native format");
        check(readFile(level) == original && readFile(incomplete) == missingEnd,
              "strict validation never rewrites input bytes");
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
        check(contains(out, "\"seamStatus\":\"complete\""),
              "validate reports whether bounded seam inspection completed");
        check(runAgent({ "validate", outpost.string(), "-" },
                       R"({"seamLimits":{"maxSurfacePairs":0}})", out, root) == 0
              && contains(out, "\"valid\":false")
              && contains(out, "\"seamStatus\":\"budget-exceeded\""),
              "explicit exhausted seam budget never reports a clean level");
        for ( const auto value : { "-1", "32000001", "1.5", "\"bad\"" } )
        {
            check(runAgent({ "validate", outpost.string(), "-" },
                           "{\"seamLimits\":{\"maxSurfacePairs\":" + std::string(value) + "}}", out, root) == 2,
                  "invalid seam budget is a usage error");
        }
        for ( const auto body : { R"({"seamLimits":0})", R"({"seamLimits":{"unknown":1}})",
                                 R"({"seamLimits":{"maxEdgePairs":256000001}})" } )
        {
            check(runAgent({ "validate", outpost.string(), "-" }, body, out, root) == 2,
                  "seam work configuration rejects unsupported shape, key and upper bound");
        }
        check(runAgent({ "validate", outpost.string(), "-" },
                       R"({"seamLimits":{"maxEdgePairs":0}})", out, root) == 0
              && contains(out, "\"seamStatus\":\"budget-exceeded\""),
              "zero geometric-work budget produces explicit incomplete result");
        check(runAgent({ "validate", outpost.string(), "-" },
                       R"({"seamLimits":{"maxSurfacePairs":32000000,"maxEdgePairs":256000000}})", out, root) == 0
              && contains(out, "\"seamStatus\":\"complete\""),
              "documented maximum seam work bounds are inclusive");

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
        check(contains(out, "\"edgeContacts\":[{")
              && contains(out, "\"insertionCandidates\":[{")
              && contains(out, "\"classification\":\"exact-on-edge\"")
              && contains(out, "\"repairAuthorized\":false"),
              "validate names seam edges and candidates without claiming safe mutation");

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
        // An exactly collinear fan is what an edge vertex beside the fan
        // origin produces, in shipped levels and after every cleave. It is
        // reported exactly, as a warning that does not decide validity.
        check(runAgent({ "validate", collinearPath.string() }, "", out, root) == 0
              && contains(out, "\"code\":\"fan-degeneracy\"")
              && contains(out, "\"kind\":\"collinear-corners\"")
              && contains(out, "\"fanIndex\":2")
              && contains(out, "\"sector\":0,\"severity\":\"warning\",\"surface\":5")
              && !contains(out, "\"kind\":\"coincident-corners\""),
              "validate collinear fan corners -> exact fan finding reported as a warning");

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
                                { "cog\\broken.cog", kBrokenCog },
                                { "cog\\valued.cog", kValueCog },
                                { "cog\\typed.cog", kTypedCog } }), "cog gob fixture written");
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

        // Typed values: flex/sound symbols take a value or the declared
        // default; references stay explicit; a surface may be named by
        // sector and surface.
        const std::string refs = "{\"symbol\":\"door\",\"kind\":\"thing\",\"targetIndex\":0},"
                                 "{\"symbol\":\"button\",\"kind\":\"surface\",\"sector\":0,\"surface\":0}";
        check(addCog("{\"schemaVersion\":1,\"script\":\"valued.cog\"," + archives
                     + ",\"bindings\":[" + refs + ",{\"symbol\":\"speed\",\"kind\":\"flex\",\"value\":4},"
                     "{\"symbol\":\"wait\",\"kind\":\"flex\",\"value\":2.5},"
                     "{\"symbol\":\"beep\",\"kind\":\"sound\"}]}", {}) == 5
              && contains(out, "unsupportedCogBindingType") && contains(out, "beep"),
              "addCog refuses a typed symbol given neither a value nor its default");
        check(addCog("{\"schemaVersion\":1,\"script\":\"valued.cog\",\"defaults\":true," + archives
                     + ",\"bindings\":[" + refs + "]}", {}) == 5 && contains(out, "cogSymbolNoDefault")
              && contains(out, "wait"), "addCog defaults:true still refuses a symbol with no declared default");
        check(addCog("{\"schemaVersion\":1,\"script\":\"valued.cog\",\"defaults\":true," + archives
                     + ",\"bindings\":[" + refs + ",{\"symbol\":\"wait\",\"kind\":\"flex\",\"value\":\"abc\"}]}",
                     {}) == 5 && contains(out, "invalidCogValue"), "addCog refuses a value its type cannot parse");
        check(addCog("{\"schemaVersion\":1,\"script\":\"valued.cog\",\"defaults\":true," + archives
                     + ",\"bindings\":[{\"symbol\":\"door\",\"kind\":\"thing\",\"targetIndex\":0,\"value\":1},"
                     "{\"symbol\":\"button\",\"kind\":\"surface\",\"targetIndex\":0},"
                     "{\"symbol\":\"wait\",\"kind\":\"flex\",\"value\":2.5}]}", {}) == 2,
              "addCog refuses a binding with both a target and a value");
        const fs::path valued = root / "valued.jkl";
        check(addCog("{\"schemaVersion\":1,\"script\":\"valued.cog\",\"defaults\":true," + archives
                     + ",\"bindings\":[" + refs + ",{\"symbol\":\"wait\",\"kind\":\"flex\",\"value\":2.5}]}",
                     { valued.string() }) == 0 && fs::exists(valued), "addCog with typed values and defaults applies");
        check(contains(readFile(valued), "valued.cog\t0\t0\t8\t2.5\tset_hi2.wav"),
              "typed values are written in declaration order: default speed, given wait, default sound");
        // setCogValue changes one typed value of the placed COG in place.
        {
            const std::string body = "{\"schemaVersion\":1," + archives
                + ",\"operations\":[{\"op\":\"setCogValue\",\"cog\":1,\"symbol\":\"wait\",\"value\":12}]}";
            const fs::path changed = root / "valued-wait.jkl";
            check(runAgent({ "apply", valued.string(), "-", changed.string() }, body, out, root) == 0
                  && contains(readFile(changed), "valued.cog\t0\t0\t8\t12\tset_hi2.wav"),
                  "setCogValue changes one typed value and keeps the others");
            const std::string back = "{\"schemaVersion\":1," + archives
                + ",\"operations\":[{\"op\":\"setCogValue\",\"cog\":1,\"symbol\":\"speed\",\"default\":true}]}";
            check(runAgent({ "plan", changed.string(), "-" }, back, out, root) == 0,
                  "setCogValue can restore a declared default");
            const std::string ref = "{\"schemaVersion\":1," + archives
                + ",\"operations\":[{\"op\":\"setCogValue\",\"cog\":1,\"symbol\":\"door\",\"value\":3}]}";
            check(runAgent({ "plan", changed.string(), "-" }, ref, out, root) == 5 && contains(out, "rebindCogReference"),
                  "setCogValue refuses a reference symbol and points to rebindCogReference");
            const std::string bad = "{\"schemaVersion\":1," + archives
                + ",\"operations\":[{\"op\":\"setCogValue\",\"cog\":1,\"symbol\":\"wait\",\"value\":\"soon\"}]}";
            check(runAgent({ "plan", changed.string(), "-" }, bad, out, root) == 5 && contains(out, "invalidCogValue"),
                  "setCogValue refuses a value its type cannot parse");
            const std::string nodefault = "{\"schemaVersion\":1," + archives
                + ",\"operations\":[{\"op\":\"setCogValue\",\"cog\":1,\"symbol\":\"wait\",\"default\":true}]}";
            check(runAgent({ "plan", changed.string(), "-" }, nodefault, out, root) == 5 && contains(out, "cogSymbolNoDefault"),
                  "setCogValue refuses default for a symbol that declares none");
        }
        // Strict typed values: what is stored is what the engine will read.
        {
            auto typed = [&](const std::string& count, const std::string& lives, const std::string& dir,
                             const std::string& f, const std::string& snd, const std::vector<std::string>& extra) {
                return addCog("{\"schemaVersion\":1,\"script\":\"typed.cog\"," + archives + ",\"bindings\":["
                    "{\"symbol\":\"count\"," + count + "},{\"symbol\":\"lives\"," + lives + "},"
                    "{\"symbol\":\"dir\"," + dir + "},{\"symbol\":\"f\"," + f + "},{\"symbol\":\"s\"," + snd + "}]}", extra);
            };
            const std::string countHex = "\"kind\":\"int\",\"value\":\"0x10\"";
            const std::string livesDefault = "\"kind\":\"int\",\"default\":true";
            const std::string dirValue = "\"kind\":\"vector\",\"value\":\"(1/2/3)\"";
            const std::string fFloat = "\"kind\":\"float\",\"value\":2";
            const std::string sDefault = "\"kind\":\"sound\",\"default\":true";
            const fs::path typedOut = root / "typed.jkl";
            check(typed(countHex, livesDefault, dirValue, fFloat, sDefault, { typedOut.string() }) == 0
                  && contains(readFile(typedOut), "typed.cog\t16\t3\t(1/2/3)\t2\tset_hi2.wav"),
                  "typed values: a hex int value is stored as decimal, flex and float are interchangeable");
            check(typed("\"kind\":\"int\",\"default\":true", livesDefault, dirValue, fFloat, sDefault, {}) == 5
                  && contains(out, "invalidCogValue") && contains(out, "reads as 0"),
                  "a hex int default is refused: the engine reads it as 0");
            check(typed("\"kind\":\"int\",\"value\":1.5", livesDefault, dirValue, fFloat, sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "an int refuses a fraction");
            check(typed("\"kind\":\"int\",\"value\":\"12abc\"", livesDefault, dirValue, fFloat, sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "an int refuses trailing junk atoi would drop");
            check(typed("\"kind\":\"int\",\"value\":3000000000", livesDefault, dirValue, fFloat, sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "an int refuses a value outside 32 bits");
            const fs::path bigOut = root / "typed-big.jkl";
            check(typed("\"kind\":\"int\",\"value\":2000000000", livesDefault, dirValue, fFloat, sDefault,
                        { bigOut.string() }) == 0
                  && contains(readFile(bigOut), "typed.cog\t2000000000\t3\t"),
                  "an integral JSON number is written exactly");
            check(typed(countHex, livesDefault, "\"kind\":\"vector\",\"value\":\"(1/2)\"", fFloat, sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "a vector needs three components");
            check(typed(countHex, livesDefault, "\"kind\":\"vector\",\"value\":\"(1/2/x)\"", fFloat, sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "a vector refuses a component that is not a number");
            const fs::path vecOut = root / "typed-vec.jkl";
            check(typed(countHex, livesDefault, "\"kind\":\"vector\",\"default\":true", fFloat, sDefault,
                        { vecOut.string() }) == 0
                  && contains(readFile(vecOut), "typed.cog\t16\t3\t(0/0/0)\t2\t"),
                  "a vector default is (0/0/0), as the engine ignores vector initializers");
            check(typed(countHex, livesDefault, dirValue, "\"kind\":\"flex\",\"value\":\"nan\"", sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "a flex refuses nan");
            check(typed(countHex, livesDefault, dirValue, "\"kind\":\"flex\",\"value\":\"1e39\"", sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "a flex refuses a value beyond float range");
            check(typed(countHex, livesDefault, dirValue, "\"kind\":\"flex\",\"value\":\"0x1p3\"", sDefault, {}) == 5
                  && contains(out, "invalidCogValue"), "a flex refuses a hex float");
            check(typed(countHex, livesDefault, dirValue, fFloat, "\"kind\":\"sound\",\"value\":\"a,b.wav\"", {}) == 5
                  && contains(out, "invalidCogValue"), "a resource name refuses a comma");
            check(typed(countHex, livesDefault, dirValue, fFloat, "\"kind\":\"sound\",\"value\":\"x=y.wav\"", {}) == 5
                  && contains(out, "invalidCogValue"), "a resource name refuses '='");
            check(typed(countHex, livesDefault, dirValue, fFloat,
                        "\"kind\":\"sound\",\"value\":\"" + std::string(32, 'a') + "\"", {}) == 5
                  && contains(out, "invalidCogValue"), "a value longer than 31 characters is refused");
            check(typed("\"kind\":\"flex\",\"value\":4", livesDefault, dirValue, fFloat, sDefault, {}) == 5
                  && contains(out, "cogSymbolMismatch"), "a binding kind that differs from the declaration is refused");
            check(typed("\"kind\":\"widget\",\"value\":4", livesDefault, dirValue, fFloat, sDefault, {}) == 2,
                  "an unknown binding kind is a usage error");
        }
        check(addCog("{\"schemaVersion\":1,\"script\":\"valued.cog\"," + archives
                     + ",\"bindings\":[{\"symbol\":\"door\",\"kind\":\"thing\",\"targetIndex\":0},"
                     "{\"symbol\":\"button\",\"kind\":\"surface\",\"sector\":0,\"surface\":99},"
                     "{\"symbol\":\"speed\",\"kind\":\"flex\",\"default\":true},"
                     "{\"symbol\":\"wait\",\"kind\":\"flex\",\"value\":1},"
                     "{\"symbol\":\"beep\",\"kind\":\"sound\",\"default\":true}]}", {}) == 5
              && contains(out, "cogTargetOutOfRange"), "addCog refuses a sector/surface pair that does not exist");

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
    else if (scenario == "model-import-normals")
    {
        const fs::path obj = root / "hard-edge.obj";
        const std::string source = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
            "vt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nvn 0 1 0\nusemtl fixture\n"
            "f 1/1/1 2/2/1 3/3/1\nf 1/1/2 4/3/2 2/2/2\n";
        writeFile(obj, source);
        std::string objPath = obj.string();
        std::replace(objPath.begin(), objPath.end(), '\\', '/');
        const std::string base = "{\"schemaVersion\":1,\"source\":{\"path\":\"" + objPath
            + "\",\"sha256\":\"" + sha256HexOf(source)
            + R"("},"materials":[{"name":"fixture","mat":"fixture.mat","size":[16,16]}],"upAxis":"z","origin":"none")";
        check(runAgent({ "model-import", "-", (root / "preserve.3do").string() }, base + "}", out, root) == 0
            && contains(out, "\"normalPolicy\":\"preserve\"") && contains(out, "\"normalSplitVertices\":2")
            && contains(out, "\"vertices\":6"), "model import preserves authored hard edges by default");
        check(runAgent({ "model-import", "-", (root / "smooth.3do").string() },
            base + ",\"normalPolicy\":\"recalculate\"}", out, root) == 0
            && contains(out, "\"normalPolicy\":\"recalculate\"") && contains(out, "\"normalSplitVertices\":0")
            && contains(out, "\"vertices\":4"), "model import exposes explicit normal recalculation");
        check(runAgent({ "model-import", "-", (root / "invalid.3do").string() },
            base + ",\"normalPolicy\":\"guess\"}", out, root) == 2
            && !fs::exists(root / "invalid.3do"), "unknown import normal policy refuses before output");
    }
    else if (scenario == "model-lighting")
    {
        const fs::path model = root / "fixture.3do";
        const fs::path result = root / "lit.3do";
        const std::string source(kModelLightingFixture);
        writeFile(model, source);
        const std::string sourceHash = sha256HexOf(source);
        std::string expected = source;
        for (const auto& values : std::vector<std::pair<std::string, std::string>>{
            { "0.25 0.25 0.25 0.375", "0.625 0.625 0.625 0.375" },
            { "0.25 0.25 0.25 0.625", "0.125 0.125 0.125 0.625" },
            { "0.25 0.25 0.25 0.875", "0.125 0.125 0.125 0.875" } })
        {
            expected.replace(expected.find(values.first), values.first.size(), values.second);
        }
        const std::string expectedHash = sha256HexOf(expected);
        const std::string request = "{\"schemaVersion\":1,\"inputSha256\":\"" + sourceHash
            + R"(","profile":"jk","ambient":[0.125,0.125,0.125],"lights":[{"type":"directional","directionToLight":[0,0,1],"intensity":0.5}]})";
        check(runAgent({ "model-lighting-inspect", model.string() }, "", out, root) == 0
            && extractSha(out, "inputSha256") == sourceHash
            && contains(out, "\"geosets\":1") && contains(out, "\"vertices\":3"),
            "model lighting inspector identifies exact source and all-LOD counts");
        check(runAgent({ "schema" }, "", out, root) == 0
            && contains(out, "model-lighting-plan") && contains(out, "modelLightingRequestSchema")
            && contains(out, "directionToLight"), "model lighting is discoverable with a precise request schema");
        check(runAgent({ "model-lighting-plan", model.string(), "-" }, request, out, root) == 0
            && extractSha(out, "outputSha256") == expectedHash && contains(out, "\"changedVertices\":3"),
            "model lighting plan matches analytic Lambert values and exact preserved bytes");
        check(!fs::exists(result) && readFile(model) == source, "model lighting plan publishes nothing");
        check(runAgent({ "model-lighting-apply", model.string(), "-", std::string(64, '0'), result.string() },
            request, out, root) == 4 && !fs::exists(result), "wrong reviewed model output refuses without publication");
        std::string stale = request;
        stale.replace(stale.find(sourceHash), sourceHash.size(), std::string(64, '0'));
        check(runAgent({ "model-lighting-apply", model.string(), "-", expectedHash, result.string() },
            stale, out, root) == 4 && !fs::exists(result), "stale model input hash refuses without publication");
        check(runAgent({ "model-lighting-apply", model.string(), "-", expectedHash, result.string() },
            request, out, root) == 0 && readFile(result) == expected && readFile(model) == source,
            "model lighting apply publishes only reviewed light tokens and preserves source");
        check(runAgent({ "model-lighting-apply", model.string(), "-", expectedHash, result.string() },
            request, out, root) == 5 && readFile(result) == expected, "model lighting cannot replace an existing output");
        std::string repeat = request;
        repeat.replace(repeat.find(sourceHash), sourceHash.size(), expectedHash);
        check(runAgent({ "model-lighting-plan", result.string(), "-" }, repeat, out, root) == 0
            && extractSha(out, "outputSha256") == expectedHash && contains(out, "\"changedVertices\":0"),
            "replacement model bake is repeatable without cumulative brightening");
        std::string wrongField = request;
        wrongField.insert(1, "\"selfOcclussion\":true,");
        check(runAgent({ "model-lighting-plan", model.string(), "-" }, wrongField, out, root) == 2,
            "model lighting rejects misspelled request fields");
        check(runAgent({ "model-lighting-plan", model.string(), "-" },
            R"({"schemaVersion":1,"profile":"jk","lights":[]})", out, root) == 2,
            "model lighting requires a bound input hash");
        std::string wrongProfile = request;
        wrongProfile.replace(wrongProfile.find("\"jk\""), 4, "\"ijim\"");
        check(runAgent({ "model-lighting-plan", model.string(), "-" }, wrongProfile, out, root) == 5,
            "model lighting cannot export IJIM RGB into a classic file");
        std::string invalid = request;
        invalid.replace(invalid.find("0,0,1"), 5, "0,0,0");
        check(runAgent({ "model-lighting-plan", model.string(), "-" }, invalid, out, root) == 2,
            "zero directional light vector is an invalid request");
        std::string bounded = request;
        bounded.insert(1, "\"limits\":{\"workUnits\":1},");
        check(runAgent({ "model-lighting-plan", model.string(), "-" }, bounded, out, root) == 5,
            "model lighting honors lowered work budgets");
        std::string directColor = source;
        directColor.replace(directColor.find("LIGHTINGMODE 3"), 14, "LIGHTINGMODE -1");
        const fs::path directModel = root / "direct-color.3do";
        writeFile(directModel, directColor);
        check(runAgent({ "model-lighting-inspect", directModel.string() }, "", out, root) == 0
            && contains(out, "\"mode\":-1") && !contains(out, "modes below Gouraud bypass")
            && contains(out, "VertexDiffuseOnly"),
            "mode -1 inspection identifies direct authored color instead of misclassifying every lower mode");
    }
    else if (scenario == "schema-inventory")
    {
        // Every patch operation the agent dispatches is listed by `schema`,
        // so a discovery-driven agent can find it.
        check(runAgent({ "schema" }, "", out, root) == 0, "schema succeeds");
        for (const char* op : { "addBox", "extrudeSurface", "adjoinSurfaces", "cleaveSurface", "cleaveSector",
                 "translateSectors", "scaleSectors", "rotateSectors", "translateThings", "translateLights",
                 "registerMaterial", "registerColormap", "renameMaterial", "renameColormap",
                 "setSurfaceMaterial", "setSectorColormap", "setSurfaceRenderModes", "setSurfaceUv",
                 "setVertexLight", "setGravity", "setLevelHeader", "importTemplates", "defineTemplate", "addThing",
                 "setThingFrames", "setSectorAmbient", "setSectorFlags", "setLightRgb", "addCog", "makeDoor",
                 "batchUvProject", "batchUvCylindrical", "batchUvSpherical", "batchUvTransform",
                 "placeThingArray", "placeLightArray", "applyMaskedSurfaceRecipe", "bakeLighting",
                 "buildHeightmapTerrain", "buildSpiralStairs", "buildStairs", "buildSwitchbackStairs",
                 "buildSectorArray", "buildDome", "buildSweptCorridor", "buildThingPath", "insertPrefab",
                 "rebindCogReference", "setCogValue", "addLight", "setLight", "setThingPosition", "setThingOrientation",
                 "setThingName", "setThingTemplate", "setThingValue", "clearThingArchitecture" })
        {
            check(contains(out, "\"" + std::string(op) + "{"), std::string("schema lists ") + op);
        }
        check(contains(out, "\"patchFields\":[") && contains(out, "resolveExportDependencies"),
              "schema lists the patch-root fields");
    }
    else if (scenario == "bake-light-limits")
    {
        const std::string plain = R"({"schemaVersion":1,"operations":[{"op":"bakeLighting"}]})";
        const std::string limited = R"({"schemaVersion":1,"operations":[{"op":"bakeLighting","lightFloor":0.25,"lightCap":0.5}]})";
        check(runAgent({ "plan", level.string(), "-" }, plain, out, root) == 0,
              "plain bake plan succeeds before opt-in policy");
        const auto plainHash = extractSha(out, "outputSha256");
        // A JK level saves one light value per corner, and a bake has to
        // store its result there whether or not a limits policy is given.
        // This fixture has no light, so the plain bake stores zero. Before
        // that the bake left the saved channel alone and the level kept its
        // previous light (a4a16481 produced ae9eda3b...356c4 here).
        check(plainHash == "672616592271847928f9ccf939b22b0e32f88f47e0e0f8c13325e0ed2cdcab2a",
              "plain JK bake stores the baked corner light (exact bytes)");
        const fs::path plainPath = root / "plain.jkl";
        check(runAgent({ "apply", level.string(), "-", plainPath.string(), plainHash }, plain, out, root) == 0
              && contains(readFile(plainPath), "3 0,0 1,1 2,2 0.0000 0.0000 0.0000"),
              "plain JK bake writes the unlit corners as zero light, not their previous values");
        check(runAgent({ "plan", level.string(), "-" }, limited, out, root) == 0
              && contains(out, "\"bakeLightingReports\":[{")
              && contains(out, "\"minimum\":0.25") && contains(out, "\"maximum\":0.25")
              && contains(out, "\"scope\":\"stored-corner-rgb\""),
              "opt-in floor reports exact unlit model-corner result");
        const auto limitedHash = extractSha(out, "outputSha256");
        check(limitedHash != plainHash, "explicit light floor changes output bytes");
        const fs::path limitedPath = root / "limited.jkl";
        check(runAgent({ "apply", level.string(), "-", limitedPath.string(), limitedHash }, limited, out, root) == 0
              && sha256HexOf(readFile(limitedPath)) == limitedHash,
              "limited apply matches reviewed plan and exact output hash");
        check(runAgent({ "plan", limitedPath.string(), "-" }, limited, out, root) == 0
              && extractSha(out, "outputSha256") == limitedHash,
              "fresh re-bake does not accumulate the previous floor");
        check(runAgent({ "plan", level.string(), "-" }, plain, out, root) == 0
              && extractSha(out, "outputSha256") == plainHash,
              "a plain bake stays byte-stable after a limited one");
        const std::string capped = R"({"schemaVersion":1,"operations":[{"op":"bakeLighting","lightFloor":0,"lightCap":0}]})";
        check(runAgent({ "plan", level.string(), "-" }, capped, out, root) == 0
              && contains(out, "\"cappedCorners\":3") && contains(out, "\"nearBlackCorners\":3")
              && contains(out, "\"corner\":0") && contains(out, "\"sector\":0")
              && contains(out, "\"surface\":0"),
              "zero cap reports bounded corner diagnostics with stable addresses");
        for ( const auto fields : { "\"lightFloor\":-1", "\"lightCap\":2",
                                   "\"lightFloor\":0.8,\"lightCap\":0.2", "\"lightFloor\":\"bad\"" } )
        {
            const std::string invalid = "{\"operations\":[{\"op\":\"bakeLighting\"," + std::string(fields) + "}]}";
            const auto refusedPath = root / "invalid-limit.jkl";
            check(runAgent({ "apply", level.string(), "-", refusedPath.string() }, invalid, out, root) == 2
                  && !fs::exists(refusedPath), "invalid bake policy refuses without publication");
        }
        std::string repeated = "{\"operations\":[";
        for ( int index = 0; index < 100; ++index )
        {
            if ( index != 0 ) repeated += ',';
            repeated += R"({"op":"bakeLighting","lightFloor":0,"lightCap":0})";
        }
        repeated += "]}";
        check(runAgent({ "plan", level.string(), "-" }, repeated, out, root) == 0,
              "multi-bake request completes with a shared diagnostic budget");
        std::size_t details = 0;
        for ( std::size_t at = 0; (at = out.find("\"corner\":", at)) != std::string::npos; ++at ) ++details;
        check(details == 256 && contains(out, "\"diagnosticsTruncated\":true"),
              "bake details are bounded across the whole patch, not per operation");
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
    else if (scenario == "asset-create")
    {
        // mat-create (PNG + CMP -> MAT) and archive-create (loose files ->
        // GOB), end to end through the CLI, with the archive then mounted.
        std::string cmp = "CMP ";
        cmp.resize(64, '\0');
        for (int i = 0; i < 256; ++i)
        {
            cmp.push_back(static_cast<char>((i & 7) * 36));
            cmp.push_back(static_cast<char>(((i >> 3) & 7) * 36));
            cmp.push_back(static_cast<char>((i >> 6) * 85));
        }
        const fs::path cmpFile = root / "pal.cmp";
        writeFile(cmpFile, cmp);
        // A 16x8 PNG from RED's own encoder (mat-export-png of a MAT made here).
        std::string mat = "MAT ";
        const auto pushI32 = [&mat](const std::int32_t v) {
            const std::uint32_t u = static_cast<std::uint32_t>(v);
            for (int shift = 0; shift < 32; shift += 8) mat.push_back(static_cast<char>((u >> shift) & 0xFFu));
        };
        pushI32(0x32); pushI32(2); pushI32(1); pushI32(1); pushI32(0); pushI32(8);
        for (int i = 0; i < 12; ++i) pushI32(0);
        pushI32(8); pushI32(0); for (int i = 0; i < 8; ++i) pushI32(0);
        pushI32(16); pushI32(8); pushI32(0); pushI32(0); pushI32(0); pushI32(1);
        for (int i = 0; i < 128; ++i) mat.push_back(static_cast<char>((i * 7) & 0xFF));
        const fs::path seedMat = root / "seed.mat";
        writeFile(seedMat, mat);
        const fs::path png = root / "src.png";
        check(runAgent({ "mat-export-png", seedMat.string(), png.string(), "0", cmpFile.string() }, "", out, root) == 0,
              "a source PNG is exported");
        const auto bound = [](const fs::path& p) {
            return "{\"path\":\"" + jsonPath(p) + "\",\"sha256\":\"" + sha256HexOf(readFile(p)) + "\"}";
        };
        const fs::path req = root / "matreq.json";
        writeFile(req, "{\"schemaVersion\":1,\"cels\":[" + bound(png) + "],\"palette\":" + bound(cmpFile)
            + ",\"dither\":false}");
        const fs::path made = root / "made.mat";
        check(runAgent({ "mat-create", req.string(), made.string() }, "", out, root) == 0 && fs::exists(made)
              && contains(out, "\"width\":16") && contains(out, "\"height\":8") && contains(out, "\"mipmaps\":1")
              && contains(out, "\"meanColourError\":0"),
              "mat-create maps a palette-exact PNG back to its palette with no error");
        check(sha256HexOf(readFile(made)) == extractSha(out, "outputSha256"), "mat-create receipt hash matches");
        check(runAgent({ "mat-inspect", made.string() }, "", out, root) == 0 && contains(out, "\"celCount\":1"),
              "the created MAT reads back");
        check(runAgent({ "mat-create", req.string(), made.string() }, "", out, root) == 5,
              "mat-create refuses an existing output -> 5");
        writeFile(req, "{\"schemaVersion\":1,\"cels\":[{\"path\":\"" + jsonPath(png) + "\",\"sha256\":\""
            + std::string(64, '0') + "\"}],\"palette\":" + bound(cmpFile) + "}");
        check(runAgent({ "mat-create", req.string(), (root / "stale.mat").string() }, "", out, root) == 4,
              "mat-create stale source hash -> 4");
        const fs::path notPng = root / "bad.png";
        writeFile(notPng, "this is not a png");
        writeFile(req, "{\"schemaVersion\":1,\"cels\":[" + bound(notPng) + "],\"palette\":" + bound(cmpFile) + "}");
        check(runAgent({ "mat-create", req.string(), (root / "bad.mat").string() }, "", out, root) == 3
              && contains(out, "invalidPng") && !fs::exists(root / "bad.mat"), "mat-create refuses a non-PNG -> 3");
        // archive-create
        const fs::path cogFile = root / "door.cog";
        writeFile(cogFile, kFixtureCog);
        const fs::path areq = root / "archive.json";
        writeFile(areq, "{\"schemaVersion\":1,\"entries\":[{\"name\":\"mat/custom.mat\",\"path\":\"" + jsonPath(made)
            + "\",\"sha256\":\"" + sha256HexOf(readFile(made)) + "\"},{\"name\":\"cog\\\\door.cog\",\"path\":\""
            + jsonPath(cogFile) + "\",\"sha256\":\"" + sha256HexOf(readFile(cogFile)) + "\"}]}");
        const fs::path gob = root / "custom.gob";
        check(runAgent({ "archive-create", areq.string(), gob.string() }, "", out, root) == 0 && fs::exists(gob)
              && contains(out, "\"readBack\":\"mat\"") && contains(out, "\"readBack\":\"cog-symbols\""),
              "archive-create writes a GOB and reads each entry back by type");
        check(runAgent({ "archive", gob.string() }, "", out, root) == 0 && contains(out, "mat\\\\custom.mat")
              && contains(out, "cog\\\\door.cog"), "the archive lists both entries with backslash names");
        const fs::path extracted = root / "back.mat";
#ifdef _WIN32
        const std::string entryArg = "mat\\custom.mat";
#else
        const std::string entryArg = "'mat\\custom.mat'"; // the test shell would eat a bare backslash
#endif
        check(runAgent({ "extract", gob.string(), entryArg, extracted.string() }, "", out, root) == 0
              && readFile(extracted) == readFile(made), "an extracted entry matches its source bytes");
        writeFile(areq, "{\"schemaVersion\":1,\"entries\":[{\"name\":\"mat/a.mat\",\"path\":\"" + jsonPath(made)
            + "\",\"sha256\":\"" + sha256HexOf(readFile(made)) + "\"},{\"name\":\"MAT\\\\A.MAT\",\"path\":\""
            + jsonPath(made) + "\",\"sha256\":\"" + sha256HexOf(readFile(made)) + "\"}]}");
        check(runAgent({ "archive-create", areq.string(), (root / "dup.gob").string() }, "", out, root) == 5
              && contains(out, "duplicateArchiveEntry"), "archive-create refuses a case-insensitive duplicate");
        writeFile(areq, "{\"schemaVersion\":1,\"entries\":[{\"name\":\"mat\\\\..\\\\x.mat\",\"path\":\"" + jsonPath(made)
            + "\",\"sha256\":\"" + sha256HexOf(readFile(made)) + "\"}]}");
        check(runAgent({ "archive-create", areq.string(), (root / "dots.gob").string() }, "", out, root) == 2,
              "archive-create refuses a name that leaves the archive");
        writeFile(areq, "{\"schemaVersion\":1,\"entries\":[{\"name\":\"mat\\\\broken.mat\",\"path\":\"" + jsonPath(cogFile)
            + "\",\"sha256\":\"" + sha256HexOf(readFile(cogFile)) + "\"}]}");
        check(runAgent({ "archive-create", areq.string(), (root / "broken.gob").string() }, "", out, root) == 3
              && contains(out, "invalidArchiveEntry") && !fs::exists(root / "broken.gob"),
              "archive-create refuses an entry that does not read as its type");
        // defineTemplate: a level-local template parsed by the template reader.
        const fs::path level = root / "tpl-level.jkl";
        writeFile(level, kJkl);
        check(runAgent({ "plan", level.string(), "-" },
                  R"({"schemaVersion":1,"operations":[{"op":"defineTemplate","name":"relay_core","parent":"none","values":{"type":"cog","model3d":"relay.3do","size":0.2,"collide":1}}]})",
                  out, root) == 0 && contains(out, "\"defined\":\"relay_core\"") && contains(out, "model3d=relay.3do"),
              "defineTemplate adds a typed level template");
        check(runAgent({ "plan", level.string(), "-" },
                  R"({"schemaVersion":1,"operations":[{"op":"defineTemplate","name":"relay_core","parent":"_structure","values":{"size":0.2}}]})",
                  out, root) == 5 && contains(out, "templateParentMissing"),
              "defineTemplate refuses a parent the level does not define");
        check(runAgent({ "plan", level.string(), "-" },
                  R"({"schemaVersion":1,"operations":[{"op":"defineTemplate","name":"a","parent":"none","values":{"size":0.2}},{"op":"defineTemplate","name":"A","parent":"none","values":{"size":0.1}}]})",
                  out, root) == 5 && contains(out, "templateExists"),
              "defineTemplate refuses a name the level already defines");
        check(runAgent({ "plan", level.string(), "-" },
                  R"({"schemaVersion":1,"operations":[{"op":"defineTemplate","name":"b","parent":"none","values":{"model3d":"has space.3do"}}]})",
                  out, root) == 2, "defineTemplate refuses a value the template format cannot carry");
    }
    else if (scenario == "model-import")
    {
        // model-import: a Wavefront OBJ becomes a JK 3DO through the core
        // importer and writer, and is read back before it is written.
        check(runAgent({ "schema" }, "", out, root) == 0 && contains(out, "\"model-import\""),
              "schema lists model-import");
        const auto bound = [](const fs::path& p) {
            return "{\"path\":\"" + jsonPath(p) + "\",\"sha256\":\"" + sha256HexOf(readFile(p)) + "\"}";
        };
        const std::string crate = "\"materials\":[{\"name\":\"crate\",\"mat\":\"crate.mat\",\"size\":[64,32]}]";
        // A 2x2x2 cube in OBJ's y-up convention, every face counter-clockwise
        // seen from outside; the bottom face's last corner uses vt 5.
        const fs::path cube = root / "cube.obj";
        writeFile(cube,
            "o Crate\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nv -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 0.25 0.75\nusemtl crate\nf 1/1 2/2 3/3 4/4\nf 6/1 5/2 8/3 7/4\n"
            "f 2/1 6/2 7/3 3/4\nf 5/1 1/2 4/3 8/4\nf 4/1 3/2 7/3 8/4\nf 5/1 6/2 2/3 1/5\n");
        const fs::path req = root / "model.json";
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(cube) + "," + crate + "}");
        const fs::path model = root / "crate.3do";
        check(runAgent({ "model-import", req.string(), model.string() }, "", out, root) == 0 && fs::exists(model)
              && contains(out, "\"faces\":6") && contains(out, "\"vertices\":8")
              && contains(out, "\"textureVertices\":5") && contains(out, "\"triangulatedPolygons\":0")
              && contains(out, "\"name\":\"Crate\"") && contains(out, "\"max\":[1,1,2]"),
              "model-import turns a textured cube into one mesh of 8 vertices and 6 faces, base at the origin");
        check(extractSha(out, "outputSha256") == sha256HexOf(readFile(model)), "model-import receipt hash matches");
        const std::string text = readFile(model);
        check(contains(text, "3DO 2.1") && contains(text, "0: crate.mat") && contains(text, "VERTICES 8")
              && contains(text, "TEXTURE VERTICES 5") && contains(text, "\n4: 16 8\n") && contains(text, "FACES 6")
              && contains(text, "\n0: 0 0x0 4 3 1 0 4 0, 0 1, 1 2, 2 3, 3\n")
              && contains(text, "HIERARCHY NODES 1\n0: 0x0 0x0 0 -1 -1 -1 0 "),
              "the 3DO lists the MAT, texel UVs (u*64, (1-v)*32), stock prop face modes and a type-0 root node");
        check(runAgent({ "model-import", req.string(), model.string() }, "", out, root) == 5,
              "model-import refuses an existing output -> 5");
        const fs::path areq = root / "archive.json";
        writeFile(areq, "{\"schemaVersion\":1,\"entries\":[{\"name\":\"3do\\\\crate.3do\",\"path\":\"" + jsonPath(model)
            + "\",\"sha256\":\"" + sha256HexOf(readFile(model)) + "\"}]}");
        check(runAgent({ "archive-create", areq.string(), (root / "crate.gob").string() }, "", out, root) == 0
              && contains(out, "\"readBack\":\"3do\""),
              "the imported 3DO reads back through the core 3DO reader when archived");
        // The texel size can come from a MAT: a 16x8 texture halves u and v.
        std::string mat = "MAT ";
        const auto pushI32 = [&mat](const std::int32_t v) {
            const std::uint32_t u = static_cast<std::uint32_t>(v);
            for (int shift = 0; shift < 32; shift += 8) mat.push_back(static_cast<char>((u >> shift) & 0xFFu));
        };
        pushI32(0x32); pushI32(2); pushI32(1); pushI32(1); pushI32(0); pushI32(8);
        for (int i = 0; i < 12; ++i) pushI32(0);
        pushI32(8); pushI32(0); for (int i = 0; i < 8; ++i) pushI32(0);
        pushI32(16); pushI32(8); pushI32(0); pushI32(0); pushI32(0); pushI32(1);
        for (int i = 0; i < 128; ++i) mat.push_back(static_cast<char>(i));
        const fs::path matFile = root / "crate.mat";
        writeFile(matFile, mat);
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(cube)
            + ",\"materials\":[{\"name\":\"crate\",\"mat\":\"crate.mat\",\"matFile\":" + bound(matFile) + "}]}");
        const fs::path sized = root / "sized.3do";
        check(runAgent({ "model-import", req.string(), sized.string() }, "", out, root) == 0
              && contains(out, "\"width\":16") && contains(out, "\"from\":\"matFile\"")
              && contains(readFile(sized), "\n4: 4 2\n"),
              "a matFile gives the texel size");
        // A quad whose fourth corner leaves the plane of the other three.
        const fs::path warped = root / "warped.obj";
        writeFile(warped, "v 0 0 0\nv 1 0 0\nv 1 0.5 -1\nv 0 0 -1\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl crate\n"
            "f 1/1 2/2 3/3 4/4\n");
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(warped) + "," + crate + "}");
        check(runAgent({ "model-import", req.string(), (root / "warped.3do").string() }, "", out, root) == 0
              && contains(out, "\"triangulatedPolygons\":1") && contains(out, "\"faces\":2"),
              "a quad that is not planar is triangulated into two faces");
        const fs::path ghost = root / "ghost.obj";
        writeFile(ghost, "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl ghost\nf 1/1 2/1 3/1\n");
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(ghost) + "," + crate + "}");
        check(runAgent({ "model-import", req.string(), (root / "ghost.3do").string() }, "", out, root) == 5
              && contains(out, "objMaterialUnmapped") && contains(out, "'ghost'") && !fs::exists(root / "ghost.3do"),
              "an unmapped usemtl is refused by name -> 5, no file");
        std::string many = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\n";
        for (int i = 0; i < 513; ++i) many += "f 1/1 2/1 3/1\n";
        const fs::path manyObj = root / "many.obj";
        writeFile(manyObj, many);
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(manyObj) + "," + crate + "}");
        check(runAgent({ "model-import", req.string(), (root / "many.3do").string() }, "", out, root) == 0
              && contains(out, "\"faces\":513") && contains(out, "\"faces\":512")
              && contains(out, "\"parts\":2") && fs::exists(root / "many.3do"),
              "more than 512 faces splits into bounded meshes by default");
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(manyObj) + "," + crate + ",\"split\":false}");
        check(runAgent({ "model-import", req.string(), (root / "many-unsplit.3do").string() }, "", out, root) == 5
              && contains(out, "objLimitExceeded") && contains(out, "reduce the model first")
              && !fs::exists(root / "many-unsplit.3do"),
              "more than 512 faces with splitting disabled is refused -> 5, no file");
        writeFile(req, "{\"schemaVersion\":1,\"source\":{\"path\":\"" + jsonPath(cube) + "\",\"sha256\":\""
            + std::string(64, '0') + "\"}," + crate + "}");
        check(runAgent({ "model-import", req.string(), (root / "stale.3do").string() }, "", out, root) == 4
              && !fs::exists(root / "stale.3do"),
              "a stale source hash -> 4, no file");
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(cube) + "," + crate + ",\"scale\":-1}");
        check(runAgent({ "model-import", req.string(), (root / "mirror.3do").string() }, "", out, root) == 2,
              "a negative (mirroring) scale is a usage error -> 2");
        const fs::path broken = root / "broken.obj";
        writeFile(broken, "v 0 nan 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\nf 1/1 2/1 3/1\n");
        writeFile(req, "{\"schemaVersion\":1,\"source\":" + bound(broken) + "," + crate + "}");
        check(runAgent({ "model-import", req.string(), (root / "broken.3do").string() }, "", out, root) == 3
              && contains(out, "objNonFinite"),
              "a NaN coordinate is an input error -> 3");
    }
    else if (scenario == "review-guards")
    {
        // The export dependency scan reads only hash-bound archives, within
        // their expansion limits, for every JKL/NDY output; template imports
        // and definitions stay within what the game's readers accept.
        const fs::path base = root / "base.jkl";
        check(runAgent({ "new", base.string(), "jk" }, "", out, root) == 0, "review-guards scaffold");
        const fs::path smallGob = root / "small.gob";
        check(writeGobV2(smallGob, { { "misc\\note.txt", "note" } }), "small gob written");
        const std::string smallArchive = "\"resourceArchives\":[{\"path\":\"" + jsonPath(smallGob)
            + "\",\"sha256\":\"" + sha256HexOf(readFile(smallGob)) + "\"}]";

        // A name in the level that is an absolute path never opens that file.
        const fs::path outside = fs::absolute(root / "outside" / "secret.snd");
        fs::create_directories(outside.parent_path());
        writeFile(outside, "export TOKEN=s3cr3t-value\nUser alice\n");
        const std::string probe = "{\"schemaVersion\":1," + smallArchive + ",\"operations\":["
            "{\"op\":\"defineTemplate\",\"name\":\"probe\",\"parent\":\"none\",\"values\":{\"type\":\"actor\","
            "\"soundclass\":\"" + jsonPath(outside) + "\"}},"
            "{\"op\":\"addThing\",\"sector\":0,\"template\":\"probe\",\"name\":\"p1\",\"position\":[0,0,0]}]}";
        const fs::path probed = root / "probed.jkl";
        check(runAgent({ "apply", base.string(), "-", probed.string() }, probe, out, root) == 0 && fs::exists(probed),
              "a level naming an absolute-path resource still applies");
        check(!contains(readFile(probed), "alice") && !contains(readFile(probed), "s3cr3t")
                  && contains(out, "\"dependencyScan\":\"incomplete\""),
              "the export scan never reads a file outside the hash-bound archives; the name reads as missing");

        // Index rows that alias one payload are admitted against the same
        // expansion budget as COG resolution.
        {
            const fs::path aliasGob = root / "alias.gob";
            const std::size_t payload = 1u << 20;
            const std::size_t rows = 600;
            std::ofstream gob(aliasGob, std::ios::binary | std::ios::trunc);
            gob.write("GOB ", 4);
            writeU32LE(gob, 0x14u);
            writeU32LE(gob, 0x0Cu);
            writeU32LE(gob, static_cast<std::uint32_t>(rows));
            const std::uint32_t dataOffset = 16u + static_cast<std::uint32_t>(rows) * 136u;
            for (std::size_t i = 0; i < rows; ++i)
            {
                writeU32LE(gob, dataOffset);
                writeU32LE(gob, static_cast<std::uint32_t>(payload));
                std::array<char, 128> name{};
                const std::string entry = "misc\\alias" + std::to_string(i) + ".snd";
                std::copy(entry.begin(), entry.end(), name.begin());
                gob.write(name.data(), static_cast<std::streamsize>(name.size()));
            }
            const std::string bytes(payload, 'a');
            gob.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            gob.close();
            const std::string body = "{\"schemaVersion\":1,\"resourceArchives\":[{\"path\":\"" + jsonPath(aliasGob)
                + "\",\"sha256\":\"" + sha256HexOf(readFile(aliasGob)) + "\"}],\"operations\":[{\"op\":\"setGravity\",\"value\":4.5}]}";
            check(runAgent({ "plan", base.string(), "-" }, body, out, root) == 3 && contains(out, "archiveExpansionLimit"),
                  "an archive whose index rows alias one payload past the expansion budget is refused (3)");
            const std::string optOut = "{\"schemaVersion\":1,\"resolveExportDependencies\":false,\"resourceArchives\":[{\"path\":\""
                + jsonPath(aliasGob) + "\",\"sha256\":\"" + sha256HexOf(readFile(aliasGob))
                + "\"}],\"operations\":[{\"op\":\"setGravity\",\"value\":4.5}]}";
            check(runAgent({ "plan", base.string(), "-" }, optOut, out, root) == 0,
                  "with the scan turned off the archive is not read for it");
        }

        // JED input writes JKL, so it gets the scan and the receipt too.
        const fs::path jed = root / "base.jed";
        writeFile(jed, kScaffoldJed);
        const std::string gravity = "{\"schemaVersion\":1," + smallArchive
            + ",\"operations\":[{\"op\":\"setGravity\",\"value\":4.5}]}";
        check(runAgent({ "plan", jed.string(), "-" }, gravity, out, root) == 0 && contains(out, "\"exportBuffers\"")
                  && !contains(out, "\"dependencyScan\":\"not-requested\""),
              "a JED input with patch-root archives runs the export scan");
        const std::string explicitScan = "{\"schemaVersion\":1,\"resolveExportDependencies\":true," + smallArchive
            + ",\"operations\":[{\"op\":\"setGravity\",\"value\":4.5}]}";
        check(runAgent({ "plan", jed.string(), "-" }, explicitScan, out, root) == 0,
              "resolveExportDependencies is accepted for JED input, which writes JKL");
        const std::string noArchives = R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":4.5}]})";
        check(runAgent({ "plan", jed.string(), "-" }, noArchives, out, root) == 0
                  && contains(out, "\"dependencyScan\":\"not-requested\""),
              "a JED input without archives reports the scan as not requested");

        // Archives on one operation do not enable the scan, and the receipt says so.
        {
            const fs::path cogGob = root / "cogs.gob";
            check(writeGobV2(cogGob, { { "cog\\fixture.cog", kFixtureCog } }), "cog gob written");
            const std::string opArchives = "\"resourceArchives\":[{\"path\":\"" + jsonPath(cogGob)
                + "\",\"sha256\":\"" + sha256HexOf(readFile(cogGob)) + "\"}]";
            const std::string body = "{\"schemaVersion\":1,\"operations\":[{\"op\":\"addCog\",\"script\":\"fixture.cog\","
                + opArchives + ",\"bindings\":[{\"symbol\":\"gate\",\"kind\":\"thing\",\"targetIndex\":-1},"
                "{\"symbol\":\"room\",\"kind\":\"sector\",\"targetIndex\":0}]}]}";
            check(runAgent({ "plan", base.string(), "-" }, body, out, root) == 0
                      && contains(out, "\"dependencyScan\":\"not-requested\"")
                      && contains(out, "do not enable the scan"),
                  "archives given on one operation do not enable the scan, and the receipt says so");
            // The standalone command reports the same receipt as plan/apply.
            const fs::path req = root / "addcog.json";
            writeFile(req, "{\"schemaVersion\":1,\"script\":\"fixture.cog\"," + opArchives
                + ",\"bindings\":[{\"symbol\":\"gate\",\"kind\":\"thing\",\"targetIndex\":-1},"
                  "{\"symbol\":\"room\",\"kind\":\"sector\",\"targetIndex\":0}]}");
            check(runAgent({ "addCog", base.string(), req.string() }, "", out, root) == 0
                      && contains(out, "\"exportBuffers\"") && !contains(out, "\"dependencyScan\":\"not-requested\""),
                  "standalone addCog runs the export scan and reports exportBuffers");
        }

        // importTemplates walks an inheritance chain without recursion.
        {
            const auto chain = [&](const int depth) {
                std::string text = "# chain\n";
                for (int i = 0; i < depth; ++i)
                    text += "t" + std::to_string(i) + " t" + std::to_string(i + 1) + " type=cog\n";
                text += "t" + std::to_string(depth) + " none type=cog\n";
                return text;
            };
            const fs::path shortChain = root / "short.tpl";
            writeFile(shortChain, chain(3000));
            const std::string shortBody = "{\"schemaVersion\":1,\"operations\":[{\"op\":\"importTemplates\","
                "\"templates\":[\"t0\"],\"templateSource\":{\"path\":\"" + jsonPath(shortChain) + "\",\"sha256\":\""
                + sha256HexOf(readFile(shortChain)) + "\"}}]}";
            check(runAgent({ "plan", base.string(), "-" }, shortBody, out, root) == 0
                      && contains(out, "\"t3000\"") && contains(out, "\"t0\""),
                  "importTemplates imports a 3000-deep inheritance chain");
            const fs::path deepChain = root / "deep.tpl";
            writeFile(deepChain, chain(20000));
            const std::string deepBody = "{\"schemaVersion\":1,\"operations\":[{\"op\":\"importTemplates\","
                "\"templates\":[\"t0\"],\"templateSource\":{\"path\":\"" + jsonPath(deepChain) + "\",\"sha256\":\""
                + sha256HexOf(readFile(deepChain)) + "\"}}]}";
            check(runAgent({ "plan", base.string(), "-" }, deepBody, out, root) == 5
                      && contains(out, "templateImportLimit"),
                  "a 20000-deep chain is refused with templateImportLimit (5), not a crash");
            std::string wide = "wide none type=cog";
            for (int i = 0; i < 70000; ++i) wide += " explode=u" + std::to_string(i);
            const fs::path wideTpl = root / "wide.tpl";
            writeFile(wideTpl, wide + "\n");
            const std::string wideBody = "{\"schemaVersion\":1,\"operations\":[{\"op\":\"importTemplates\","
                "\"templates\":[\"wide\"],\"templateSource\":{\"path\":\"" + jsonPath(wideTpl) + "\",\"sha256\":\""
                + sha256HexOf(readFile(wideTpl)) + "\"}}]}";
            check(runAgent({ "plan", base.string(), "-" }, wideBody, out, root) == 5
                      && contains(out, "templateImportLimit"),
                  "a template naming more than 65536 templates is refused with templateImportLimit (5)");
        }

        // defineTemplate tokens read back as one argument in the game.
        {
            const auto define = [&](const std::string& name, const std::string& values) {
                return runAgent({ "plan", base.string(), "-" },
                    "{\"schemaVersion\":1,\"operations\":[{\"op\":\"defineTemplate\",\"name\":\"" + name
                        + "\",\"parent\":\"none\",\"values\":" + values + "}]}", out, root);
            };
            check(define("relay,core", R"({"type":"cog"})") == 2, "defineTemplate refuses a comma in the name");
            check(define(";hidden", R"({"type":"cog"})") == 2, "defineTemplate refuses a name starting with ';'");
            check(define("crate", R"({"size,thingflags":"0x400"})") == 2, "defineTemplate refuses a comma in a key");
            check(define("crate", R"({"thingflags":"0x400,0x8"})") == 2, "defineTemplate refuses a comma in a value");
            check(define("crate", R"({"model3d":"relay.3do\\"})") == 2, "defineTemplate refuses a backslash");
            check(define("crate", R"J({"frame":"(1/2/3:0/0/0))"})J") == 2,
                  "defineTemplate refuses unbalanced parentheses");
            check(define("crate", R"J({"frame":"(1/2/3:0/0/0)","health":2000000000})J") == 0
                      && contains(out, "health=2000000000"),
                  "defineTemplate keeps balanced frames and writes an integral number exactly");
        }

        // A surface whose every fan triangle is collinear has no area: an
        // error, unlike one collinear triangle beside a fan origin.
        {
            std::string flat = readFile(base);
            const auto replaceOnce = [&flat](const std::string& from, const std::string& to) {
                const std::size_t at = flat.find(from);
                if (at == std::string::npos) return false;
                flat.replace(at, from.size(), to);
                return true;
            };
            const bool edited = replaceOnce("WORLD VERTICES 8\n", "WORLD VERTICES 10\n")
                && replaceOnce("7: -1.00000000 -1.00000000 1.00000000\n",
                    "7: -1.00000000 -1.00000000 1.00000000\n8: -0.50000000 -1.00000000 -1.00000000\n"
                    "9: 0.50000000 -1.00000000 -1.00000000\n")
                && replaceOnce(" 4 0,0 1,1 2,2 3,3 ", " 4 0,0 8,1 9,2 1,3 ")
                && replaceOnce("\nVERTICES 8\n", "\nVERTICES 10\n")
                && replaceOnce("\n7: 7\n", "\n7: 7\n8: 8\n9: 9\n");
            check(edited, "zero-area fixture edited from the scaffold");
            const fs::path flatLevel = root / "flat.jkl";
            writeFile(flatLevel, flat);
            check(runAgent({ "validate", flatLevel.string() }, "", out, root) == 0
                      && contains(out, "\"valid\":false") && contains(out, "zero-area-surface"),
                  "validate reports a surface whose fan triangles are all collinear as a zero-area error");
            check(runAgent({ "validate", base.string() }, "", out, root) == 0 && contains(out, "\"valid\":true")
                      && !contains(out, "zero-area-surface"),
                  "the unedited scaffold stays valid");
        }

        // buildSwitchbackStairs has no total step count.
        check(runAgent({ "plan", base.string(), "-" },
                  R"({"schemaVersion":1,"operations":[{"op":"buildSwitchbackStairs","origin":[0,0,-1],"direction":"+y","width":0.5,"run":0.2,"rise":0.1,"headroom":0.6,"landingRun":0.5,"stepsPerFlight":2,"steps":10}]})",
                  out, root) == 2 && contains(out, "stepsPerFlight"),
              "buildSwitchbackStairs refuses an unknown 'steps' field and names stepsPerFlight");
        check(runAgent({ "schema" }, "", out, root) == 0
                  && contains(out, "buildSwitchbackStairs{origin,direction,width,run,rise,headroom,landingRun,stepsPerFlight}"),
              "the schema lists the switchback fields the operation reads");
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
