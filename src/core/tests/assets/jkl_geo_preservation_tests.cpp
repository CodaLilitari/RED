#include "test_common.hpp"

#include "sed/assets/jed.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/level.hpp"

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

// Returns the record lines following a header line such as
// "WORLD VERTICES 4".
std::vector<std::string> recordsAfter(
    const std::string& text, const std::string& header, std::size_t count)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    bool collecting = false;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (collecting)
        {
            lines.push_back(line);
            if (lines.size() == count)
            {
                break;
            }
        }
        else if (line.rfind(header, 0) == 0)
        {
            collecting = true;
        }
    }
    return lines;
}

// Original synthetic fixture. World vertices, texture vertices,
// adjoins and surfaces are deliberately not in sector/first-use
// order, and the material table duplicates one name across slots 0
// and 1 so an exact slot differs from a first-name match.
const std::string kGeoSource =
    "SECTION: HEADER\n"
    "VERSION 1\n"
    "X-FUTURE-HEADER 7\n"
    "END\n"
    "SECTION: MATERIALS\n"
    "WORLD MATERIALS 3\n"
    "0: dup.mat 1 1\n"
    "1: dup.mat 1 1\n"
    "2: other.mat 1 1\n"
    "END\n"
    "SECTION: GEORESOURCE\n"
    "WORLD COLORMAPS 1\n"
    "0: dflt.cmp\n"
    "WORLD VERTICES 4\n"
    "0: 1 1 0\n"
    "1: 1 0 0\n"
    "2: 0 0 0\n"
    "3: 0 1 0\n"
    "WORLD TEXTURE VERTICES 3\n"
    "0: 0.5 0.25\n"
    "1: -0 0\n"
    "2: 0 1\n"
    "WORLD ADJOINS 2\n"
    "0: 0x1 1 0.00\n"
    "1: 0x1 0 0.00\n"
    "WORLD SURFACES 2\n"
    "0: 0 0x0 0x0 0 0 0 1 1 3 2,2 1,1 0,0 1 1 1\n"
    "0: 0 0 1\n"
    "1: 1 0x0 0x0 0 0 0 0 1 3 3,0 2,2 1,1 1 1 1\n"
    "0: 0 0 1\n"
    "END\n"
    "SECTION: SECTORS\n"
    "WORLD SECTORS 2\n"
    "SECTOR 0\n"
    "FLAGS 0x0\n"
    "COLORMAP 0\n"
    "X-FUTURE-SECTOR 9\n"
    "SURFACES 1 1\n"
    "SECTOR 1\n"
    "FLAGS 0x0\n"
    "COLORMAP 0\n"
    "SURFACES 0 1\n"
    "END\n";

sed::Level loadGeoLevel()
{
    std::istringstream input(kGeoSource);
    return sed::assets::loadFromJKL(input);
}

} // namespace

