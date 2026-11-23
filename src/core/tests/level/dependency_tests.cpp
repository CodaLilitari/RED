#include "sed/level/dependencies.hpp"
#include "sed/assets/jkl.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
using namespace sed::level;
int failures{};

void check(bool condition, const char* name)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

std::string hash(std::string_view bytes)
{
    return sed::assets::jklSourceSha256Hex(bytes);
}

DependencyArchiveInput archive(std::vector<std::pair<std::string, std::string>> entries,
    bool stock = false, std::string label = "resources.gob")
{
    const auto bytes = sed::assets::buildGobBytes(entries);
    std::vector<std::byte> raw(bytes.size());
    std::memcpy(raw.data(), bytes.data(), bytes.size());
    return {sed::assets::GobArchive::fromBytes(std::move(raw)), std::move(label), stock};
}

struct Fixture
{
    std::string source{"SECTION: MODELS\nWorld Models 1\ncrate.3do\nEND\n"};
    std::vector<DependencyArchiveInput> archives{
        archive({{"3do/crate.3do", "wall.mat\n"}, {"mat/wall.mat", "abc"}})};
    std::vector<PackageExtraResource> extra;
    DependencyLimits limits;

    DependencyInspection inspect() const
    {
        return inspectDependencies({source, "level.jkl", archives, extra, limits});
    }
};

bool reason(const std::vector<std::string>& reasons, std::string_view expected)
{
    return std::find(reasons.begin(), reasons.end(), expected) != reasons.end();
}

void chain()
{
    Fixture f;
    const auto g = f.inspect();
    check(g.scanComplete && g.detailsComplete && g.allReferencesResolved == true
        && g.counts.nodes == 3 && g.counts.edges == 2 && g.counts.issues == 0,
        "chain has exact completed counts and independent success flags");
    check(g.inputSha256 == hash(f.source) && g.sourceArchives.size() == 1,
        "chain binds source and archives to captured hashes");
    if ( g.nodes.size() == 3 && g.edges.size() == 2 && g.sourceArchives.size() == 1 )
    {
        check(g.nodes[0].id == 0 && g.nodes[0].path == "jkl/level.jkl"
            && g.nodes[0].resolution == DependencyResolution::Source
            && g.nodes[1].path == "3do/crate.3do" && g.nodes[2].path == "mat/wall.mat",
            "chain node identity and discovery order");
        check(g.edges[0].parent == 0 && g.edges[0].target == 1
            && g.edges[0].relation == "jklModel" && g.edges[0].authoredName == "crate.3do"
            && g.edges[1].parent == 1 && g.edges[1].target == 2
            && g.edges[1].relation == "modelMaterial" && !g.edges[1].reused,
            "chain retains exact parents, relations and authored spellings");
        check(g.nodes[2].sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "selected material payload hash uses exact bytes");
        const auto& p = g.nodes[2].provenance;
        check(p && p->archiveOrdinal == 0 && p->entryOrdinal == 1 && p->offset == 297
            && p->bytes == 3 && p->entryName == "mat\\wall.mat" && p->lookupTier == "folderExact",
            "chain provenance uses physical row metadata");
        check(g.sourceArchives[0].bytes == 300 && g.sourceArchives[0].entries == 2
            && g.sourceArchives[0].sha256 == hash(sed::assets::buildGobBytes(
                {{"3do/crate.3do", "wall.mat\n"}, {"mat/wall.mat", "abc"}})),
            "archive evidence hashes the raw GOB, not materialized entries");
    }
    else
    {
        check(false, "chain returns every expected graph row");
    }
    check(g.usage.workUnits == 13 && g.usage.indexEntries == 2 && g.usage.resourceBytes == 12
        && g.usage.scanBytes == f.source.size() + 9 && g.usage.peakPending == 1,
        "chain work accounting has hand-derived units");
    check(g.usage.stringBytes == 404 && g.usage.detailBytes == 3974,
        "chain string and detail accounting match independently derived byte totals");
}

void sectorAmbientSounds()
{
    Fixture f;
    f.source =
        "SECTION: SOUNDS\nWorld Sounds 4\nfan.wav\nEND\n"
        "Section: Sectors # ambient emitters\nWorld Sectors 2 trailing\nSOUND before.wav 1\n"
        "Sector 0\nSoUnD\tFan.WAV\t0.75 # humming.wav\nSURFACES 0 0\n"
        "Sector 1 trailing\nSOUND Vent.wav 1\n"
        "# SOUND commented.wav 0.5\nSOUNDTRACK music.wav 1\n"
        "SOUND none 0\nSOUND no-volume.wav # 1\nSURFACES 0 0\n"
        "SOUND after-span.wav 1\nSECTOR 2\nSOUND beyond-count.wav 1\n"
        "END # finished sectors\nSOUND outside.wav 1\n"
        "SECTION: OTHER\nheader\nSOUND other.wav 1\nEND\n";
    f.archives = {archive({{"sound/fan.wav", "fan"}, {"sound/vent.wav", "vent"}})};
    const auto g = f.inspect();
    check(g.scanComplete && g.allReferencesResolved == true
        && g.counts.nodes == 3 && g.edges.size() == 3,
        "ambient roots include only sector SOUND rows with filename and volume");
    if ( g.edges.size() == 3 )
    {
        check(g.edges[0].relation == "jklSound" && !g.edges[0].reused
            && g.edges[1].relation == "sectorAmbientSound" && g.edges[1].reused
            && g.edges[1].target == g.edges[0].target && g.edges[1].authoredName == "Fan.WAV"
            && g.edges[2].relation == "sectorAmbientSound" && !g.edges[2].reused
            && g.edges[2].parent == 0 && g.edges[2].folder == "sound"
            && g.edges[2].authoredName == "Vent.wav",
            "sector references retain spelling and order and share preloaded sound nodes");
    }
    f.limits.maxEdges = 1;
    const auto stopped = f.inspect();
    check(!stopped.scanComplete && reason(stopped.scanReasons, "maxEdges")
        && stopped.edges.size() == 1,
        "sector ambient references respect the shared edge limit");

    f.archives.clear();
    f.source = "SECTION: SECTORS\nWORLD SECTORS 1\nSECTOR 0\nSOUND lost.wav 1\nEND\n";
    f.limits = {};
    const auto missing = f.inspect();
    check(missing.scanComplete && missing.allReferencesResolved == false && missing.counts.missing == 1
        && missing.edges.size() == 1 && missing.edges[0].authoredName == "lost.wav",
        "inspection identifies the missing ambient filename even without a preload row");
    // Four physical lines, three WORLD tokens, two SECTOR tokens and two SOUND
    // tokens exhaust this allowance before the volume can complete the reference.
    f.limits.maxWorkUnits = 11;
    const auto interrupted = f.inspect();
    check(!interrupted.scanComplete && reason(interrupted.scanReasons, "maxWorkUnits")
        && interrupted.usage.workUnits == 11 && interrupted.edges.empty(),
        "an exhausted token budget stops before admitting a partial SOUND row");
}

