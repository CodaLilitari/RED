#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/level.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>

namespace sed::tests
{

namespace
{

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::size_t countStagedFiles(const std::filesystem::path& root)
{
    std::size_t count = 0;
    if (std::filesystem::exists(root))
    {
        for (const std::filesystem::directory_entry& entry
            : std::filesystem::directory_iterator(root))
        {
            const std::string name = entry.path().filename().string();
            if (name.starts_with(".sed-save-") && name.ends_with(".tmp"))
            {
                ++count;
            }
        }
    }
    return count;
}

// Original synthetic fixture: duplicate colormap names make slot 1
// distinguishable from a first-name match, and the custom section is
// data the importer does not model.
const std::string kParitySource =
    "SECTION: HEADER\n"
    "VERSION 1\n"
    "END\n"
    "SECTION: GEORESOURCE\n"
    "WORLD COLORMAPS 3\n"
    "0: duplicate.cmp\n"
    "1: duplicate.cmp\n"
    "2: other.cmp\n"
    "WORLD VERTICES 3\n"
    "0: 0 0 0\n"
    "1: 1 0 0\n"
    "2: 0 1 0\n"
    "WORLD TEXTURE VERTICES 2\n"
    "0: -0 0.5\n"
    "1: 0 0.5\n"
    "WORLD ADJOINS 0\n"
    "WORLD SURFACES 1\n"
    "0: -1 0x0 0x0 0 3 3 -1 0 3 0,0 1,1 2,0 1 1 1\n"
    "0: 0 0 1\n"
    "END\n"
    "SECTION: SECTORS\n"
    "WORLD SECTORS 1\n"
    "SECTOR 0\n"
    "FLAGS 0x0\n"
    "COLORMAP 1\n"
    "SURFACES 0 1\n"
    "END\n"
    "SECTION: CUSTOM_SYNTHETIC\n"
    "opaque payload KEEP me 42\n"
    "END\n";

sed::Level loadParityLevel()
{
    std::istringstream input(kParitySource);
    return sed::assets::loadFromJKL(input);
}

// One refuse-don't-repair row: export must throw and the destination
// must stay byte-identical with no staging file left behind.
void checkRefusalLeavesFileUntouched(
    const sed::Level& level,
    const std::filesystem::path& root,
    const char* caseName)
{
    const std::filesystem::path path = root / "refusal.jkl";
    const std::string sentinel = "previous complete JKL destination\n";
    writeTextFile(path, sentinel);

    bool threw = false;
    try
    {
        sed::assets::saveToJKL(level, path);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    check(threw, caseName);
    check(readTextFile(path) == sentinel, caseName);
    check(countStagedFiles(root) == 0, caseName);
}

} // namespace

void runJklPreservationParityTests()
{
    const std::string unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sed_jkl_parity_" + unique);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    check(!error, "JKL preservation: temporary directory is created");

    {
        const sed::Level level = loadParityLevel();
        check(level.retainedJklSourceText == kParitySource,
            "JKL preservation: loader retains the exact source text");
        check(level.retainedJklSourceHash
                == sed::assets::jklSourceSha256Hex(kParitySource),
            "JKL preservation: loader captures the source hash");
        check(level.retainedJklUnknownSections.size() == 1,
            "JKL preservation: unknown section is retained");

        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        check(built.text.find("SECTION: CUSTOM_SYNTHETIC")
                    != std::string::npos
                && built.text.find("opaque payload KEEP me 42")
                    != std::string::npos,
            "JKL preservation: unknown section survives export");

        std::istringstream roundTripInput(built.text);
        const sed::Level roundTrip =
            sed::assets::loadFromJKL(roundTripInput);
        check(roundTrip.retainedJklUnknownSections.size() == 1,
            "JKL preservation: unknown section survives a round-trip");
        check(sed::assets::buildJklExport(roundTrip).text == built.text,
            "JKL preservation: export is byte-stable after a round-trip");
    }

    {
        sed::Level level = loadParityLevel();
        level.header.gravity = 2.0;
        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        check(built.text.find("opaque payload KEEP me 42")
                != std::string::npos,
            "JKL preservation: unknown section survives an unrelated edit");
    }

    {
        const sed::Level level = loadParityLevel();
        check(level.sectors.size() == 1
                && level.sectors[0].surfaces.size() == 1
                && level.sectors[0].surfaces[0].texVertices.size() == 3,
            "JKL preservation: signed-zero fixture loads");
        if (level.sectors.size() == 1
            && level.sectors[0].surfaces.size() == 1
            && level.sectors[0].surfaces[0].texVertices.size() == 3)
        {
            const std::vector<sed::TexVertex>& corners =
                level.sectors[0].surfaces[0].texVertices;
            check(std::signbit(corners[0].u) && !std::signbit(corners[1].u),
                "JKL preservation: loader keeps signed-zero UV signs");

            const sed::assets::JklBuiltExport built =
                sed::assets::buildJklExport(level);
            std::istringstream input(built.text);
            const sed::Level reloaded = sed::assets::loadFromJKL(input);
            check(reloaded.sectors.size() == 1
                    && reloaded.sectors[0].surfaces.size() == 1
                    && reloaded.sectors[0].surfaces[0].texVertices.size()
                        == 3,
                "JKL preservation: signed-zero export reloads");
            if (reloaded.sectors.size() == 1
                && reloaded.sectors[0].surfaces.size() == 1
                && reloaded.sectors[0].surfaces[0].texVertices.size() == 3)
            {
                const std::vector<sed::TexVertex>& out =
                    reloaded.sectors[0].surfaces[0].texVertices;
                check(std::signbit(out[0].u) && !std::signbit(out[1].u)
                        && std::signbit(out[2].u),
                    "JKL preservation: signed-zero UVs keep distinct slots");
            }
        }
    }

    {
        sed::Level level = loadParityLevel();
        level.header.gravity = 1.5;
        const std::string first = sed::assets::buildJklExport(level).text;
        std::istringstream input(first);
        const sed::Level reloaded = sed::assets::loadFromJKL(input);
        check(close(reloaded.header.gravity, 1.5),
            "JKL preservation: numeric values survive a round-trip");
        check(sed::assets::buildJklExport(reloaded).text == first,
            "JKL preservation: numeric spellings are stable on re-export");
    }

    {
        sed::Level level = loadParityLevel();
        level.retainedJklSourceHash[0] =
            level.retainedJklSourceHash[0] == '0' ? '1' : '0';
        checkRefusalLeavesFileUntouched(level, root,
            "JKL preservation: corrupted retained hash refuses, zero bytes");
    }
    {
        sed::Level level = loadParityLevel();
        level.retainedJklSourceText += " ";
        checkRefusalLeavesFileUntouched(level, root,
            "JKL preservation: corrupted retained text refuses, zero bytes");
    }
    {
        sed::Level level = loadParityLevel();
        level.sectors[0].colormap = "other.cmp";
        checkRefusalLeavesFileUntouched(level, root,
            "JKL preservation: colormap slot mismatch refuses pre-write");
    }
    {
        sed::Level level = loadParityLevel();
        level.sectors[0].colormapIndex = -1;
        checkRefusalLeavesFileUntouched(level, root,
            "JKL preservation: ambiguous colormap name refuses pre-write");
    }
    {
        sed::Level level = loadParityLevel();
        level.sectors[0].colormap = "other.cmp";
        level.sectors[0].colormapIndex = 99;
        checkRefusalLeavesFileUntouched(level, root,
            "JKL preservation: invalid colormap slot refuses pre-write");
    }
    {
        sed::Level level{};
        level.clear();
        level.kind = sed::ProjectType::JKDF2;
        level.header.version = 1;
        level.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
        sed::Sector& sector = level.newSector();
        sector.addVertex(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
        sector.addVertex(1.0, 0.0, 0.0);
        sector.addVertex(0.0, 1.0, 0.0);
        sed::Surface surface{};
        surface.vertices = { 0, 1, 2 };
        surface.texVertices.resize(3u);
        surface.normal = sed::TVector{ 0.0, 0.0, 1.0 };
        sector.addSurface(std::move(surface));
        checkRefusalLeavesFileUntouched(level, root,
            "JKL preservation: nonfinite geometry refuses pre-write");
    }

    std::filesystem::remove_all(root, error);
}

} // namespace sed::tests