void runJklGeoPreservationTests()
{
    const std::string unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sed_jkl_geo_" + unique);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    check(!error, "JKL GEO preservation: temporary directory is created");

    // (a)+(b)+(c): a non-topology edit keeps every imported wire slot.
    {
        sed::Level level = loadGeoLevel();
        check(level.sectors.size() == 2
                && level.importedGeoSurfaceCount == 2
                && level.importedGeoVertexIndices.size() == 2,
            "JKL GEO preservation: import captures wire-slot identity");
        level.header.gravity = 2.0;
        if (level.sectors.size() == 2 && !level.sectors[0].vertices.empty())
        {
            // Move sector 0's first vertex (world slot 3): the slot
            // number must survive with the new coordinates.
            level.sectors[0].vertices[0].y = 2.0;
        }

        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        check(!built.geoResourceRenumbered
                && built.geoResourceRenumberReason.empty(),
            "JKL GEO preservation: edit keeps slots, no renumber reported");

        const std::vector<std::string> vertices =
            recordsAfter(built.text, "WORLD VERTICES", 4);
        check(vertices.size() == 4
                && vertices[0] == "0: 1.00000000 1.00000000 0.00000000"
                && vertices[1] == "1: 1.00000000 0.00000000 0.00000000"
                && vertices[2] == "2: 0.00000000 0.00000000 0.00000000"
                && vertices[3] == "3: 0.00000000 2.00000000 0.00000000",
            "JKL GEO preservation: world vertex slots keep imported order");

        const std::vector<std::string> texVertices =
            recordsAfter(built.text, "WORLD TEXTURE VERTICES", 3);
        check(texVertices.size() == 3
                && texVertices[0] == "0: 0.50000000 0.25000000"
                && texVertices[2] == "2: 0.00000000 1.00000000",
            "JKL GEO preservation: texture vertex slots keep imported order");
        check(texVertices.size() == 3
                && texVertices[1].rfind("1: -0.00000000", 0) == 0,
            "JKL GEO preservation: signed-zero texture slot is preserved");

        const std::vector<std::string> surfaces =
            recordsAfter(built.text, "WORLD SURFACES", 2);
        check(surfaces.size() == 2
                && surfaces[0].rfind("0: 0 ", 0) == 0
                && surfaces[0].find("2,2 1,1 0,0") != std::string::npos
                && surfaces[1].rfind("1: 1 ", 0) == 0
                && surfaces[1].find("3,0 2,2 1,1") != std::string::npos,
            "JKL GEO preservation: surface slots and pairs keep imported order");

        // (b) exact material slots: sector 1's surface keeps slot 0
        // and sector 0's surface keeps slot 1 of the duplicated name.
        check(built.text.find("0: dup.mat") != std::string::npos
                && built.text.find("1: dup.mat") != std::string::npos,
            "JKL GEO preservation: duplicate material names keep both slots");

        // (c) unknown fields inside modeled sections survive verbatim.
        check(level.retainedJklHeaderUnknownLines.size() == 1
                && built.text.find("X-FUTURE-HEADER 7") != std::string::npos
                && built.text.find("X-FUTURE-SECTOR 9") != std::string::npos,
            "JKL GEO preservation: unknown in-section fields are re-emitted");

        std::istringstream input(built.text);
        const sed::Level reloaded = sed::assets::loadFromJKL(input);
        check(reloaded.sectors.size() == 2
                && reloaded.retainedJklHeaderUnknownLines.size() == 1
                && reloaded.retainedJklSectorUnknownLines.size() == 2
                && !reloaded.retainedJklSectorUnknownLines[0].empty(),
            "JKL GEO preservation: unknown fields survive a round-trip");
        check(sed::assets::buildJklExport(reloaded).text == built.text,
            "JKL GEO preservation: preserved export is byte-stable");
    }

    // (a) a topology change renumbers and says so on the result.
    {
        sed::Level level = loadGeoLevel();
        level.sectors[0].addVertex(5.0, 5.0, 5.0);
        const sed::assets::JklBuiltExport built =
            sed::assets::buildJklExport(level);
        check(built.geoResourceRenumbered
                && !built.geoResourceRenumberReason.empty(),
            "JKL GEO preservation: topology change reports explicit renumber");
        std::istringstream input(built.text);
        const sed::Level reloaded = sed::assets::loadFromJKL(input);
        check(reloaded.sectors.size() == 2,
            "JKL GEO preservation: renumbered export still reloads");
    }

    // (b) a new surface naming a duplicated material refuses rather
    // than guessing the first duplicate slot.
    {
        sed::Level level = loadGeoLevel();
        sed::Surface surface{};
        surface.material = "dup.mat";
        surface.vertices = { 0, 1, 2 };
        surface.texVertices.resize(3u);
        surface.normal = sed::TVector{ 0.0, 0.0, 1.0 };
        level.sectors[0].addSurface(std::move(surface));
        bool threw = false;
        try
        {
            (void)sed::assets::buildJklExport(level);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        check(threw,
            "JKL GEO preservation: ambiguous duplicate material refuses");
    }

    // (d) .ned companion: a .ned-named file is the JED editor text
    // format (the agent dispatches .jed/.ned to the same codec).
    // Prove its round-trip with a synthetic fixture on disk.
    {
        sed::Level src{};
        src.clear();
        src.kind = sed::ProjectType::JKDF2;
        src.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
        src.header.version = 1;
        src.header.gravity = 4.0;
        sed::Sector& sector = src.newSector();
        sector.addVertex(0.0, 0.0, 0.0);
        sector.addVertex(1.0, 0.0, 0.0);
        sector.addVertex(0.0, 1.0, 0.0);
        sed::Surface surface{};
        surface.material = "ned.mat";
        surface.vertices = { 0, 1, 2 };
        surface.texVertices.resize(3u);
        sector.addSurface(std::move(surface));

        const std::filesystem::path nedPath = root / "companion.ned";
        sed::assets::saveToJED(src, nedPath);
        const sed::Level loaded = sed::assets::loadFromJED(nedPath);
        check(loaded.sectors.size() == 1
                && loaded.sectors[0].surfaces.size() == 1
                && close(loaded.header.gravity, 4.0),
            "JKL GEO preservation: .ned companion loads through JED codec");

        const std::filesystem::path nedPath2 = root / "companion2.ned";
        sed::assets::saveToJED(loaded, nedPath2);
        const sed::Level reloaded = sed::assets::loadFromJED(nedPath2);
        check(reloaded.sectors.size() == 1
                && reloaded.sectors[0].vertices.size() == 3
                && readTextFile(nedPath2) == readTextFile(nedPath),
            "JKL GEO preservation: .ned companion round-trip is byte-stable");
    }

    std::filesystem::remove_all(root, error);
}

} // namespace sed::tests