void sectorSoundGrammar()
{
    Fixture f;
    f.archives = {archive({{"sound/vent.wav", "vent"}})};
    const auto inspectAmbient = [&](const std::string& source, const char* message)
    {
        f.source = source + "SURFACES 0 0\nEND\n";
        std::istringstream input(f.source);
        const auto level = sed::assets::loadFromJKL(input);
        const auto graph = f.inspect();
        check(level.sectors.size() == 1 && level.sectors[0].sound == "vent.wav"
            && graph.scanComplete && graph.allReferencesResolved == true && graph.edges.size() == 1
            && graph.edges[0].authoredName == level.sectors[0].sound, message);
    };
    inspectAmbient("SECTION: SECTORS metadata\nWORLD SECTORS 1\nSECTOR 0\nSOUND vent.wav 1\n",
        "trailing section header fields do not hide ambient sounds");
    inspectAmbient("SECTION: SECTORS\nWORLD\vSECTORS\f1\nSECTOR\f0\nSOUND\vvent.wav\f1\n",
        "sector record separators include vertical tab and form feed");
    inspectAmbient("SECTION: SECTORS\nWORLD SECTORS 1\nSECTOR 0\nVERTICES 1\n"
        "# a comment is not a counted vertex row\nSOUND vertex-row.wav 1\nSOUND vent.wav 0.75\n",
        "the counted sector vertex payload is skipped before scanning later properties");
    inspectAmbient("SECTION:SECTORS\nWORLD SECTORS 1\nSECTOR 0\nSOUND ignored.wav 1\nSURFACES 0 0\nEND\n"
        "SECTION: SECTORS\nWORLD SECTORS 1\nSECTOR 0\nSOUND vent.wav 1\n",
        "an attached section name ignored by the level loader cannot create a missing ambient dependency");
    inspectAmbient("SECTION:tag SECTORS # the imported section name is the second word\n"
        "WORLD SECTORS 1\nSECTOR 0\nSOUND vent.wav 1\n",
        "the dependency visitor follows the same section field as the level importer");
}

void compactModelMaterialRows()
{
    const std::string model = "SECTION: MODELRESOURCE\nMATERIALS 2\n0:FIRST.MAT\n1: second.mat\n";
    const std::vector<std::pair<std::string, std::string>> files{
        {"3do/crate.3do", model}, {"mat/FIRST.MAT", "first"}, {"mat/second.mat", "second"}};
    Fixture fixture;
    fixture.archives = {archive(files)};
    const auto graph = fixture.inspect();
    check(graph.allReferencesResolved == true && graph.edges.size() == 3
        && graph.edges[1].authoredName == "FIRST.MAT" && graph.edges[2].authoredName == "second.mat",
        "compact and spaced 3DO material indices are separate from the authored resource names");

    PackageRequest request;
    request.inputBytes = fixture.source;
    request.levelFileName = "crate.jkl";
    request.episodeTitle = "Indexed materials";
    request.archives.push_back({"assets", false, files});
    const auto plan = planLevelPackage(request);
    check(plan.canBuild && std::any_of(plan.entries.begin(), plan.entries.end(), [](const auto& entry)
        { return entry.path == "mat/first.mat"; }), "packaging includes the compact-row MAT at its engine path");
}

