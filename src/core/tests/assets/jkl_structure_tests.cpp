#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/jkl_structure.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>

namespace sed::tests
{
namespace
{

// Original synthetic single-triangle wire fixture, with no game assets.
std::string fixture()
{
    return "SECTION: MATERIALS\n"
           "WORLD MATERIALS 1\n"
           "0: synthetic.mat 1 1\n"
           "END\n"
           "SECTION: GEORESOURCE\n"
           "WORLD COLORMAPS 0\n"
           "WORLD VERTICES 3\n"
           "0: 0 0 0\n"
           "1: 1 0 0\n"
           "2: 0 1 0\n"
           "WORLD TEXTURE VERTICES 3\n"
           "0: 0 0\n"
           "1: 1 0\n"
           "2: 0 1\n"
           "WORLD ADJOINS 0\n"
           "WORLD SURFACES 1\n"
           "0: 0 0x0 0x0 3 0 1 -1 0 3 0,0 1,1 2,2 1 1 1\n"
           "0: 0 0 1\n"
           "END\n";
}

std::string replaced(std::string text, const std::string_view oldText,
    const std::string_view newText)
{
    const auto position = text.find(oldText);
    check(position != std::string::npos, "JKL structure fixture mutation exists");
    if ( position != std::string::npos )
    {
        text.replace(position, oldText.size(), newText);
    }
    return text;
}

bool finding(const assets::JklStructureReport& report, const std::string_view code,
    const std::string_view section, const std::size_t line)
{
    return !report.ok() && std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
        [&](const assets::JklStructureDiagnostic& diagnostic)
        {
            return diagnostic.code == code && diagnostic.section == section
                && diagnostic.line == line && !diagnostic.message.empty();
        });
}

} // namespace

