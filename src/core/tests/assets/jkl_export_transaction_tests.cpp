#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/level.hpp"
#include "../../src/assets/atomic_file_write.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

bool contains(const std::vector<std::string>& values, const std::string& wanted)
{
    for (const std::string& value : values)
    {
        if (value == wanted)
        {
            return true;
        }
    }
    return false;
}

// Original synthetic fixture with duplicate colormap names: the sector
// is bound to slot 1, which no first-name match would ever select.
const std::string kTransactionSource =
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
    "WORLD TEXTURE VERTICES 1\n"
    "0: 0 0\n"
    "WORLD ADJOINS 0\n"
    "WORLD SURFACES 1\n"
    "0: -1 0x0 0x0 0 3 3 -1 0 3 0,0 1,0 2,0 1 1 1\n"
    "0: 0 0 1\n"
    "END\n"
    "SECTION: SECTORS\n"
    "WORLD SECTORS 1\n"
    "SECTOR 0\n"
    "FLAGS 0x0\n"
    "COLORMAP 1\n"
    "SURFACES 0 1\n"
    "END\n";

sed::Level loadTransactionLevel()
{
    std::istringstream input(kTransactionSource);
    sed::Level level = sed::assets::loadFromJKL(input);
    level.sectors[0].surfaces[0].material = "synthetic-new.mat";
    return level;
}

} // namespace