void authoredSoundResources()
{
    Fixture fixture;
    fixture.source =
        "SECTION:SOUNDS # ordinary header comment\n"
        "; ignored.wav\nWorld Sounds 1\n"
        "name=Bell.WAV,preload=0\n"
        "bell.wav ; another action\n"
        "Al\\\narm.wav,0 # ignored.wav\n"
        "NONE phantom.wav\n"
        "key=end,ignored.wav\noutside.wav\n";
    const std::vector<std::pair<std::string, std::string>> files{{"sound/bell.wav", "B"}};
    const std::vector<std::pair<std::string, std::string>> stock{{"sound/alarm.wav", "A"}};
    fixture.archives = {archive(files), archive(stock, true)};
    const auto graph = fixture.inspect();
    check(graph.allReferencesResolved == true && graph.edges.size() == 3
        && graph.edges[0].authoredName == "Bell.WAV"
        && graph.edges[1].authoredName == "bell.wav" && graph.edges[1].reused
        && graph.edges[2].authoredName == "Alarm.wav"
        && graph.edges[2].relation == "jklSound" && graph.counts.nodes == 3,
        "authored sound rows expose effective native names, each action edge and one node per resource");

    PackageRequest request;
    request.inputBytes = fixture.source;
    request.levelFileName = "level.jkl";
    request.episodeTitle = "Sound actions";
    request.archives = {{"custom", false, files}, {"stock", true, stock}};
    const auto plan = planLevelPackage(request);
    const auto copies = std::count_if(plan.entries.begin(), plan.entries.end(), [](const auto& entry)
    {
        return entry.path == "sound/bell.wav";
    });
    check(plan.canBuild && copies == 1 && plan.stockOmissions.size() == 1
        && plan.stockOmissions[0].path == "sound/alarm.wav",
        "package copies each authored sound once and omits the stock sound named across a continuation");
    request.archives.pop_back();
    const auto missing = planLevelPackage(request);
    const bool missingSound = std::any_of(missing.issues.begin(), missing.issues.end(), [](const auto& issue)
    {
        return issue.code == "MissingResource" && issue.reference == "Alarm.wav";
    });
    check(!missing.canBuild && missingSound,
        "package names the effective continued sound when its archive is missing");

    fixture.archives.clear();
    fixture.source = "SECTION: SOUNDS\nWORLD SOUNDS 1\nAl\\\narm.wav,0\nEND\n";
    for ( const std::size_t budget : {7u, 12u} )
    {
        // Five physical lines identify the dialect. At 7 the header fields
        // cannot start; at 12 the joined filename still cannot be visited.
        fixture.limits.maxWorkUnits = budget;
        const auto stopped = fixture.inspect();
        check(!stopped.scanComplete && !stopped.allReferencesResolved.has_value()
            && reason(stopped.scanReasons, "maxWorkUnits") && stopped.edges.empty()
            && stopped.usage.workUnits == budget,
            "dialect classification and continued name admission stop before emitting a partial sound reference");
    }
    fixture.limits = {};

    const std::string prefix = "NONE ";
    const std::string marker(sed::kJklSoundPreloadMaxRowBytes - prefix.size(), '0');
    fixture.source = "SECTION: SOUNDS\nWORLD SOUNDS 1\n" + prefix + "\\\n" + marker + "\nEND\n";
    check(fixture.inspect().scanComplete, "exact maximum continued sound row fits");
    fixture.source.insert(fixture.source.find("\nEND"), 1, '0');
    bool oversized = false;
    try
    {
        (void)fixture.inspect();
    }
    catch ( const std::invalid_argument& )
    {
        oversized = true;
    }
    check(oversized, "one extra byte refuses an overlong continued sound row before retaining it");

    fixture.source = "SECTION: SOUNDS\nWORLD SOUNDS 0\nignored.wav 0\nEND\n";
    const auto dormant = fixture.inspect();
    check(dormant.allReferencesResolved == true && dormant.edges.empty(),
        "a zero-capacity native sound table has no active resource edges");
}

void soundTableAdmission()
{
    Fixture fixture;
    fixture.archives.clear();
    PackageRequest request;
    request.levelFileName = "level.jkl";
    request.episodeTitle = "Sound table framing";
    const auto load = [&]()
    {
        std::istringstream input(fixture.source);
        return sed::assets::loadFromJKL(input, {.projectType = sed::ProjectType::JKDF2});
    };
    const auto refuses = [](auto&& operation)
    {
        try { operation(); }
        catch ( const std::exception& ) { return true; }
        return false;
    };
    // An ordinary open follows the game, which loads these tables, so it keeps no
    // authored rows instead of failing. Strict reads, dependencies and packaging refuse.
    const auto strictImportRefused = [&]()
    {
        return refuses([&]
        {
            std::istringstream input(fixture.source);
            (void)sed::assets::loadFromJKL(input,
                {.projectType = sed::ProjectType::JKDF2, .requireCompleteCollections = true});
        });
    };
    const auto opensWithoutRows = [&]()
    {
        try { return load().jklSoundPreloads.rows.empty(); }
        catch ( const std::exception& ) { return false; }
    };
    const std::string prefix = "SECTION: HEADER\nVERSION 1\nEND\n";
    for (const std::string_view invalid : {
        "SECTION: SOUNDS\nWORLD SOUNDS 1\nBell.wav marker\\\n"
        "SECTION: THINGS\nWORLD THINGS 1\n0: base one 0 0 0 0 0 0 0\nEND\n",
        "SECTION: SOUNDS\n",
        "SECTION: SOUNDS\nWORLD SOUNDS 1\nBell\\\n",
        "SECTION: SOUNDS\nWORLD SOUNDS 1\n\\\n",
        "SECTION: SOUNDS\nWORLD SOUNDS 0\nEND\nSECTION: SOUNDS\nWORLD SOUNDS 0\nEND\n",
        "SECTION: SOUNDS\nWORLD SOUNDS 1\nnone\nSECTION: THINGS\nWorld Things 0\nEND\n",
        "SECTION: SOU\\\nNDS\nWORLD SOUNDS 1\nAbsent.wav\nEND\n",
        "SEC\\\nTION: SOUNDS\nWORLD SOUNDS 1\nAbsent.wav\nEND\n"})
    {
        fixture.source = prefix + std::string(invalid);
        request.inputBytes = fixture.source;
        const bool importRefused = strictImportRefused();
        const bool opened = opensWithoutRows();
        const bool dependenciesRefused = refuses([&] { (void)fixture.inspect(); });
        const bool packageRefused = refuses([&] { (void)planLevelPackage(request); });
        if (!importRefused || !opened || !dependenciesRefused || !packageRefused)
        {
            std::cerr << "Sound input [" << invalid << "] strict import/open/dependencies/package: "
                      << importRefused << '/' << opened << '/' << dependenciesRefused << '/' << packageRefused << '\n';
        }
        check(importRefused && opened && dependenciesRefused && packageRefused,
            "strict reads refuse inconclusive SOUNDS framing; an ordinary open keeps no authored rows");
    }

    fixture.source = prefix + "SECTION:SOUNDS # native compact header\r\nWORLD SOUNDS 2\r\n"
        "Bell\\ # joined\r\n; skip inside continuation\r\nTone.wav,0\r\n"
        "bellTone.wav marker\\# literal backslash\r\nEND\r\n";
    const auto level = load();
    const auto graph = fixture.inspect();
    const std::vector<std::pair<std::string, std::string>> files{{"sound/BellTone.wav", "B"}};
    request.inputBytes = fixture.source;
    request.archives = {{"sounds", false, files}};
    const auto plan = planLevelPackage(request);
    check(level.jklSoundPreloads.rows == std::vector<std::string>{"BellTone.wav,0", "bellTone.wav marker\\"}
        && graph.scanComplete && graph.edges.size() == 2 && graph.edges[0].authoredName == "BellTone.wav"
        && graph.edges[1].authoredName == "bellTone.wav" && plan.canBuild,
        "comment truncation before continuation preserves native actions across all three routes");
    fixture.source = prefix + "SECTION: SOUNDS\nWORLD SOUNDS 0\ndormant.wav\n"
        "SECTION: MODELS\nWORLD MODELS 1\ncrate.3do\nEND\n";
    const auto dormant = load();
    const auto following = fixture.inspect();
    check(dormant.jklSoundPreloads.rows == std::vector<std::string>{"dormant.wav"}
        && following.edges.size() == 1 && following.edges[0].authoredName == "crate.3do",
        "a dormant table returns the next section to its ordinary reader");
    fixture.source = prefix + "SECTION: SOUNDS\nWORLD SOUNDS 1\nBellTone.wav";
    request.inputBytes = fixture.source;
    check(load().jklSoundPreloads.rows == std::vector<std::string>{"BellTone.wav"}
        && fixture.inspect().edges.size() == 1 && planLevelPackage(request).canBuild,
        "a completed final preload action at EOF reaches every reader");

    // The native penultimate-byte continuation check sees a newline only
    // when the physical source contains one. Keep all other bytes identical.
    fixture.archives = {archive({{"sound/Bell.wav", "B"}})};
    request.archives = {{"sounds", false, {{"sound/Bell.wav", "B"}}}};
    for ( const bool newline : {false, true} )
    {
        for ( const bool trailingX : {false, true} )
        {
            const std::string row = std::string("Bell.wav marker\\") + (trailingX ? "X" : "");
            fixture.source = prefix + "SECTION: SOUNDS\nWORLD SOUNDS 1\n" + row + (newline ? "\n" : "");
            request.inputBytes = fixture.source;
            const bool continues = newline != trailingX;
            if ( continues )
            {
                const bool importRefused = strictImportRefused();
                const bool dependencyRefused = refuses([&] { (void)fixture.inspect(); });
                const bool packageRefused = refuses([&] { (void)planLevelPackage(request); });
                check(importRefused && opensWithoutRows() && dependencyRefused && packageRefused,
                    "a penultimate backslash starts an unfinished physical EOF continuation on every strict route");
            }
            else
            {
                const auto inspected = fixture.inspect();
                bool matchingRow = false;
                try
                {
                    const auto final = load();
                    matchingRow = final.jklSoundPreloads.rows == std::vector<std::string>{row}
                        && final.retainedJklSourceText == fixture.source + (newline ? "" : "\n")
                        && final.retainedJklSourceHash == hash(final.retainedJklSourceText);
                }
                catch ( const std::exception& ) {}
                check(matchingRow && inspected.scanComplete && inspected.allReferencesResolved == true
                    && inspected.edges.size() == 1 && inspected.edges[0].authoredName == "Bell.wav"
                    && planLevelPackage(request).canBuild,
                    "a completed physical EOF action preserves its marker, binding and legacy retained-source hash");
            }
        }
    }

    const std::string sector = "WORLD SECTORS 1\nSECTOR 0\nSOUND vent.wav 1\nSURFACES 0 0\nEND\n";
    const std::string thing = "WORLD THINGS 1\n0: base one 0 0 0 0 0 0 0\nEND\n";
    fixture.source = prefix + "SECTION:SECTORS\n" + sector + "SECTION:THINGS\n" + thing;
    const auto compact = load();
    fixture.source = prefix + "SECTION:tag\vSECTORS # second physical word\n" + sector
        + "SECTION:tag\fTHINGS # second physical word\n" + thing;
    const auto tagged = load();
    check(compact.sectors.empty() && compact.things.empty() && tagged.sectors.size() == 1
        && tagged.sectors[0].sound == "vent.wav" && tagged.things.size() == 1,
        "compact native SOUNDS preserves established sector and placed Thing section grammar");
}

