#include "test_common.hpp"

#include "sed/assets/tpl.hpp"
#include "sed/level/thing.hpp"
#include "sed/level/thing_frames.hpp"
#include "sed/parse_frame.hpp"
#include "sed/text.hpp"
#include "sed/values.hpp"

#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace sed::tests
{
namespace
{

using level::PreparedThingFrames;

bool sameValue(const TplValue& a, const TplValue& b)
{
    return a.name == b.name && a.type == b.type && a.adjType == b.adjType && a.intValue == b.intValue
        && a.floatValue == b.floatValue && a.stringValue == b.stringValue;
}

bool sameValues(const std::vector<TplValue>& a, const std::vector<TplValue>& b)
{
    if ( a.size() != b.size() )
    {
        return false;
    }
    for ( std::size_t index = 0; index < a.size(); ++index )
    {
        if ( !sameValue(a[index], b[index]) )
        {
            return false;
        }
    }
    return true;
}

bool isTypedFrame(const TplValue& value)
{
    return value.type == ValueType::frame && text::iequalsAscii(value.name, "frame");
}

// The GUI frame editor's composition: Delete removes one local frame and
// resynchronizes `numframes`; Add appends one parsed frame and resynchronizes.
Thing frameEditorComposition(Thing thing, const std::vector<FrameValue>& frames)
{
    for ( std::size_t index = 0; index < thing.values.size(); )
    {
        if ( isTypedFrame(thing.values[index]) )
        {
            thing.values.erase(thing.values.begin() + static_cast<std::ptrdiff_t>(index));
            level::thingSyncNumFrames(thing);
            index = 0;
            continue;
        }
        ++index;
    }
    for ( const FrameValue& frame : frames )
    {
        TplValue next{};
        next.name = "frame";
        next.type = ValueType::frame;
        next.adjType = tplAdjTypeForName(next.name);
        check(next.parse(format::frameValue(frame)), "thing frames parity: the frame editor parses the frame text");
        thing.values.push_back(next);
        level::thingSyncNumFrames(thing);
    }
    return thing;
}

Level framesLevel()
{
    Level level{};
    Thing& keep = level.newThing();
    keep.name = "keep";
    keep.templateName = "keep";
    keep.values.push_back(deserializeTplValue("frame", "(9/9/9:0/0/0)"));
    Thing& lift = level.newThing();
    lift.name = "lift";
    lift.templateName = "lift";
    lift.x = 1.0;
    lift.y = 2.0;
    lift.z = 3.0;
    lift.values.push_back(deserializeTplValue("health", "100"));
    lift.values.push_back(deserializeTplValue("numframes", "2"));
    lift.values.push_back(deserializeTplValue("frame", "(1/2/3:0/0/0)"));
    lift.values.push_back(deserializeTplValue("thingflags", "0x400"));
    lift.values.push_back(deserializeTplValue("frame", "(1/2/4:0/90/0)"));
    lift.values.push_back(deserializeTplValue("customparam", "kept verbatim"));
    return level;
}

void runSetThingFramesMatchesFrameEditor()
{
    const Level original = framesLevel();
    const std::vector<FrameValue> frames{
        FrameValue{ 1.0, 2.0, 3.0, 0.0, 0.0, 0.0 },
        FrameValue{ 1.0, 2.0, 5.5, 0.0, 90.0, 0.0 },
        FrameValue{ -7.25, 0.125, 1e-3, 10.0, -45.0, 180.0 },
    };
    const PreparedThingFrames prepared = level::prepareSetThingFrames(original, nullptr, 1, frames);
    check(prepared.ok(), "thing frames: a valid replacement prepares");
    check(prepared.previousFrameCount == 2 && prepared.frames.size() == 3 && !prepared.pathMove,
        "thing frames: the preview reports the previous and new frame counts");

    Level applied = original;
    check(prepared.apply(applied), "thing frames: the prepared replacement applies");
    const Thing expected = frameEditorComposition(original.things[1], frames);
    check(sameValues(applied.things[1].values, expected.values),
        "thing frames: the result equals the GUI frame editor's delete-then-add composition");
    check(level::thingCountFrames(applied.things[1]) == 3, "thing frames: three local frames remain");
    const std::vector<TplValue>& values = applied.things[1].values;
    check(values.size() == 7 && text::iequalsAscii(values[0].name, "health")
            && text::iequalsAscii(values[1].name, "thingflags") && values[2].name == "customparam"
            && values[2].stringValue == "kept verbatim" && text::iequalsAscii(values[3].name, "numframes")
            && values[3].type == ValueType::integer && values[3].intValue == 3 && isTypedFrame(values[4])
            && isTypedFrame(values[5]) && isTypedFrame(values[6]),
        "thing frames: other values keep their order and numframes precedes the appended frames");
    check(values.size() == 7 && values[4].stringValue == "(1/2/3:0/0/0)" && values[5].stringValue == "(1/2/5.5:0/90/0)"
            && values[6].stringValue == "(-7.25/0.125/0.001:10/-45/180)",
        "thing frames: frames are written in authored shortest round-trip form");
    check(sameValues(applied.things[0].values, original.things[0].values) && applied.things.size() == original.things.size()
            && applied.things[1].x == 1.0 && applied.things[1].name == "lift",
        "thing frames: the pose, other Things and their frames are untouched");
}

void runSetThingFramesClearsFrames()
{
    const Level original = framesLevel();
    const PreparedThingFrames prepared = level::prepareSetThingFrames(original, nullptr, 1, {});
    check(prepared.ok() && prepared.previousFrameCount == 2, "thing frames clear: an empty list prepares");
    Level applied = original;
    check(prepared.apply(applied), "thing frames clear: applies");
    const Thing expected = frameEditorComposition(original.things[1], {});
    check(sameValues(applied.things[1].values, expected.values),
        "thing frames clear: equals deleting every frame in the GUI");
    bool hasNumFrames = false;
    for ( const TplValue& value : applied.things[1].values )
    {
        hasNumFrames = hasNumFrames || text::iequalsAscii(value.name, "numframes");
    }
    check(level::thingCountFrames(applied.things[1]) == 0 && !hasNumFrames,
        "thing frames clear: frames and numframes are removed");
}

void runSetThingFramesRefusals()
{
    const Level original = framesLevel();
    const auto refused = [](const Level& level, const std::size_t thing, const std::vector<FrameValue>& frames,
                             const char* code, const char* message)
        {
            const PreparedThingFrames prepared = level::prepareSetThingFrames(level, nullptr, thing, frames);
            const bool sameCode = !prepared.ok() && prepared.failure->code == code && !prepared.failure->message.empty();
            check(sameCode, message);
            if ( !sameCode && prepared.failure )
            {
                std::cerr << "  code: " << prepared.failure->code << '\n';
            }
            Level target = level;
            check(!prepared.apply(target) && sameValues(target.things[1].values, level.things[1].values),
                "thing frames: a refused preparation never applies");
        };

    refused(original, 2, { FrameValue{} }, "thingFrameTargetOutOfRange", "thing frames: an unknown Thing is refused");

    std::vector<FrameValue> tooMany(level::kMaxThingFrames + 1);
    refused(original, 1, tooMany, "thingFrameLimit", "thing frames: more than the frame ceiling is refused");
    const std::vector<FrameValue> atLimit(level::kMaxThingFrames);
    check(level::prepareSetThingFrames(original, nullptr, 1, atLimit).ok(), "thing frames: the frame ceiling is inclusive");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    refused(original, 1, { FrameValue{ nan, 0, 0, 0, 0, 0 } }, "invalidThingFrame", "thing frames: NaN position is refused");
    refused(original, 1, { FrameValue{ 0, 0, 0, 0, inf, 0 } }, "invalidThingFrame", "thing frames: infinite yaw is refused");
    refused(original, 1, { FrameValue{ 0, 0, 0, 0, 0, -inf } }, "invalidThingFrame",
        "thing frames: infinite roll is refused");
    refused(original, 1, { FrameValue{ 0, 0, level::kMaxWorkbenchCoordinate * 2.0, 0, 0, 0 } }, "invalidThingFrame",
        "thing frames: an out-of-range coordinate is refused");
    check(level::prepareSetThingFrames(original, nullptr, 1,
              std::vector<FrameValue>{ FrameValue{ level::kMaxWorkbenchCoordinate, -level::kMaxWorkbenchCoordinate, 0,
                  -level::kMaxWorkbenchCoordinate, level::kMaxWorkbenchCoordinate, 0 } })
              .ok(),
        "thing frames: the coordinate bound is inclusive");

    Level untyped = original;
    TplValue legacy{};
    legacy.name = "frame";
    legacy.type = ValueType::str;
    legacy.stringValue = "(1/2/3:4/5/6";
    untyped.things[1].values.push_back(legacy);
    refused(untyped, 1, { FrameValue{} }, "untypedThingFrame",
        "thing frames: a frame the editor cannot parse is preserved by refusing");
}

void runSetThingFramesStaleApply()
{
    const Level original = framesLevel();
    const std::vector<FrameValue> frames{ FrameValue{ 4.0, 5.0, 6.0, 0.0, 0.0, 0.0 } };
    const PreparedThingFrames prepared = level::prepareSetThingFrames(original, nullptr, 1, frames);
    check(prepared.ok(), "thing frames stale: prepares");

    Level edited = original;
    edited.things[1].values.push_back(deserializeTplValue("mass", "5"));
    const std::vector<TplValue> before = edited.things[1].values;
    check(!prepared.apply(edited) && sameValues(edited.things[1].values, before),
        "thing frames stale: a changed Thing is never overwritten");

    Level shrunk = original;
    shrunk.things.resize(1);
    check(!prepared.apply(shrunk) && shrunk.things.size() == 1, "thing frames stale: a removed Thing is refused");

    Level renamed = original;
    renamed.things[1].templateName = "door";
    check(!prepared.apply(renamed) && renamed.things[1].values.size() == original.things[1].values.size(),
        "thing frames stale: a changed template is refused");
}

ThingTemplate makeTemplate(const char* name, const char* parent, const std::vector<std::pair<const char*, const char*>>& values)
{
    ThingTemplate tpl{};
    tpl.name = name;
    tpl.parent = parent;
    for ( const auto& [key, text] : values )
    {
        tpl.values.push_back(deserializeTplValue(key, text));
    }
    return tpl;
}

void runSetThingFramesPathCapacity()
{
    Level level{};
    level.templates.push_back(makeTemplate("_lift", "none",
        { { "move", "path" }, { "numframes", "4" }, { "frame", "(0/0/0:0/0/0)" } }));
    level.templates.push_back(makeTemplate("lift", "_lift", { { "frame", "(0/0/1:0/0/0)" } }));
    Thing& thing = level.newThing();
    thing.name = "lift";
    thing.templateName = "lift";

    check(level::thingMovesOnPath(level, nullptr, thing), "path capacity: an inherited move=path is detected");
    const level::ThingPathFrameState state = level::thingInheritedPathFrameState(level, nullptr, thing);
    check(state.capacity == 4 && state.activeFrames == 2 && state.ignoredFrames == 0,
        "path capacity: base-to-derived numframes allocates and both template frames are active");

    const std::vector<FrameValue> two(2);
    const PreparedThingFrames fits = level::prepareSetThingFrames(level, nullptr, 0, two);
    check(fits.ok() && fits.pathMove && fits.pathState.capacity == 4 && fits.pathState.activeFrames == 2,
        "path capacity: frames that fit the remaining buffer prepare and report the inherited state");

    // The GUI frame editor refuses each Add once inherited + local frames
    // reach the capacity; replay that rule frame by frame.
    std::size_t guiAccepted = 0;
    for ( std::size_t attempt = 0; attempt < 3; ++attempt )
    {
        if ( state.activeFrames + guiAccepted >= state.capacity )
        {
            break;
        }
        ++guiAccepted;
    }
    check(guiAccepted == 2, "path capacity: the GUI add rule accepts exactly the remaining capacity");
    const std::vector<FrameValue> three(3);
    const PreparedThingFrames overflow = level::prepareSetThingFrames(level, nullptr, 0, three);
    check(!overflow.ok() && overflow.failure->code == "pathFrameCapacityExceeded",
        "path capacity: one frame beyond the inherited buffer is refused");

    Level withLocalFrame = level;
    withLocalFrame.things[0].values.push_back(
        deserializeTplValue("frame", "(0/0/2:0/0/0)"));
    check(level::prepareSetThingFrames(withLocalFrame, nullptr, 0, two).ok()
            && !level::prepareSetThingFrames(withLocalFrame, nullptr, 0, three).ok(),
        "path capacity: replacement does not count the local frames being replaced");

    Level early{};
    early.templates.push_back(makeTemplate("_early", "none",
        { { "move", " Path " }, { "frame", "(0/0/0:0/0/0)" }, { "numframes", "1" }, { "frame", "(0/0/2:0/0/0)" },
            { "frame", "(0/0/3:0/0/0)" } }));
    Thing& earlyThing = early.newThing();
    earlyThing.templateName = "_early";
    const level::ThingPathFrameState earlyState = level::thingInheritedPathFrameState(early, nullptr, earlyThing);
    check(earlyState.capacity == 1 && earlyState.activeFrames == 1 && earlyState.ignoredFrames == 2,
        "path capacity: frames before allocation or beyond capacity are ignored");
    check(level::thingMovesOnPath(early, nullptr, earlyThing), "path capacity: move matching trims and ignores case");
    check(!level::prepareSetThingFrames(early, nullptr, 0, std::vector<FrameValue>(1)).ok(),
        "path capacity: a full inherited buffer refuses any local frame");

    Level unallocated{};
    unallocated.templates.push_back(makeTemplate("_free", "none", { { "move", "path" } }));
    Thing& freeThing = unallocated.newThing();
    freeThing.templateName = "_free";
    check(level::prepareSetThingFrames(unallocated, nullptr, 0, std::vector<FrameValue>(8)).ok(),
        "path capacity: without an inherited allocation local frames are not limited by the buffer");

    Level notPath = framesLevel();
    notPath.things[1].values.push_back(deserializeTplValue("move", "none"));
    check(!level::thingMovesOnPath(notPath, nullptr, notPath.things[1]), "path capacity: move=none is not a path mover");
}

void runSetThingFramesProjectTemplates()
{
    std::istringstream text("_dblift none move=path numframes=2 frame=(0/0/0:0/0/0)\ndblift _dblift\n");
    const assets::Templates templates = assets::loadTemplates(text);

    Level level{};
    Thing& thing = level.newThing();
    thing.templateName = "dblift";

    check(level::thingMovesOnPath(level, &templates, thing), "project templates: move=path resolves from the database");
    const level::ThingPathFrameState state = level::thingInheritedPathFrameState(level, &templates, thing);
    check(state.capacity == 2 && state.activeFrames == 1, "project templates: the database chain allocates the buffer");
    check(level::prepareSetThingFrames(level, &templates, 0, std::vector<FrameValue>(1)).ok()
            && !level::prepareSetThingFrames(level, &templates, 0, std::vector<FrameValue>(2)).ok(),
        "project templates: the remaining database buffer bounds the replacement");

    level.templates.push_back(makeTemplate("dblift", "none", { { "move", "path" }, { "numframes", "5" } }));
    const level::ThingPathFrameState embedded = level::thingInheritedPathFrameState(level, &templates, level.things[0]);
    check(embedded.capacity == 5 && embedded.activeFrames == 1,
        "project templates: embedded numframes wins while frames still fall back per parameter");
}

} // namespace

void runThingFramesTests()
{
    runSetThingFramesMatchesFrameEditor();
    runSetThingFramesClearsFrames();
    runSetThingFramesRefusals();
    runSetThingFramesStaleApply();
    runSetThingFramesPathCapacity();
    runSetThingFramesProjectTemplates();
}

} // namespace sed::tests