void runJklStructureTests()
{
    using assets::inspectJklStructure;
    const std::string valid = fixture();
    const auto scoped = inspectJklStructure(valid);
    check(scoped.inspectedMaterials && scoped.inspectedGeoResource,
        "JKL structure identifies the supported sections actually inspected");
    for ( const auto input : { "", "arbitrary text", "SECTION: UNKNOWN\nopaque\n" } )
    {
        const auto unsupported = inspectJklStructure(input);
        check(!unsupported.inspectedMaterials && !unsupported.inspectedGeoResource,
            "JKL structure does not imply whole-document completeness for unsupported input");
    }
    check(inspectJklStructure(valid).ok(), "JKL structure accepts grouped normals and exact material capacity");
    check(inspectJklStructure(replaced(valid, "WORLD MATERIALS 1", "WORLD MATERIALS 64")).ok(),
        "JKL structure accepts intentional material spare capacity");
    check(inspectJklStructure("SECTION: MATERIALS\nWORLD MATERIALS 0\nEND\n").ok(),
        "JKL structure accepts empty zero-capacity materials");
    const std::string emptyGeometry =
        "SECTION: GEORESOURCE\n"
        "WORLD COLORMAPS 0\n"
        "WORLD VERTICES 0\n"
        "WORLD TEXTURE VERTICES 0\n"
        "WORLD ADJOINS 0\n"
        "WORLD SURFACES 0\n"
        "END\n";
    check(inspectJklStructure(emptyGeometry).ok(),
        "JKL structure accepts complete ordered classic zero-count geometry headers");
    check(inspectJklStructure(replaced(emptyGeometry, "END\n", "")).ok(),
        "JKL structure accepts complete zero-surface geometry without END");
    for ( const auto header : { "WORLD COLORMAPS 0\n", "WORLD VERTICES 0\n",
              "WORLD TEXTURE VERTICES 0\n", "WORLD ADJOINS 0\n", "WORLD SURFACES 0\n" } )
    {
        check(finding(inspectJklStructure(replaced(emptyGeometry, header, "")),
                "missing-count", "GEORESOURCE", 1),
            "JKL structure rejects each missing required classic geometry count header");
    }
    check(finding(inspectJklStructure("SECTION: GEORESOURCE\nWORLD ADJOINS 0\nEND\n"),
            "missing-count", "GEORESOURCE", 1),
        "JKL structure rejects an otherwise empty geometry section containing only adjoins");
    check(finding(inspectJklStructure(replaced(emptyGeometry,
                "WORLD VERTICES 0\nWORLD TEXTURE VERTICES 0\n",
                "WORLD TEXTURE VERTICES 0\nWORLD VERTICES 0\n")),
            "count-order", "GEORESOURCE", 3),
        "JKL structure rejects reordered all-zero geometry headers at first wrong header");
    check(inspectJklStructure("SECTION: COPYRIGHT\nOriginal synthetic text\n" + valid
            + "SECTION: FUTURE_EXTENSION\nopaque payload\n").ok(),
        "JKL structure does not invent terminator contracts for copyright or extensions");

    const std::string missingEnd = replaced(valid, "END\nSECTION: GEORESOURCE", "SECTION: GEORESOURCE");
    check(finding(inspectJklStructure(missingEnd), "missing-terminator", "MATERIALS", 1),
        "JKL structure locates missing material END before next section");
    check(finding(inspectJklStructure("SECTION: MATERIALS\nWORLD MATERIALS 1\n0: synthetic.mat 1 1\n"),
            "missing-terminator", "MATERIALS", 1),
        "JKL structure locates missing material END at EOF");
    check(finding(inspectJklStructure(replaced(valid, "WORLD MATERIALS 1", "WORLD MATERIALS -1")),
            "invalid-count", "MATERIALS", 2), "JKL structure rejects negative capacity");
    check(finding(inspectJklStructure(replaced(valid, "WORLD MATERIALS 1", "WORLD MATERIALS 9999999999999999999999")),
            "invalid-count", "MATERIALS", 2), "JKL structure bounds overflowing counts without allocation");
    check(finding(inspectJklStructure(replaced(valid, "WORLD MATERIALS 1", "WORLD MATERIALS 1garbage")),
            "invalid-count", "MATERIALS", 2), "JKL structure rejects partial integer parsing");
    check(finding(inspectJklStructure(replaced(valid, "WORLD MATERIALS 1", "WORLD MATERIALS 0")),
            "capacity-exceeded", "MATERIALS", 2), "JKL structure rejects rows beyond material capacity");
    check(finding(inspectJklStructure(replaced(valid, "WORLD MATERIALS 1\n", "")),
            "missing-count", "MATERIALS", 1), "JKL structure reports absent material capacity header");

    check(finding(inspectJklStructure(replaced(valid, "WORLD VERTICES 3", "WORLD VERTICES 4")),
            "count-mismatch", "GEORESOURCE", 7), "JKL structure reports short exact vertex block");
    check(finding(inspectJklStructure(replaced(valid, "WORLD VERTICES 3", "WORLD VERTICES 2")),
            "count-mismatch", "GEORESOURCE", 7), "JKL structure reports excess exact vertex records");
    check(finding(inspectJklStructure(replaced(valid, "WORLD ADJOINS 0", "WORLD ADJOINS nope")),
            "invalid-count", "GEORESOURCE", 15), "JKL structure reports malformed geometry count");
    check(finding(inspectJklStructure(replaced(valid, "WORLD TEXTURE VERTICES 3", "WORLD TEXTURE WRONG 3")),
            "invalid-count", "GEORESOURCE", 11), "JKL structure rejects malformed texture count keywords");
    check(finding(inspectJklStructure(replaced(valid, "WORLD SURFACES 1", "WORLD SURFACES 2")),
            "count-mismatch", "GEORESOURCE", 16), "JKL structure distinguishes normals from surface records");

    const std::string missingNormals = replaced(valid, "0: 0 0 1\nEND", "END");
    check(finding(inspectJklStructure(missingNormals), "missing-normal", "GEORESOURCE", 16),
        "JKL structure locates missing normals at surface header");
    check(finding(inspectJklStructure(replaced(valid, "0: 0 0 1\nEND", "0: 0 0 1\n1: 0 0 1\nEND")),
            "count-mismatch", "GEORESOURCE", 16), "JKL structure rejects extra grouped normal rows");
    // Engine sithSurface_ReadSurfacesListText returns after the counted normals;
    // GEORESOURCE is count-delimited, unlike sentinel-delimited MATERIALS.
    const std::string countedGeometry = replaced(valid, "0: 0 0 1\nEND\n", "0: 0 0 1");
    check(inspectJklStructure(countedGeometry).ok(),
        "JKL structure accepts complete counted geometry at EOF without END");
    check(inspectJklStructure(countedGeometry + "\nSECTION: SECTORS\nWORLD SECTORS 0\nEND\n").ok(),
        "JKL structure accepts complete counted geometry before next SECTION without END");
    check(finding(inspectJklStructure(replaced(valid, "0: 0 0 1\nEND\n", "")),
            "missing-normal", "GEORESOURCE", 16),
        "JKL structure still rejects missing normals when geometry has no END");
    for ( const auto normal : { "0: 0 nope 1", "0: 0 0", "0: nan 0 1", "0: 0 inf 1" } )
    {
        check(finding(inspectJklStructure(replaced(valid, "0: 0 0 1\nEND", std::string(normal) + "\nEND")),
                "invalid-normal", "GEORESOURCE", 18), "JKL structure locates malformed or nonfinite normal");
    }

    // Strict inspection must not change the permissive compatibility importer.
    std::istringstream compatibilityInput(missingNormals);
    try
    {
        (void)assets::loadFromJKL(compatibilityInput);
        check(true, "JKL compatibility import still accepts missing redundant normals");
    }
    catch (...)
    {
        check(false, "JKL compatibility import still accepts missing redundant normals");
    }
    check(valid == fixture(), "JKL structure inspection leaves input unchanged");

    const std::string commented = "# synthetic preamble\r\n"
        "section: materials\r\nworld materials 1 # capacity\r\n0: synthetic.mat 1 1\r\nend\r\n";
    check(inspectJklStructure(commented).ok(), "JKL structure accepts CRLF comments and lowercase keywords");
    check(finding(inspectJklStructure("# comment\nSECTION: MATERIALS\nWORLD MATERIALS -1\nEND\n"),
            "invalid-count", "MATERIALS", 3), "JKL structure reports physical line numbers including comments");

    assets::JklStructureOptions limits{};
    limits.maxSourceBytes = valid.size() - 1;
    const auto sourceLimit = inspectJklStructure(valid, limits);
    check(!sourceLimit.complete && finding(sourceLimit, "source-limit", "", 1),
        "JKL structure fails closed on source budget");
    limits = {};
    limits.maxLineBytes = 8;
    const auto lineLimit = inspectJklStructure(valid, limits);
    check(!lineLimit.complete && finding(lineLimit, "line-limit", "", 1),
        "JKL structure fails closed on line budget");
    limits = {};
    limits.maxDiagnostics = 1;
    const auto diagnosticLimit = inspectJklStructure(
        "SECTION: MATERIALS\nWORLD MATERIALS nope\n", limits);
    check(!diagnosticLimit.complete && diagnosticLimit.diagnostics.size() == 1 && !diagnosticLimit.ok(),
        "JKL structure bounds findings and marks incomplete scan");
    limits.maxDiagnostics = 0;
    const auto noDiagnosticRoom = inspectJklStructure(valid, limits);
    check(!noDiagnosticRoom.complete && !noDiagnosticRoom.ok(),
        "JKL structure cannot report success with a zero diagnostic budget");
}

} // namespace sed::tests