void legacyDialectSoundDependencies()
{
    const std::string sections =
        "SECTION:SOUNDS SECTORS\nWORLD SECTORS 1\nSECTOR 0\nSOUND vent.wav 1\nSURFACES 0 0\nEND\n"
        "SECTION: SOUNDS\nWORLD SOUNDS 0\nlegacy.wav\nEND\n"
        "SECTION: THINGS\nWORLD THINGS 1\n0: base one 0 0 0 0 0 0 0 sound=placed.wav\nEND\n";
    const std::vector<std::pair<std::string, std::string>> files{
        {"sound/vent.wav", "V"}, {"sound/legacy.wav", "L"}, {"sound/placed.wav", "P"}};
    Fixture fixture;
    fixture.archives = {archive(files)};
    PackageRequest request;
    request.levelFileName = "mission.jkl";
    request.episodeTitle = "Legacy resources";
    request.archives = {{"assets", false, files}};
    for ( const auto kind : {sed::ProjectType::DroidWorks, sed::ProjectType::IJIM} )
    {
        // The identity can appear after the first ambiguous header.
        const std::string identity = kind == sed::ProjectType::IJIM
            ? "SECTION: HEADER\nVERSION 2\nEND\n"
            : "SECTION: HEADER\nVERSION 1\nEND\nSECTION: COPYRIGHT\n@@@..Lucas Learning Limited..@@@\nEND\n";
        fixture.source = sections + identity;
        request.inputBytes = fixture.source;
        try
        {
            std::istringstream input(fixture.source);
            const auto level = sed::assets::loadFromJKL(input);
            const auto graph = fixture.inspect();
            const auto plan = planLevelPackage(request);
            check(level.kind == kind && level.sectors.size() == 1 && level.sectors[0].sound == "vent.wav"
                && level.things.size() == 1 && level.jklSoundPreloads.rows.empty(),
                "the detected legacy dialect keeps sector and placed Thing collections under its own headers");
            check(graph.scanComplete && graph.allReferencesResolved == true && graph.edges.size() == 3
                && graph.edges[0].relation == "sectorAmbientSound" && graph.edges[0].authoredName == "vent.wav"
                && graph.edges[1].relation == "jklSound" && graph.edges[1].authoredName == "legacy.wav"
                && graph.edges[2].relation == "thingSound" && graph.edges[2].authoredName == "placed.wav",
                "legacy dependency inspection preserves ambient, historical sound-table and placed-Thing edges");
            check(plan.canBuild && std::all_of(files.begin(), files.end(), [&](const auto& file)
                { return std::any_of(plan.entries.begin(), plan.entries.end(), [&](const auto& entry)
                    { return entry.path == file.first; }); }),
                "legacy packaging admits and includes the same three sound resources");
        }
        catch ( const std::exception& error )
        {
            std::cerr << "Legacy sound dependency case: " << error.what() << '\n';
            check(false, "late legacy dialect identity reaches import, dependency inspection and packaging");
        }

        fixture.source = "SECTION: SOU\\\nNDS\nWORLD SOUNDS 32\nignored.wav\nEND\n" + identity;
        try
        {
            const auto graph = fixture.inspect();
            check(graph.scanComplete && graph.edges.empty(),
                "a continued native-only section header retains the historical legacy lexical interpretation");
        }
        catch ( const std::exception& )
        {
            check(false, "continued native-only SOUNDS syntax is not a malformed legacy source");
        }
        Fixture bounded;
        bounded.archives.clear();
        bounded.source = sections + std::string(2000, '\n') + identity;
        bounded.limits.maxWorkUnits = 4;
        const auto stopped = bounded.inspect();
        check(!stopped.scanComplete && !stopped.allReferencesResolved.has_value() && stopped.edges.empty()
            && stopped.usage.workUnits == 4 && reason(stopped.scanReasons, "maxWorkUnits"),
            "bounded dialect lookup stops before admitting an unclassified resource");
    }
}

