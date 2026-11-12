#include "sed/level/dependencies.hpp"
#include "sed/assets/jkl.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
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
    repeatedAndCycles();
    provenance();
    coverage();
    budgets();
    std::cout << failures << " dependency failure(s)\n";
    return failures == 0 ? 0 : 1;
}
