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

void checkEntityTables()
{
    using assets::inspectJklStructure;
    const std::string templates = "section: templates\r\nworld templates 2\r\n"
        "; editor comment\r\nbase none type=actor\r\nchild base \\\r\n"
        "# comment between pieces\r\nmodel3d=child.3do # inline\r\nend\r\n";
    check(!inspectJklStructure(templates).ok(),
        "strict preflight refuses native logical rows split by the physical compatibility reader");
    check(!inspectJklStructure("SECTION:TEMPLATES\nWORLD TEM\\\n; comment\nPLATES,+1\n"
            "child none\nEND\n").ok(),
        "strict preflight refuses a continued declaration the compatibility reader cannot decode");
    check(finding(inspectJklStructure(replaced(templates, "templates 2", "templates 1")),
            "capacity-exceeded", "TEMPLATES", 2),
        "strict inspection exposes a template compatibility import would discard");
    // Read each fixture through the real compatibility importer to witness the rows
    // it will expose after raw validation. Only the canonical table preserves both.
    const std::string control = "SECTION: TEMPLATES\nWORLD TEMPLATES 2\nbase none\nchild base\nEND\n";
    const auto names = [](const std::string& source)
    {
        std::istringstream input(source);
        const auto level = assets::loadFromJKL(input);
        std::vector<std::string> result;
        for ( const auto& item : level.templates ) result.push_back(item.name);
        return result;
    };
    check(names(control) == std::vector<std::string>{"base", "child"} && inspectJklStructure(control).ok(),
        "canonical template framing preserves both authored names");
    for ( const auto& different : {
            replaced(control, "base none", "; editor comment\nbase none"),
            replaced(control, "TEMPLATES 2", "TEMPLATES +2"),
            replaced(control, "TEMPLATES 2", "TEMPLATES,2"),
            replaced(control, "base none", "base,none"),
            replaced(control, "base none", ",,,\nbase none"),
            replaced(control, "SECTION: TEMPLATES", "SECTION:TEMPLATES"),
            replaced(control, "base none", "base none \\\nmodel3d=base.3do")} )
    {
        check(names(different) != names(control), "fixture actually changes physical template import");
        check(!inspectJklStructure(different).ok(), "strict preflight must refuse an inconsistent template read");
    }
    const auto contradictory = replaced(replaced(control, "SECTION: TEMPLATES", "SECTION:other TEMPLATES"),
                                        "TEMPLATES 2", "TEMPLATES 1");
    check(names(contradictory) == std::vector<std::string>{"base"} && !inspectJklStructure(contradictory).ok(),
        "strict preflight catches an overflowing section named only by the compatibility reader");
    const auto inlineJoin = replaced(replaced(control, "SECTION: TEMPLATES", "SECT\\ # comment\nION: TEMPLATES"),
                                     "TEMPLATES 2", "TEMPLATES 1");
    check(!inspectJklStructure(inlineJoin).ok(), "native inline-comment continuation cannot conceal an entity table");
    assets::JklStructureOptions commentLimit;
    commentLimit.maxLineBytes = 18;
    const auto longJoin = inspectJklStructure("0123456789\\ #x\n0123456789\n", commentLimit);
    check(!longJoin.complete && finding(longJoin, "line-limit", "", 1),
        "inline-comment continuations count against the joined logical byte budget");
    check(inspectJklStructure("SECTION: SOUNDS\nWORLD SOUNDS 1\nBell\\ # joined\nTone.wav 0\nEND\n" + control).ok(),
        "native sound preloads retain their own logical rows outside the strict entity tables");
    for ( const std::string section : { "TEMPLATES", "MODELS", "THINGS" } )
    {
        const std::string start = "SECTION: " + section + "\n";
        const std::string header = "WORLD " + section + " ";
        const auto table = [&](const std::string& body) { return start + body + "END\n"; };
        check(inspectJklStructure(table(header + "0\n")).ok(),
            "strict entity tables accept empty zero capacity");
        check(inspectJklStructure(table(header + "2147483647\n")).ok(),
            "strict entity tables accept spare signed-32-bit capacity without allocating slots");
        check(finding(inspectJklStructure(table(header + "2147483648\n")),
                "invalid-count", section, 2), "strict entity tables bound capacity");
        check(finding(inspectJklStructure(table(header + "-1\n")),
                "invalid-count", section, 2), "strict entity tables reject negative capacity");
        check(finding(inspectJklStructure(table("")), "missing-count", section, 1),
            "strict entity tables require a declaration when present");
        check(finding(inspectJklStructure(table("0: early\n" + header + "2\n")),
                "count-order", section, 2), "strict entity rows cannot precede their count");
        check(finding(inspectJklStructure(table(header + "1\n" + header + "2\n")),
                "duplicate-count", section, 3), "strict entity tables reject duplicate declarations");
        check(finding(inspectJklStructure(start + header + "1\nSECTION: OTHER\n"),
                "missing-terminator", section, 1), "strict entity tables require END at a section boundary");
        check(finding(inspectJklStructure(table(header + "0\n") + table(header + "0\n")),
                "duplicate-section", section, 4), "strict entity tables report duplicate sections");
    }
    const std::string overloaded = "WORLD TEMPLATES 1\nbase none type=actor\nextra base\nEND\n";
    for ( const std::string header : { "SECTION: TEMPLATES annotations", "SECTION:\vTEMPLATES",
              "\fSECTION:\fTEMPLATES\tannotations" } )
    {
        check(finding(inspectJklStructure(header + "\n" + overloaded), "capacity-exceeded", "TEMPLATES", 2),
            "native first section token selects a known table despite whitespace or trailing annotations");
    }
    const std::string header = "SECTION: TEMPLATES";
    for ( std::size_t split = 1; split < header.size(); ++split )
    {
        const auto joined = header.substr(0, split) + "\\\n; comment between pieces\n" + header.substr(split);
        check(finding(inspectJklStructure(joined + "\n" + overloaded), "capacity-exceeded", "TEMPLATES", 4),
            "a continued section keyword or name cannot hide a capacity error");
    }
    const auto nul = inspectJklStructure(header + '\0' + "ignored\n" + overloaded);
    check(!nul.complete && finding(nul, "embedded-nul", "", 1),
        "strict text inspection fails closed on a NUL before dispatch");
    const auto hiddenHeader = inspectJklStructure("opaque\\\n" + header + "\n" + overloaded);
    check(!hiddenHeader.complete && finding(hiddenHeader, "ambiguous-section", "", 2),
        "strict preflight refuses a physical header swallowed by a previous logical record");
    for ( const std::string terminator : { "end trailing", "END,more", "tag=EnD metadata" } )
    {
        const auto report = inspectJklStructure(header + "\nWORLD TEMPLATES 1\n" + terminator + "\n");
        check(finding(report, "ambiguous-terminator", "TEMPLATES", 3),
            "native END aliases incompatible with RED import fail closed");
    }

    const std::string models = "SECTION: MODELS\nWORLD MODELS 2\n17: a.3do\n17: b.3do\nEND\n";
    check(inspectJklStructure(models).ok(), "model labels are not runtime slot addresses");
    check(finding(inspectJklStructure(replaced(models, "MODELS 2", "MODELS 1")),
            "capacity-exceeded", "MODELS", 2), "strict authoring bounds model rows");
    const std::string things = "SECTION: THINGS\nWORLD THINGS 9\n"
        "8: prop same 0 0 0 0 0 0 0\n2: prop same 0 0 0 0 0 0 0\nEND\n";
    check(inspectJklStructure(things).ok(), "sparse reordered Things may share a descriptive name");
    check(finding(inspectJklStructure(replaced(things, "8:", "9:")),
            "thing-slot-out-of-range", "THINGS", 3), "Thing slot must be below capacity even with spare rows");
    check(finding(inspectJklStructure(replaced(things, "8:", "-1:")),
            "invalid-thing-slot", "THINGS", 3), "Thing slot must be nonnegative");
    check(finding(inspectJklStructure(replaced(things, "2:", "8:")),
            "duplicate-thing-slot", "THINGS", 4), "Thing slot can be authored only once");
    check(finding(inspectJklStructure(replaced(things, "THINGS 9", "THINGS 1")),
            "capacity-exceeded", "THINGS", 2), "strict authoring bounds Thing rows");
    assets::JklStructureOptions limits{};
    limits.maxLineBytes = 32;
    const auto overBudget = inspectJklStructure("SECTION: TEMPLATES\nWORLD TEMPLATES 1\n"
        "child base type=actor \\\nmodel3d=longname.3do\nEND\n", limits);
    check(!overBudget.complete && finding(overBudget, "line-limit", "TEMPLATES", 3),
        "joined logical rows are bounded at their first physical line");
    limits = {};
    limits.maxTrackedThingSlots = 1;
    const auto slotBudget = inspectJklStructure(things, limits);
    check(!slotBudget.complete && finding(slotBudget, "thing-slot-limit", "THINGS", 4),
        "sparse slot accounting has an explicit distinct-slot inspection budget");
    const auto unfinished = inspectJklStructure("SECTION: TEMPLATES\nWORLD TEMPLATES 2\n"
        "prop none \\\n; trailing comment\n");
    check(!unfinished.complete && finding(unfinished, "unfinished-continuation", "TEMPLATES", 3),
        "continued row ending at EOF fails closed");
}

} // namespace