void repeatedAndCycles()
{
    Fixture f;
    f.source = "SECTION: MODELS\nWorld Models 2\ncrate.3do\ncrate.3do\nEND\n";
    f.extra = {{"crate.3do", "3do"}};
    auto g = f.inspect();
    check(g.scanComplete && g.counts.nodes == 3 && g.counts.edges == 4,
        "repeated roots retain incoming edges without repeated traversal");
    if ( g.edges.size() == 4 )
    {
        check(g.edges[1].reused && g.edges[2].reused && g.edges[3].parent == 1,
            "repeated edges disclose destination reuse");
    }
    f.source = "SECTION: COGS\nWorld Cogs 1\n0: a.cog\nEND\n";
    f.extra.clear();
    f.archives = {archive({{"cog/a.cog", "symbols\ncog next=b.cog\nend\n"},
        {"cog/b.cog", "symbols\ncog next=a.cog\nend\n"}})};
    f.limits.maxDepth = 2;
    g = f.inspect();
    check(g.scanComplete && g.counts.nodes == 3 && g.counts.edges == 3
        && reason(g.unverifiedScopes, "cog-literal-references"),
        "COG cycle closes without repeating traversal or claiming code closure");
    if ( g.edges.size() == 3 )
    {
        check(g.edges[2].parent == 2 && g.edges[2].target == 1 && g.edges[2].reused,
            "cycle-closing edge survives at maximum new-target depth");
    }
    f.source.clear();
    f.archives.clear();
    f.extra = {{"absent.mat", "mat"}, {"ABSENT.MAT", "mat"}};
    g = f.inspect();
    check(g.scanComplete && g.allReferencesResolved == false && g.counts.nodes == 2
        && g.counts.edges == 2 && g.counts.missing == 1 && g.counts.issues == 1,
        "repeated immutable misses share a target and bounded diagnostic");
    f.archives = {archive({{"a/stone.mat", "A"}, {"b/stone.mat", "B"}})};
    f.extra = {{"stone.mat", "mat"}, {"a/stone.mat", "mat"}};
    g = f.inspect();
    check(g.scanComplete && g.counts.nodes == 2 && g.counts.edges == 2
        && g.counts.ambiguous == 1 && g.allReferencesResolved == false,
        "ambiguous destination remains authoritative before an exact alias");
}

void provenance()
{
    Fixture f;
    f.source.clear();
    f.extra = {{"wall.mat", "mat"}};
    f.archives = {archive({{"other/wall.mat", "WRONG"}}, false, "same.gob"),
        archive({{"mat/wall.mat", "abc"}}, true, "same.gob")};
    auto g = f.inspect();
    check(g.nodes.size() == 2 && g.sourceArchives.size() == 2, "ordered archives yield one winner");
    if ( g.nodes.size() == 2 && g.nodes[1].provenance && g.sourceArchives.size() == 2 )
    {
        check(g.nodes[1].provenance->archiveOrdinal == 1 && g.nodes[1].stock
            && g.nodes[1].sha256 == hash("abc")
            && g.sourceArchives[0].label == g.sourceArchives[1].label
            && g.sourceArchives[0].sha256 != g.sourceArchives[1].sha256,
            "later exact beats early basename and same labels retain distinct identity");
    }
    f.archives = {archive({{"a/wall.mat", "A"}, {"b/wall.mat", "B"}}),
        archive({{"other/wall.mat", "C"}})};
    g = f.inspect();
    check(g.nodes.size() == 2 && g.nodes[1].provenance
        && g.nodes[1].provenance->archiveOrdinal == 1
        && g.nodes[1].provenance->lookupTier == "basename"
        && g.allReferencesResolved == true,
        "ambiguous early basename does not hide a unique later winner");
    f.archives = {archive({{"MAT/WALL.MAT", "FIRST"}, {"mat/wall.mat", "SECOND"}})};
    g = f.inspect();
    check(g.nodes.size() == 2 && g.nodes[1].provenance
        && g.nodes[1].provenance->entryOrdinal == 0 && g.nodes[1].sha256 == hash("FIRST"),
        "case and slash aliases retain first physical row and its payload");
    f.archives = {archive({{"/mat/wall.mat", "FIRST"}, {"mat/wall.mat", "SECOND"}})};
    g = f.inspect();
    check(g.nodes.size() == 2 && g.nodes[1].provenance
        && g.nodes[1].provenance->entryOrdinal == 1 && g.nodes[1].provenance->offset == 293
        && g.nodes[1].provenance->bytes == 6 && g.nodes[1].sha256 == hash("SECOND"),
        "leading-slash aliases use selected physical bytes rather than name-based read");
    f.extra = {{"/mat/wall.mat", "mat"}};
    g = f.inspect();
    check(g.nodes.size() == 2 && g.nodes[1].provenance
        && g.nodes[1].provenance->entryOrdinal == 0
        && g.nodes[1].provenance->lookupTier == "authoredExact",
        "authored exact tier retains a literal leading slash");
}

