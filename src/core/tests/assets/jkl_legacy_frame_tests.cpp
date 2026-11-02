#include "test_common.hpp"

#include "sed/assets/jed.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/level.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace sed::tests
{

namespace
{

struct LoadOutcome
{
    sed::Level level{};
    std::vector<std::string> warnings{};
    bool threw{ false };
    std::string error{};
};

LoadOutcome loadJkl(const std::string& source)
{
    LoadOutcome outcome{};
    std::istringstream input(source);
    sed::assets::JklLoadOptions options{};
    options.onDiagnostic = [&outcome](const sed::DiagnosticSeverity severity,
                                      const std::string_view message)
    {
        if (severity == sed::DiagnosticSeverity::warning)
        {
            outcome.warnings.emplace_back(message);
        }
    };
    try
    {
        outcome.level = sed::assets::loadFromJKL(input, options);
    }
    catch (const std::exception& error)
    {
        outcome.threw = true;
        outcome.error = error.what();
    }
    return outcome;
}

std::string thingSource(const std::string& params)
{
    return "SECTION: HEADER\n"
           "VERSION 1\n"
           "END\n"
           "SECTION: THINGS\n"
           "World things 1\n"
           "0: crate crate 1 2 3 0 0 0 -1 " + params + "\n"
           "END\n";
}

const sed::TplValue* findValue(
    const std::vector<sed::TplValue>& values, const std::string_view name)
{
    for (const sed::TplValue& value : values)
    {
        if (value.name == name)
        {
            return &value;
        }
    }
    return nullptr;
}

std::vector<std::string> frameSpellings(const std::vector<sed::TplValue>& values)
{
    std::vector<std::string> frames{};
    for (const sed::TplValue& value : values)
    {
        if (value.name == "frame")
        {
            frames.push_back(value.asString());
        }
    }
    return frames;
}

bool anyWarningContains(
    const std::vector<std::string>& warnings, const std::string_view needle)
{
    for (const std::string& warning : warnings)
    {
        if (warning.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

std::string exportText(const sed::Level& level)
{
    std::ostringstream output;
    sed::assets::saveToJKL(level, output);
    return output.str();
}

} // namespace

void runJklLegacyFrameTests()
{
    const std::string caesarParams =
        "thingflags=0X400048 numframes=2 "
        "frame=(8.05/8.35/10.57:0/270/0) frame=(8.05/8.35/6.92:0/270/0)70/0)";

    // D-10: the Caesar stray-')' frame is kept verbatim, warned once with
    // its row named, and round-trips byte-stably through the writer.
    {
        LoadOutcome outcome = loadJkl(thingSource(caesarParams));
        check(!outcome.threw, "legacy trailing frame: Caesar row loads");
        check(outcome.level.things.size() == 1, "legacy trailing frame: one thing");
        if (outcome.level.things.size() == 1)
        {
            const std::vector<std::string> frames =
                frameSpellings(outcome.level.things[0].values);
            check(frames.size() == 2
                    && frames[0] == "(8.05/8.35/10.57:0/270/0)"
                    && frames[1] == "(8.05/8.35/6.92:0/270/0)70/0)",
                "legacy trailing frame: both frame spellings kept verbatim");
            const std::string exported = exportText(outcome.level);
            check(exported.find("frame=(8.05/8.35/6.92:0/270/0)70/0)")
                    != std::string::npos,
                "legacy trailing frame: spelling survives export");
            LoadOutcome reloaded = loadJkl(exported);
            check(!reloaded.threw && exportText(reloaded.level) == exported,
                "legacy trailing frame: export/load/export is stable");
        }
        check(outcome.warnings.size() == 1
                && outcome.warnings[0].starts_with("THINGS row 0:")
                && outcome.warnings[0].find("legacy frame parameter")
                    != std::string::npos
                && outcome.warnings[0].find("unmatched ')'")
                    != std::string::npos,
            "legacy trailing frame: one row-named preservation warning");
    }

    // Later parameters stay separate tokens after the stray frame.
    {
        LoadOutcome outcome = loadJkl(thingSource(caesarParams + " cog=vader.cog"));
        check(!outcome.threw, "legacy trailing frame: follower loads");
        if (outcome.level.things.size() == 1)
        {
            const sed::TplValue* cog =
                findValue(outcome.level.things[0].values, "cog");
            check(cog && cog->asString() == "vader.cog",
                "legacy trailing frame: following parameter stays separate");
            bool exported = true;
            try
            {
                (void)sed::assets::buildJklExport(outcome.level);
            }
            catch (const std::exception&)
            {
                exported = false;
            }
            check(exported,
                "legacy trailing frame: stray frame may gain followers on save");
        }
    }

    // A balanced trailing suffix is ordinary text: kept, never warned.
    {
        LoadOutcome outcome = loadJkl(thingSource("frame=(1/2/3:4/5/6)abc"));
        check(!outcome.threw
                && !anyWarningContains(outcome.warnings, "legacy frame"),
            "legacy trailing frame: balanced suffix is warning-free");
    }

    // Uppercase FRAME with a stray ')' is the same legacy spelling.
    {
        LoadOutcome outcome = loadJkl(thingSource("FRAME=(1/2/3:4/5/6))"));
        check(!outcome.threw
                && anyWarningContains(outcome.warnings, "legacy frame parameter"),
            "legacy trailing frame: uppercase FRAME keeps the rule");
    }

    // Templates keep the stray frame and following parameters, warned too.
    {
        const std::string combined =
            "SECTION: HEADER\n"
            "VERSION 1\n"
            "END\n"
            "SECTION: TEMPLATES\n"
            "WORLD TEMPLATES 1\n"
            "crate none model3d=original.3do frame=(1/2/3:0/90/0)70/0) mass=3\n"
            "END\n"
            "SECTION: THINGS\n"
            "World things 1\n"
            "0: crate crate 1 2 3 0 0 0 -1\n"
            "END\n";
        LoadOutcome outcome = loadJkl(combined);
        check(!outcome.threw, "legacy trailing frame: template row loads");
        if (outcome.level.templates.size() == 1)
        {
            const std::vector<sed::TplValue>& values =
                outcome.level.templates[0].values;
            const sed::TplValue* frame = findValue(values, "frame");
            const sed::TplValue* mass = findValue(values, "mass");
            check(frame && frame->asString() == "(1/2/3:0/90/0)70/0)"
                    && mass && mass->asString() == "3",
                "legacy trailing frame: template spelling and follower kept");
            check(exportText(outcome.level)
                    .find("frame=(1/2/3:0/90/0)70/0)") != std::string::npos,
                "legacy trailing frame: template spelling survives export");
        }
        check(anyWarningContains(outcome.warnings, "TEMPLATES")
                && anyWarningContains(outcome.warnings, "legacy frame parameter"),
            "legacy trailing frame: template warning names its row");
    }

    // Near-miss malformed vectors stay refused at load.
    {
        const std::vector<std::string> refused{
            "frame=(1/2/3:4/5/6))(",
            "frame=(1/2/3:4/5)7)",
            "frame=(1/2/3:4/5/6/7)8)",
            "frame=(1/2/3:4/5/NaN)7)",
            "frame=(1/2/3/4/5/6)7)",
            "frame=(1/2/3:4/5/)7)",
            "frame=1/2/3:4/5/6)7)",
            "pos=(1/2/3:4/5/6)7)",
            "frame=(1/2/3:4/5/6)7) x=(1",
            "frame=(1 /2/3:4/5/6)7)",
        };
        for (const std::string& params : refused)
        {
            const LoadOutcome outcome = loadJkl(thingSource(params));
            check(outcome.threw,
                "legacy trailing frame: near-miss stays refused");
        }
    }

    // The unterminated final frame (September legacy spelling) is kept
    // verbatim with its warning, and exports unchanged.
    {
        LoadOutcome outcome = loadJkl(thingSource("health=75 frame=(1/2/3:4/5/6"));
        check(!outcome.threw, "legacy unterminated frame: row loads");
        if (outcome.level.things.size() == 1)
        {
            const std::vector<std::string> frames =
                frameSpellings(outcome.level.things[0].values);
            check(frames.size() == 1 && frames[0] == "(1/2/3:4/5/6",
                "legacy unterminated frame: spelling kept verbatim");
            check(exportText(outcome.level).find("frame=(1/2/3:4/5/6\n")
                    != std::string::npos
                || exportText(outcome.level).find("frame=(1/2/3:4/5/6")
                    != std::string::npos,
                "legacy unterminated frame: spelling survives export");
        }
        check(anyWarningContains(outcome.warnings, "legacy final frame"),
            "legacy unterminated frame: preservation warning emitted");
    }

    // An unterminated frame followed by another parameter is refused at
    // load: the exception is final-only.
    {
        const LoadOutcome outcome =
            loadJkl(thingSource("frame=(1/2/3:4/5/6 cog=vader.cog"));
        check(outcome.threw,
            "legacy unterminated frame: follower refused at load");
    }

    // The unterminated eyeoffset spelling keeps following parameters
    // separate and is warned.
    {
        LoadOutcome outcome =
            loadJkl(thingSource("eyeoffset=(1/2/3 cog=vader.cog"));
        check(!outcome.threw, "legacy eyeoffset: row loads");
        if (outcome.level.things.size() == 1)
        {
            const sed::TplValue* cog =
                findValue(outcome.level.things[0].values, "cog");
            check(cog && cog->asString() == "vader.cog",
                "legacy eyeoffset: following parameter stays separate");
        }
        check(anyWarningContains(outcome.warnings, "legacy eyeoffset"),
            "legacy eyeoffset: preservation warning emitted");
    }

    // D-11 writer guard: appending after a preserved unterminated final
    // frame refuses the export and writes nothing; removing the follower
    // restores the export.
    {
        LoadOutcome outcome = loadJkl(thingSource("health=75 frame=(1/2/3:4/5/6"));
        check(!outcome.threw && outcome.level.things.size() == 1,
            "writer guard: fixture loads");
        if (outcome.level.things.size() == 1)
        {
            sed::Level edited = outcome.level;
            edited.things[0].values.push_back(
                sed::deserializeTplValue("cog", "vader.cog"));
            bool refused = false;
            try
            {
                (void)sed::assets::buildJklExport(edited);
            }
            catch (const std::exception&)
            {
                refused = true;
            }
            check(refused,
                "writer guard: follower after unterminated frame refuses export");

            bool exported = true;
            try
            {
                (void)sed::assets::buildJklExport(outcome.level);
            }
            catch (const std::exception&)
            {
                exported = false;
            }
            check(exported, "writer guard: unedited level still exports");
        }
    }

    // D-10 native parity: the same stray frame in a JED/NED thing row is
    // kept verbatim and reported as a native thing warning.
    {
        sed::Level source{};
        source.clear();
        source.kind = sed::ProjectType::JKDF2;
        source.header.version = 1;
        sed::Thing& thing = source.newThing();
        thing.name = "caesar_native";
        thing.templateName = "crate";
        thing.sector = -1;
        thing.values.push_back(sed::deserializeTplValue(
            "frame", "(8.05/8.35/6.92:0/270/0)70/0)"));

        std::ostringstream savedStream;
        sed::assets::saveToJED(source, savedStream);

        std::vector<std::string> warnings{};
        sed::assets::JedLoadOptions options{};
        options.onDiagnostic = [&warnings](const sed::DiagnosticSeverity severity,
                                           const std::string_view message)
        {
            if (severity == sed::DiagnosticSeverity::warning)
            {
                warnings.emplace_back(message);
            }
        };
        std::istringstream reloadStream(savedStream.str());
        const sed::Level reloaded = sed::assets::loadFromJED(reloadStream, options);
        check(reloaded.things.size() == 1, "legacy JED: thing reloads");
        if (reloaded.things.size() == 1)
        {
            const sed::TplValue* frame =
                findValue(reloaded.things[0].values, "frame");
            check(frame && frame->asString() == "(8.05/8.35/6.92:0/270/0)70/0)",
                "legacy JED: stray frame kept verbatim");
        }
        check(anyWarningContains(warnings, "Native thing 0:")
                && anyWarningContains(warnings, "legacy frame parameter"),
            "legacy JED: native thing warning emitted");
    }
}

} // namespace sed::tests