void runJklStructureTests()
{
    using assets::inspectJklStructure;
    checkEntityTables();
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
    const std::string lateMaterials = "SECTION: MATERIALS\njunk\nWORLD MATERIALS 0\nEND\n";
    std::istringstream materialInput(lateMaterials);
    check(assets::loadFromJKL(materialInput).materials == std::vector<std::string>{"WORLD MATERIALS 0"},
        "compatibility consumes a late material capacity header as a material name");
    check(finding(inspectJklStructure(lateMaterials), "count-order", "MATERIALS", 2),
        "strict validation requires the material capacity before data");
    const auto lateGeometry = replaced(emptyGeometry, "WORLD COLORMAPS", "stray 1\nWORLD COLORMAPS");
    std::istringstream geoInput(lateGeometry);
    check(assets::loadFromJKL(geoInput).colormaps == std::vector<std::string>{"COLORMAPS"},
        "compatibility consumes a late geometry count header as a colormap");
    check(finding(inspectJklStructure(lateGeometry), "count-order", "GEORESOURCE", 2),
        "strict validation requires the first geometry count before data");
    const auto continuedBoundary = replaced(emptyGeometry, "END\n",
        "SEC\\\nTION: EXTENSION\nopaque payload\n");
    check(finding(inspectJklStructure(continuedBoundary), "ambiguous-section", "GEORESOURCE", 7),
        "an optional geometry terminator cannot hide differing section boundaries");
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