void thingOverrideEdges()
{
    Fixture f;
    f.archives.clear();
    f.source =
        "SECTION: THINGS # placed objects\n# skip before header\nWORLD\tTHINGS 32 # spare capacity\n"
        "7: base one 1.25 -2 +3 .5 0 4e-2 0 MoDeL3D=Ship.3do keyframe=k.key puppet=p.pup "
        "soundclass=s.snd sound=a.wav snd=b.wav material=m.mat sprite=s.spr aiclass=a.ai cog=c.cog\n"
        "8: base two 0 0 0 0 0 0 0 -1 soundclass=s.snd # sound=comment.wav\n"
        "12: base three 0 0 0 0 0 0 0 4: soundclass=s.snd\n"
        "13: base four 0 0 0 0 0 0 0 unknown=phantom.mat model3d=NoNe sound=\n"
        "x: base bad 0 0 0 0 0 0 0 sound=bad-index.wav\n"
        "9: base short 0 0 0 0 sound=short.wav\n"
        "10: base bad 0 0 0 oops 0 0 0 sound=bad-pose.wav\n"
        "11: base bad 0 0 0 0 0 0 sector sound=bad-sector.wav\n"
        "15: base bad 0 0 0 0 0 0 0 bogus sound=bad-arch.wav\n"
        "END # end of table\n18: base outside 0 0 0 0 0 0 0 sound=outside.wav\n"
        "Section: Things\nWorld things 0\n17: base exceeds 0 0 0 0 0 0 0 sound=over.wav\nEND\n"
        "Section: Things\nmalformed header\n19: base bad 0 0 0 0 0 0 0 sound=bad-header.wav\nEND\n";
    const auto g = f.inspect();
    struct Expected
    {
        std::string_view relation;
        std::string_view folder;
        std::string_view name;
    };
    const Expected expected[]{
        {"thingModel", "3do", "Ship.3do"}, {"thingKeyframe", "3do/key", "k.key"},
        {"thingPuppet", "misc/pup", "p.pup"}, {"thingSoundClass", "misc/snd", "s.snd"},
        {"thingSound", "sound", "a.wav"}, {"thingSound", "sound", "b.wav"},
        {"thingMaterial", "mat", "m.mat"}, {"thingSprite", "misc/spr", "s.spr"},
        {"thingAiClass", "misc/ai", "a.ai"}, {"thingCog", "cog", "c.cog"},
        {"thingSoundClass", "misc/snd", "s.snd"}, {"thingSoundClass", "misc/snd", "s.snd"}};
    check(g.scanComplete && g.edges.size() == std::size(expected)
        && std::equal(g.edges.begin(), g.edges.end(), std::begin(expected), std::end(expected),
            [](const auto& edge, const Expected& want)
            {
                return edge.parent == 0 && edge.relation == want.relation
                    && edge.folder == want.folder && edge.authoredName == want.name;
            }), "Thing override edges retain only framed JK and MotS references in source order");
    check(g.edges.size() == 12 && !g.edges[3].reused && g.edges[10].reused && g.edges[11].reused
        && g.edges[10].target == g.edges[3].target && g.edges[11].target == g.edges[3].target,
        "repeated Thing overrides retain each incoming edge and reuse one target");
}

void thingOverrideBudgets()
{
    Fixture f;
    f.archives.clear();
    f.source = "Section: Things\nWorld things 1\n0: base one 0 0 0 0 0 0 0 sound=a.wav sound=b.wav\nEND\n";
    f.extra = {{"extra.wav", "sound"}};
    f.limits.maxWorkUnits = 13; // Three lines and the ten prefix tokens, before either override.
    auto g = f.inspect();
    check(!g.scanComplete && reason(g.scanReasons, "maxWorkUnits") && g.counts.edges == 0,
        "a token-work stop in a Thing row skips its overrides and later extra roots");
    f.limits = {};
    f.limits.maxEdges = 1;
    g = f.inspect();
    check(!g.scanComplete && reason(g.scanReasons, "maxEdges") && g.edges.size() == 1
        && g.edges[0].authoredName == "a.wav", "edge limit stops during Thing overrides before extra roots");
}

void thingOverridePackage()
{
    PackageRequest request;
    request.inputBytes = "Section: Things\nWorld things 12\n"
        "0: bell first 0 0 0 0 0 0 0 soundclass=bell.snd\n"
        "1: bell second 0 0 0 0 0 0 0 soundclass=bell.snd\nEND\n";
    request.levelFileName = "mission.jkl";
    request.episodeTitle = "Bells";
    request.archives.push_back({"custom", false, {{"misc/snd/bell.snd", "alert ring.wav\n"},
        {"sound/ring.wav", "alarm"}}});
    const auto plan = planLevelPackage(request);
    check(plan.canBuild && !plan.runtimeClosureVerified && plan.edges == 3 && plan.nodes == 4,
        "package follows a Thing-only SND and its child WAV, sharing repeated destinations");
    if ( plan.canBuild )
    {
        const auto raw = std::as_bytes(std::span(plan.outputData));
        const auto gob = sed::assets::GobArchive::fromBytes({raw.begin(), raw.end()});
        check(gob.entries().size() == 4 && gob.contains("misc/snd/bell.snd")
            && gob.contains("sound/ring.wav"), "physical package includes one sound class and one WAV");
    }
    Fixture f;
    f.source = "Section: Soundclass\nWorld soundclass 1\nbell.snd\nEND\n" + request.inputBytes;
    f.archives = {archive(request.archives[0].entries)};
    const auto graph = f.inspect();
    check(graph.allReferencesResolved == true && graph.edges.size() == 4 && graph.nodes.size() == 3
        && graph.edges[0].relation == "jklSoundclass" && graph.edges[1].relation == "thingSoundClass"
        && graph.edges[2].relation == "thingSoundClass" && graph.edges[1].reused && graph.edges[2].reused
        && graph.edges[0].target == graph.edges[1].target && graph.edges[0].target == graph.edges[2].target
        && graph.edges[3].relation == "soundClassWav" && graph.edges[3].parent == graph.edges[0].target,
        "table-listed sound class retains both Thing uses and one child traversal");

    request.archives[0].entries.pop_back();
    const auto blocked = planLevelPackage(request);
    check(!blocked.canBuild && blocked.outputData.empty()
        && std::any_of(blocked.issues.begin(), blocked.issues.end(), [](const auto& issue)
            { return issue.code == "MissingResource" && issue.reference == "ring.wav"; }),
        "missing audio behind a Thing override blocks package creation and names the WAV");
}

