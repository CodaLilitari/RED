#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level.hpp"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace sed::tests
{
namespace
{

Level loadSoundLevel(const ProjectType kind, const std::string_view sounds)
{
    std::istringstream input("SECTION: HEADER\nVERSION 1\nEND\n"
        "SECTION: SOUNDS\n" + std::string(sounds));
    return assets::loadFromJKL(input, {.projectType = kind});
}

std::string soundSection(const std::string& text, const bool compact = false)
{
    const std::string marker = compact ? "SECTION:\tSOUNDS\n" : "SECTION: SOUNDS\n";
    const std::size_t begin = text.find(marker);
    if ( begin == std::string::npos )
    {
        return {};
    }
    const std::size_t end = text.find("\nEND\n", begin);
    return end == std::string::npos ? std::string{} : text.substr(begin, end + 5u - begin);
}

void checkRefusesSoundExport(const Level& level, const char* description)
{
    bool refused = false;
    try
    {
        (void)assets::buildJklExport(level);
    }
    catch ( const std::exception& )
    {
        refused = true;
    }
    check(refused, description);
}

void checkSoundHeaderDialects()
{
    const auto read = [](std::string text, const bool strict)
    {
        std::istringstream stream(std::move(text));
        return assets::loadFromJKL(stream, {.requireCompleteCollections = strict});
    };
    const auto refuses = [&](const std::string& text)
    {
        try { (void)read(text, true); }
        catch (const std::runtime_error&) { return true; }
        catch (const assets::JklSoundTableError&) { return true; }
        return false;
    };
    const std::string classic = "SECTION: HEADER\nVERSION 1\nEND\n";
    const std::string ijim = "SECTION: HEADER\nVERSION 3\nEND\n";
    const std::string droid = classic + "SECTION: COPYRIGHT\n@@@..Lucas Learning Limited..@@@\nEND\n";
    const std::string sectorBody = "WORLD SECTORS 1\nSECTOR 0\nSURFACES 0 0\nEND\n";
    const std::string emptySectors = "SECTION: SECTORS\nWORLD SECTORS 0\nEND\n";
    const std::string compactSounds = "SECTION:SOUNDS\nWORLD SOUNDS 1\nbell.wav,0\nEND\n";
    const auto hasIdentity = [](std::string text)
    {
        std::istringstream stream(std::move(text));
        try
        {
            (void)assets::loadFromJKL(stream, {.requireLevelIdentity = true});
            return true;
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    };
    const std::string versionAction = "SECTION:SOUNDS HEADER\nWORLD SOUNDS 1\nVERSION 1\nEND\n";
    check(!hasIdentity(versionAction), "a JK sound action named VERSION cannot establish level identity");
    check(hasIdentity(versionAction + classic), "a genuine HEADER after the JK sound action establishes identity");
    check(hasIdentity("SECTION:SOUNDS HEADER\nWORLD SOUNDS 1\nVERSION invalid\nEND\n" + classic),
        "a JK sound action cannot invalidate the later genuine HEADER version");
    for (const auto& signature : {ijim, droid})
    {
        const auto kind = signature == ijim ? ProjectType::IJIM : ProjectType::DroidWorks;
        const auto mixed = "SECTION:SOUNDS SECTORS\n" + sectorBody + signature;
        for (const bool strict : {false, true})
        {
            try
            {
                const auto level = read(mixed, strict);
                check(level.kind == kind && level.sectors.size() == 1 && level.jklSoundPreloads.rows.empty(),
                    "IJIM and DroidWorks keep the legacy second-field sector even when dialect markers follow");
            }
            catch (const std::exception&)
            {
                check(false, "a complete non-JK sector using the legacy second field loads in strict mode");
            }
        }
        const auto opaque = read(compactSounds + signature, false);
        check(opaque.retainedJklUnknownSections == std::vector<std::string>{compactSounds},
            "non-JK compact sound blocks retain their legacy opaque bytes");
        check(refuses(signature + emptySectors + "SECTION: SOUNDS extra\nWORLD SOUNDS 0\nEND\n"),
            "strict IJIM and DroidWorks headers retain their two-field rule for SOUNDS");
    }
    bool legacyHeaderDetected = false;
    try
    {
        const auto disguised = read("SECTION:SOUNDS HEADER\nVERSION 3\nEND\nSECTION:SOUNDS SECTORS\n"
            + sectorBody, false);
        legacyHeaderDetected = disguised.kind == ProjectType::IJIM && disguised.sectors.size() == 1;
    }
    catch (const std::exception&) {}
    check(legacyHeaderDetected,
        "a legacy second-field HEADER establishes IJIM before applying JK sound grammar");
    for (const std::string_view first : {"invalid", "1"})
    {
        const auto text = "SECTION: HEADER\nVERSION " + std::string(first) + "\nVERSION 3\nEND\n"
            + "SECTION:SOUNDS SECTORS\n" + sectorBody;
        bool lateVersionPreserved = false;
        try
        {
            const auto level = read(text, false);
            lateVersionPreserved = level.kind == ProjectType::IJIM && level.sectors.size() == 1
                && level.uvCoordinateSpace == UvCoordinateSpace::TexturePixels;
        }
        catch (const std::exception&) {}
        check(lateVersionPreserved, "later IJIM VERSION retains final dialect and original initial UV defaults");
    }
    for (const auto& signature : {classic, classic + "SECTION: ARCHLIGHTING\nEND\n"})
    {
        const auto level = read(signature + emptySectors + compactSounds, true);
        check(level.jklSoundPreloads.rows == std::vector<std::string>{"bell.wav,0"}
                && level.retainedJklUnknownSections.empty(),
            "strict JK and MotS accept compact SOUNDS under their native grammar");
        check(refuses(signature + emptySectors + "SECTION:foo SOUNDS metadata\nWORLD SOUNDS 0\nEND\n"),
            "strict collection admission checks ambiguous non-native sound aliases before dropping them");
        check(refuses(signature + emptySectors + "SECTION: THINGS\nWORLD THINGS 2\n" + compactSounds),
            "a compact JK SOUNDS header cannot terminate an incomplete THINGS collection");
    }
}
} // namespace

void runJklSoundPreloadTests()
{
    checkSoundHeaderDialects();
    // Original synthetic text only. The two duplicate zeta actions differ:
    // the native loader treats the presence of any second argument as a
    // preload request, including a literal zero.
    for ( const ProjectType kind : {ProjectType::JKDF2, ProjectType::MOTS} )
    {
        const std::string authored = "WORLD SOUNDS 10\n"
            "zeta.wav\nalpha.wav 0\nzeta.wav 1\nEND\n";
        const std::string expected = "SECTION: SOUNDS\n" + authored;
        Level level = loadSoundLevel(kind, authored);
        check(soundSection(assets::buildJklExport(level).text) == expected,
            "sound preloads: preserve order, duplicate actions, zero marker and capacity");
        const assets::JklExportBufferReport buffers = assets::inspectJklExportBuffers(level);
        check(buffers.sounds.collected == 2u && buffers.sounds.additional == 8u
                && buffers.sounds.total == 10u,
            "sound preloads: capacity counts distinct names instead of physical action rows");

        level.retainedJklSourceText.clear();
        level.retainedJklSourceHash.clear();
        check(soundSection(assets::buildJklExport(level).text) == expected,
            "sound preloads: preservation is independent of the transient source cache");

        Level reopened = assets::loadFromSedBytes(assets::saveToSedBytes(level));
        reopened.header.gravity = 3.25;
        check(soundSection(assets::buildJklExport(reopened).text) == expected,
            "sound preloads: native save/reopen and unrelated edit preserve authored actions");

        const std::string source = "WORLD SOUNDS 8\n"
            "; physical column-one comment\n"
            "name=Zeta.WAV,preload=0.0150\n"
            "Alpha.wav 001.5000 ignored=02 # removed comment\n"
            "Zeta.WAV\\\n"
            " 0\n"
            "END ignored-tail\n";
        Level nativeSyntax = loadSoundLevel(kind, source);
        nativeSyntax.newSector().sound = "zeta.wav";
        const std::string rows = "name=Zeta.WAV,preload=0.0150\n"
            "Alpha.wav 001.5000 ignored=02 \nZeta.WAV 0\nEND\n";
        check(soundSection(assets::buildJklExport(nativeSyntax).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 8\n" + rows,
            "sound preloads: native commas, equals, continuations and terminator values survive");
        check(soundSection(assets::buildJklExport(nativeSyntax, {.compact = true}).text, true)
                == "SECTION:\tSOUNDS\nWORLD\tSOUNDS\t8\n" + rows,
            "sound preloads: compact output keeps exact marker spelling and logical row text");

        Level append = loadSoundLevel(kind, "WORLD SOUNDS 5\nZeta.wav 0\nEND\n");
        append.newSector().sound = "alpha.wav";
        append.newSector().sound = "ZETA.WAV";
        check(soundSection(assets::buildJklExport(append).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 6\nZeta.wav 0\nalpha.wav\nEND\n",
            "sound preloads: append inferred resources after the authored prefix without case duplicates");

        const std::filesystem::path root = std::filesystem::temp_directory_path()
            / ("sed-sound-preloads-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        assets::commitJklExport(append, assets::buildJklExport(append), root / "sounds.jkl");
        append.sectors[0].sound.clear();
        check(soundSection(assets::buildJklExport(append).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 5\nZeta.wav 0\nEND\n",
            "sound preloads: committing an export does not promote inferred rows into authored state");
        std::filesystem::remove_all(root);

        Level duplicateCapacity = loadSoundLevel(kind,
            "WORLD SOUNDS 1\nA.wav\na.wav 0\nEND\n");
        check(soundSection(assets::buildJklExport(duplicateCapacity).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 1\nA.wav\na.wav 0\nEND\n",
            "sound preloads: duplicate actions may outnumber the allocated sound slots");

        Level underdeclared = loadSoundLevel(kind,
            "WORLD SOUNDS 1\nA.wav\nB.wav 0\nEND\n");
        check(soundSection(assets::buildJklExport(underdeclared).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 1\nA.wav\nB.wav 0\nEND\n",
            "sound preloads: an unchanged underdeclared source retains its capacity");
        underdeclared.newSector().sound = "c.wav";
        check(soundSection(assets::buildJklExport(underdeclared).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 3\nA.wav\nB.wav 0\nc.wav\nEND\n",
            "sound preloads: a new tail has room after the complete authored prefix");

        Level dormant = loadSoundLevel(kind,
            "WORLD SOUNDS 0\nDormant.wav 0\nEND\n");
        check(soundSection(assets::buildJklExport(dormant).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 0\nDormant.wav 0\nEND\n",
            "sound preloads: zero-capacity source rows remain dormant on unchanged export");
        dormant.newSector().sound = "new.wav";
        checkRefusesSoundExport(dormant,
            "sound preloads: refuse implicitly activating a dormant prefix for a new dependency");
        dormant.exportBufferExtras.sounds = 1u;
        check(soundSection(assets::buildJklExport(dormant).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 3\nDormant.wav 0\nnew.wav\nEND\n",
            "sound preloads: an explicit positive Extra activates the prefix with adequate capacity");

        const Level noRows = loadSoundLevel(kind, "WORLD SOUNDS 7\nEND\n");
        check(soundSection(assets::buildJklExport(noRows).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 7\nEND\n",
            "sound preloads: an empty table retains its editable spare capacity");
        const Level noOp = loadSoundLevel(kind, "WORLD SOUNDS 4\nnone 0\nEND\n");
        check(soundSection(assets::buildJklExport(noOp).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 4\nnone 0\nEND\n",
            "sound preloads: preserve authored none actions without consuming a local slot");

        std::istringstream compactHeader("SECTION: HEADER\nVERSION 1\nEND\n"
            "SECTION:SOUNDS\nWORLD SOUNDS 4\nOne.wav 0\nEND\n");
        const Level noHeaderGap = assets::loadFromJKL(compactHeader, {.projectType = kind});
        const std::string noHeaderGapText = assets::buildJklExport(noHeaderGap).text;
        check(soundSection(noHeaderGapText)
                == "SECTION: SOUNDS\nWORLD SOUNDS 4\nOne.wav 0\nEND\n"
                && noHeaderGapText.find("SECTION:SOUNDS") == std::string::npos,
            "sound preloads: a section name without a colon gap is modeled exactly once");

        Level dormantMatch = loadSoundLevel(kind,
            "WORLD SOUNDS 0\nsame.wav 0\nEND\n");
        dormantMatch.newSector().sound = "same.wav";
        checkRefusesSoundExport(dormantMatch,
            "sound preloads: a dormant authored row cannot satisfy a newly inferred matching sound");
        check(assets::inspectJklExportBuffers(dormantMatch).sounds.collected == 1u,
            "sound preloads: report-only capacity remains available to edit a dormant table");
        Level activateMatch = loadSoundLevel(kind,
            "WORLD SOUNDS 0\na.wav\nb.wav\nc.wav 0\nEND\n");
        activateMatch.newSector().sound = "c.wav";
        activateMatch.exportBufferExtras.sounds = 1u;
        check(soundSection(assets::buildJklExport(activateMatch).text)
                == "SECTION: SOUNDS\nWORLD SOUNDS 4\na.wav\nb.wav\nc.wav 0\nEND\n",
            "sound preloads: explicit activation makes room before a matching inferred sound");

        try
        {
            const Level noEnd = loadSoundLevel(kind,
                "WORLD SOUNDS 0\nDormant.wav 0\n"
                "SECTION: SECTORS\nWORLD SECTORS 0\nEND\n");
            check(soundSection(assets::buildJklExport(noEnd).text)
                    == "SECTION: SOUNDS\nWORLD SOUNDS 0\nDormant.wav 0\nEND\n",
                "sound preloads: zero capacity returns to the outer reader before the next section");
        }
        catch ( const std::exception& )
        {
            check(false, "sound preloads: valid dormant section without END must load");
        }

        try
        {
            const Level backslash = loadSoundLevel(kind,
                "WORLD SOUNDS 4\none.wav marker\\# trailing comment\nEND\n");
            const std::string exported = assets::buildJklExport(backslash).text;
            check(soundSection(exported)
                    == "SECTION: SOUNDS\nWORLD SOUNDS 4\none.wav marker\\#\nEND\n",
                "sound preloads: protect a literal terminal backslash with an empty comment");
            std::istringstream reimport(exported);
            check(soundSection(assets::buildJklExport(
                    assets::loadFromJKL(reimport, {.projectType = kind})).text) == soundSection(exported),
                "sound preloads: a literal terminal backslash remains stable on reimport");
        }
        catch ( const std::exception& )
        {
            check(false, "sound preloads: literal backslash marker must export");
        }
    }
}

} // namespace sed::tests