void runJklExportTransactionTests()
{
    const std::string unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / ("sed_jkl_transaction_" + unique);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    check(!error, "JKL transaction: temporary directory is created");

    {
        sed::Level level = loadTransactionLevel();
        const std::vector<std::string> materialsBefore = level.materials;
        const std::vector<std::string> colormapsBefore = level.colormaps;
        const std::string retainedHashBefore = level.retainedJklSourceHash;
        check(!contains(materialsBefore, "synthetic-new.mat"),
            "JKL transaction: fixture starts without the new material");
        check(level.sectors[0].colormapIndex == 1,
            "JKL transaction: loader captures the exact colormap slot");

        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        check(level.materials == materialsBefore
                && level.colormaps == colormapsBefore
                && level.retainedJklSourceHash == retainedHashBefore
                && level.sectors[0].colormapIndex == 1,
            "JKL transaction: build does not mutate the Level");
        check(contains(built.materials, "synthetic-new.mat")
                && built.text.find("synthetic-new.mat") != std::string::npos,
            "JKL transaction: build proposes the new registration");
        check(built.colormaps == colormapsBefore
                && built.sectorColormapIndices.size() == 1
                && built.sectorColormapIndices[0] == 1,
            "JKL transaction: build preserves the colormap slot identity");

        std::ostringstream streamed;
        sed::assets::saveToJKL(level, streamed);
        check(streamed.str() == built.text,
            "JKL transaction: stream save matches the built export");

        const std::filesystem::path path = root / "committed.jkl";
        sed::assets::commitJklExport(level, built, path);
        check(readTextFile(path) == built.text,
            "JKL transaction: commit writes the built bytes");
        check(level.materials == built.materials
                && level.colormaps == built.colormaps
                && level.sectors[0].colormapIndex == 1,
            "JKL transaction: commit installs registrations and slots");
        check(level.retainedJklSourceHash
                == sed::assets::jklSourceSha256Hex(built.text),
            "JKL transaction: commit refreshes the retained baseline");
        check(sed::assets::buildJklExport(level).text == built.text,
            "JKL transaction: rebuild after commit is byte-identical");

        std::istringstream input(built.text);
        const sed::Level reopened = sed::assets::loadFromJKL(input);
        check(reopened.sectors.size() == 1
                && reopened.sectors[0].colormap == "duplicate.cmp"
                && reopened.sectors[0].colormapIndex == 1
                && reopened.colormaps == colormapsBefore,
            "JKL transaction: slot identity survives commit and reopen");
    }

    {
        // Editing a modeled field must not retarget the slot either.
        sed::Level level = loadTransactionLevel();
        level.sectors[0].surfaces[0].extraLight =
            sed::ColorF{ 0.375f, 0.375f, 0.375f, 1.0f };
        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        check(built.sectorColormapIndices.size() == 1
                && built.sectorColormapIndices[0] == 1,
            "JKL transaction: slot identity survives a sector edit");
        std::istringstream input(built.text);
        const sed::Level reopened = sed::assets::loadFromJKL(input);
        check(reopened.sectors.size() == 1
                && reopened.sectors[0].colormapIndex == 1,
            "JKL transaction: edited export reopens on the same slot");
    }

    {
        // A baseline changed between build and commit refuses the
        // commit before staging, leaving file and registrations alone.
        sed::Level level = loadTransactionLevel();
        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        const std::vector<std::string> materialsBefore = level.materials;
        const std::filesystem::path path = root / "guarded.jkl";
        const std::string sentinel = "previous complete JKL destination\n";
        writeTextFile(path, sentinel);
        level.retainedJklSourceText += " ";

        bool threw = false;
        try
        {
            sed::assets::commitJklExport(level, built, path);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        check(threw && readTextFile(path) == sentinel
                && level.materials == materialsBefore
                && countStagedFiles(root) == 0,
            "JKL transaction: stale baseline refuses commit, zero bytes");
    }

    {
        // Simulated promotion failure: destination is a directory, so
        // the staged sibling cannot replace it. The previous state and
        // every registration must survive, and a clean retry of the
        // same built export must produce identical bytes.
        sed::Level level = loadTransactionLevel();
        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        const std::vector<std::string> materialsBefore = level.materials;
        const std::vector<std::string> colormapsBefore = level.colormaps;
        const std::string retainedHashBefore = level.retainedJklSourceHash;

        const std::filesystem::path blocked = root / "blocked.jkl";
        std::filesystem::create_directory(blocked, error);
        check(!error, "JKL transaction: blocked destination is created");
        const std::filesystem::path marker = blocked / "keep.txt";
        writeTextFile(marker, "retain destination marker");

        bool threw = false;
        try
        {
            sed::assets::commitJklExport(level, built, blocked);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        check(threw && std::filesystem::is_directory(blocked)
                && readTextFile(marker) == "retain destination marker"
                && countStagedFiles(root) == 0,
            "JKL transaction: failed promotion keeps the previous file");
        check(level.materials == materialsBefore
                && level.colormaps == colormapsBefore
                && level.retainedJklSourceHash == retainedHashBefore
                && level.sectors[0].colormapIndex == 1,
            "JKL transaction: failed promotion keeps registrations");

        const std::filesystem::path retry = root / "retry.jkl";
        sed::assets::commitJklExport(level, built, retry);
        check(readTextFile(retry) == built.text,
            "JKL transaction: clean retry is byte-identical");
    }

    {
        // Injected publish failure against a real previous file: its
        // hash must be unchanged and the retry must succeed cleanly.
        sed::Level level = loadTransactionLevel();
        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        const std::vector<std::string> materialsBefore = level.materials;
        const std::filesystem::path path = root / "previous.jkl";
        const std::string sentinel = "previous complete JKL destination\n";
        writeTextFile(path, sentinel);
        const std::string hashBefore =
            sed::assets::jklSourceSha256Hex(sentinel);

        bool threw = false;
        bool injected = false;
        {
            sed::assets::detail::ScopedStagedAssetFileFaultForTests fault(
                sed::assets::detail::StagedAssetFileFaultPointForTests::publish);
            try
            {
                sed::assets::commitJklExport(level, built, path);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            injected = fault.triggered();
        }
        check(injected && threw
                && sed::assets::jklSourceSha256Hex(readTextFile(path))
                    == hashBefore
                && level.materials == materialsBefore
                && countStagedFiles(root) == 0,
            "JKL transaction: publish failure leaves previous hash unchanged");

        sed::assets::commitJklExport(level, built, path);
        check(readTextFile(path) == built.text,
            "JKL transaction: retry after publish failure is identical");
    }

    std::filesystem::remove_all(root, error);
}

} // namespace sed::tests