void coverage()
{
    Fixture f;
    f.source =
        "SECTION: MATERIALS\nWorld Materials 1\nm.mat\nEND\n"
        "SECTION: SOUNDS\nWorld Sounds 1\ns.wav\nEND\n"
        "SECTION: MODELS\nWorld Models 1\nm.3do\nEND\n"
        "SECTION: SPRITES\nWorld Sprites 1\ns.spr\nEND\n"
        "SECTION: KEYFRAMES\nWorld Keyframes 1\nk.key\nEND\n"
        "SECTION: ANIMCLASS\nWorld Animclass 1\np.pup\nEND\n"
        "SECTION: SOUNDCLASS\nWorld Soundclass 1\ns.snd\nEND\n"
        "SECTION: AICLASS\nWorld Aiclass 1\na.ai\nEND\n"
        "SECTION: COGSCRIPTS\nWorld Cogscripts 1\ns.cog\nEND\n"
        "SECTION: TEMPLATES\nWorld Templates 1\nbox none model3d=m.3do material=m.mat cog=s.cog\nEND\n"
        "SECTION: COGS\nWorld Cogs 1\n0: s.cog\nEND\n"
        "SECTION: GEORESOURCE\nWorld Colormaps 1\nc.cmp\nWorld Vertices 0\nEND\n";
    f.extra = {{"unknown.bin", ""}};
    f.archives = {archive({{"mat/m.mat", "M"}, {"sound/s.wav", "W"}, {"3do/m.3do", "m.mat\n"},
        {"misc/spr/s.spr", "# comment\nm.mat rest\n"}, {"3do/key/k.key", "K"},
        {"misc/pup/p.pup", "0 a.key # discarded.key\n1 b.KEY; discarded\n"},
        {"misc/snd/s.snd", "create a.wav // discarded\nnoise b.wav\n"},
        {"misc/ai/a.ai", "unknown"}, {"cog/s.cog", "symbols\nmaterial localmat=\"m.mat\" local\nend\n"},
        {"misc/cmp/c.cmp", "C"}, {"unknown.bin", "unknown"}, {"3do/key/a.key", "A"},
        {"3do/key/b.KEY", "B"}, {"sound/a.wav", "A"}, {"sound/b.wav", "B"}}, true)};
    auto g = f.inspect();
    check(g.scanComplete && g.allReferencesResolved == true && g.counts.nodes == 16
        && g.counts.edges == 22 && g.counts.unsupported == 2,
        "all supported roots and lexical child families traverse stock resources");
    for ( const auto relation : {"jklMaterial", "jklSound", "jklModel", "jklSprite", "jklKeyframe",
        "jklPuppet", "jklSoundclass", "jklAiClass", "jklCogScript", "templateModel", "templateMaterial",
        "templateCog", "placedCogScript", "levelColormap", "extraResource", "modelMaterial",
        "spriteMaterial", "puppetKeyframe", "soundClassWav", "cogDefaultResource"} )
    {
        check(std::any_of(g.edges.begin(), g.edges.end(), [relation](const auto& e)
            { return e.relation == relation; }), relation);
    }
    f.source.clear();
    f.extra = {{"empty.spr", "misc/spr"}, {"broken.cog", "cog"}};
    f.archives = {archive({{"misc/spr/empty.spr", "# no token\n"}, {"cog/broken.cog", "symbols\nnonsense\n"}})};
    g = f.inspect();
    check(g.scanComplete && g.allReferencesResolved == true && g.counts.issues == 2
        && reason(g.unverifiedScopes, "cog-resource-defaults")
        && reason(g.unverifiedScopes, "subresource-dependencies"),
        "incomplete readers are coverage issues separate from missing and budget stops");
    f.extra = {{"s.cog", "cog"}};
    std::string cog = "symbols\nend\n" + std::string(1022, '\n');
    f.archives = {archive({{"cog/s.cog", cog}})};
    g = f.inspect();
    check(g.scanComplete, "1024 COG physical lines fit conservative parser admission");
    f.archives = {archive({{"cog/s.cog", cog + "\n"}})};
    g = f.inspect();
    check(!g.scanComplete && reason(g.scanReasons, "cogParserLimit"),
        "one extra COG line stops before invoking the schema parser");

    // The schema parser retains a raw declaration in every modifier diagnostic.
    // Commas split modifiers even when the reader sees one whitespace token.
    const auto modifiers = [](std::size_t count, char separator = ',')
    {
        std::string text = "symbols\nint x ";
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( i )
            {
                text += separator;
            }
            text += 'x';
        }
        return text + "\nend\n";
    };
    // Independently derived evidence estimates: 16,766,526 and 16,782,908 bytes.
    // The fixed preflight allowance is 16,777,216 bytes (16 MiB).
    f.archives = {archive({{"cog/s.cog", modifiers(2009)}})};
    g = f.inspect();
    check(g.scanComplete && reason(g.unverifiedScopes, "cog-resource-defaults"),
        "largest synthetic comma declaration within parser evidence allowance is inspected");
    f.archives = {archive({{"cog/s.cog", modifiers(2010)}})};
    g = f.inspect();
    check(!g.scanComplete && reason(g.scanReasons, "cogParserLimit")
        && !g.allReferencesResolved.has_value(),
        "one more comma modifier refuses before diagnostic amplification");
    f.archives = {archive({{"cog/s.cog", modifiers(2010, ' ')}})};
    g = f.inspect();
    check(!g.scanComplete && reason(g.scanReasons, "cogParserLimit"),
        "whitespace modifier expansion is refused by the same parser evidence cap");

    std::string boundedCog = "symbols\n#" + std::string(256u * 1024u - 14u, 'a') + "\nend\n";
    check(boundedCog.size() == 256u * 1024u, "COG byte-cap fixture has exact size");
    f.archives = {archive({{"cog/s.cog", boundedCog}})};
    check(f.inspect().scanComplete, "exact COG byte cap with bounded parser evidence fits");
    boundedCog += '\n';
    f.archives = {archive({{"cog/s.cog", boundedCog}})};
    g = f.inspect();
    check(!g.scanComplete && reason(g.scanReasons, "cogParserLimit"),
        "one byte beyond COG cap refuses before schema parsing");
}

void budgets()
{
    const auto boundary = [](std::size_t DependencyLimits::*member, std::size_t exact, const char* name)
    {
        Fixture f;
        f.limits.*member = exact;
        check(f.inspect().scanComplete, name);
        f.limits.*member = exact - 1;
        const auto stopped = f.inspect();
        check(!stopped.scanComplete && reason(stopped.scanReasons, name)
            && !stopped.allReferencesResolved.has_value(), name);
    };
    boundary(&DependencyLimits::maxNodes, 3, "maxNodes");
    boundary(&DependencyLimits::maxEdges, 2, "maxEdges");
    boundary(&DependencyLimits::maxDepth, 2, "maxDepth");
    boundary(&DependencyLimits::maxIndexEntries, 2, "maxIndexEntries");
    boundary(&DependencyLimits::maxPending, 1, "maxPending");
    boundary(&DependencyLimits::maxResourceBytes, 12, "maxResourceBytes");
    boundary(&DependencyLimits::maxWorkUnits, 13, "maxWorkUnits");
    Fixture f;
    boundary(&DependencyLimits::maxScanBytes, f.source.size() + 9, "maxScanBytes");
    const auto full = f.inspect();
    if ( full.usage.stringBytes > 0 )
    {
        boundary(&DependencyLimits::maxStringBytes, full.usage.stringBytes, "maxStringBytes");
    }
    else
    {
        check(false, "string accounting is nonzero for a retained graph");
    }
    f.limits.maxStringBytes = 0;
    auto g = f.inspect();
    check(!g.scanComplete && g.counts.nodes == 0 && g.usage.stringBytes == 0,
        "zero string allowance refuses before root storage");
    f = {};
    f.limits.maxDetailRecords = 5;
    check(f.inspect().detailsComplete, "exact combined node/edge detail record allowance");
    f.limits.maxDetailRecords = 4;
    g = f.inspect();
    check(g.scanComplete && !g.detailsComplete && g.counts.nodes == 3 && g.counts.edges == 2
        && g.nodes.size() == 3 && g.edges.size() == 1 && reason(g.detailReasons, "maxDetailRecords"),
        "detail truncation preserves full scan totals and a deterministic prefix");
    f.limits.maxDetailRecords = 0;
    g = f.inspect();
    check(g.scanComplete && !g.detailsComplete && g.nodes.empty() && g.edges.empty()
        && g.counts.nodes == 3 && g.allReferencesResolved == true,
        "zero details does not imply an empty or unresolved graph");
    f = {};
    if ( full.usage.detailBytes > 0 )
    {
        f.limits.maxDetailBytes = full.usage.detailBytes;
        check(f.inspect().detailsComplete, "exact detail byte allowance");
        --f.limits.maxDetailBytes;
        g = f.inspect();
        check(g.scanComplete && !g.detailsComplete && reason(g.detailReasons, "maxDetailBytes"),
            "one-over detail bytes stops output only");
    }
    else
    {
        check(false, "detail byte accounting is nonzero for emitted rows");
    }
    f = {};
    f.source.clear();
    f.archives.clear();
    f.extra = {{"one.mat", "mat"}, {"two.mat", "mat"}};
    f.limits.maxIssues = 1;
    g = f.inspect();
    check(g.scanComplete && !g.detailsComplete && g.counts.issues == 2 && g.issues.size() == 1
        && g.allReferencesResolved == false && reason(g.detailReasons, "maxIssues"),
        "issue storage cap preserves observed issues and resolution failures");
    f.limits.maxIssues = 0;
    g = f.inspect();
    check(g.scanComplete && g.issues.empty() && g.counts.issues == 2 && !g.detailsComplete,
        "zero issue allowance retains summary evidence");
    f = {};
    f.archives.clear();
    f.source = "SECTION: MATERIALS\nWorld Materials 1\n" + std::string(8192, 'x') + ".mat\nEND\n";
    f.limits.maxStringBytes = 512;
    g = f.inspect();
    check(!g.scanComplete && g.usage.stringBytes <= 512 && g.counts.nodes == 1,
        "oversized authored token cannot allocate graph strings beyond admission");
    f = {};
    f.archives = {DependencyArchiveInput{}};
    bool refused = false;
    try { (void)f.inspect(); }
    catch ( const std::invalid_argument& ) { refused = true; }
    check(refused, "uncaptured archives are refused without reopening paths");
    f = {};
    f.limits.maxNodes = 16385;
    refused = false;
    try { (void)f.inspect(); }
    catch ( const std::invalid_argument& ) { refused = true; }
    check(refused, "public core rejects limits above hard maxima");
}
} // namespace

int main()
{
    chain();
    sectorAmbientSounds();
    sectorSoundGrammar();
    compactModelMaterialRows();
    authoredSoundResources();
    soundTableAdmission();
    legacyDialectSoundDependencies();
    repeatedAndCycles();
    provenance();
    thingOverrideEdges();
    thingOverrideBudgets();
    thingOverridePackage();
    coverage();
    budgets();
    std::cout << failures << " dependency failure(s)\n";
    return failures == 0 ? 0 : 1;
}
