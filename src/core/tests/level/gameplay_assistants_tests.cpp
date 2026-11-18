#include "test_common.hpp"

#include "sed/assets/cog_compiler.hpp"
#include "sed/assets/cog_script.hpp"
#include "sed/level/gameplay_assistants.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

using level::AssistantKind;
using level::thingCountFrames;
using level::thingFlags;
using level::thingSyncNumFrames;

// Stand-ins with the stock scripts' symbols and behaviour, written for these
// tests (the stock scripts themselves are LucasArts' and are not in the repo).
const char* const kDoorScript = R"(symbols
message startup
message activated
message arrived
message timer
thing door0 linkid=0
thing door1 linkid=1
thing door2 linkid=2
thing door3 linkid=3
flex movespeed=8.0
flex sleeptime=2.0
flex lightvalue=0.5
sector doorsector local
int i local
int moving local
int open local
int count local
end
code
startup:
  doorsector = GetThingSector(door0);
  SetSectorAdjoins(doorsector, 0);
  SetSectorLight(doorsector, lightvalue, 0.0);
  return;
activated:
  call status;
  if (moving) return;
  if (open == 0) { SetSectorAdjoins(doorsector, 1); call go1; }
  return;
arrived:
  call status;
  if (moving) return;
  if (open == count) SetTimer(sleeptime);
  else if (open == 0) SetSectorAdjoins(doorsector, 0);
  return;
timer:
  call go0;
  return;
go1:
  for (i = 0; i <= 3; i = i + 1) if (door0[i] >= 0) MoveToFrame(door0[i], 1, movespeed);
  return;
go0:
  for (i = 0; i <= 3; i = i + 1) if (door0[i] >= 0) MoveToFrame(door0[i], 0, movespeed);
  return;
status:
  moving = 0;
  open = 0;
  count = 0;
  for (i = 0; i <= 3; i = i + 1) if (door0[i] >= 0) {
    count = count + 1;
    moving = moving + IsThingMoving(door0[i]);
    open = open + GetCurFrame(door0[i]);
  }
  return;
end
)";

const char* const kElevatorScript = R"(symbols
message crossed
message activated
message arrived
message timer
surface lower_adjoin0 linkid=1
surface lower_adjoin1 linkid=1
surface lower_adjoin2 linkid=1
surface button linkid=1
thing elevator linkid=2
flex start_wait=0.25
flex sleeptime=2.0
flex speed=4.0
end
code
crossed:
  Sleep(start_wait);
activated:
  if (GetSenderId() != 1) return;
  if (GetWallCel(button) == 1) return;
  SetWallCel(button, 1);
  MoveToFrame(elevator, 1, speed);
  return;
arrived:
  if (GetCurFrame(elevator) == 0) SetWallCel(button, 0);
  else SetTimer(sleeptime);
  return;
timer:
  MoveToFrame(elevator, 0, speed);
  return;
end
)";

const char* const kSwitchScript = R"(symbols
thing door0 linkid=1
thing door1 linkid=1
thing door2 linkid=1
thing door3 linkid=1
surface switch0 linkid=2
surface switch1 linkid=2
flex movespeed=8.0
flex sleeptime=2.0
flex lightvalue=0.5
int doorsector local
int i local
int moving local
int open local
int count local
message startup
message activated
message arrived
end
code
startup:
  doorsector = GetThingSector(door0);
  SetSectorAdjoins(doorsector, 0);
  return;
activated:
  if (GetSenderId() != 2) return;
  call status;
  if (moving) return;
  if (open != 0) return;
  SetWallCel(switch0, 1);
  if (switch1 >= 0) SetWallCel(switch1, 1);
  SetSectorAdjoins(doorsector, 1);
  for (i = 0; i <= 3; i = i + 1) if (door0[i] >= 0) MoveToFrame(door0[i], 1, movespeed);
  return;
arrived:
  call status;
  if (moving) return;
  if (open == count) {
    Sleep(sleeptime);
    for (i = 0; i <= 3; i = i + 1) if (door0[i] >= 0) MoveToFrame(door0[i], 0, movespeed);
  } else if (open == 0) {
    SetWallCel(switch0, 0);
    if (switch1 >= 0) SetWallCel(switch1, 0);
    SetSectorAdjoins(doorsector, 0);
  }
  return;
status:
  moving = 0;
  open = 0;
  count = 0;
  for (i = 0; i <= 3; i = i + 1) if (door0[i] >= 0) {
    count = count + 1;
    moving = moving + IsThingMoving(door0[i]);
    open = open + GetCurFrame(door0[i]);
  }
  return;
end
)";

std::vector<CogValue> symbolsOf(const char* text)
{
    return assets::loadCogSymbolsFromTextResult(text, false).symbols;
}

ThingTemplate pathTemplate()
{
    ThingTemplate tpl;
    tpl.name = "slab";
    tpl.parent = "none";
    tpl.values.push_back(deserializeTplValue("move", "path"));
    return tpl;
}

Sector boxSector(const double x0)
{
    Sector sector;
    for (const double z : { 0.0, 1.0 })
    {
        for (const auto& [x, y] : { std::pair{ 0.0, 0.0 }, std::pair{ 1.0, 0.0 }, std::pair{ 1.0, 1.0 }, std::pair{ 0.0, 1.0 } })
            sector.vertices.push_back(Vertex{ x0 + x, y, z, 0 });
    }
    for (const std::vector<std::int32_t>& face : { std::vector<std::int32_t>{ 1, 2, 6, 5 }, { 0, 1, 5, 4 }, { 0, 3, 2, 1 } })
    {
        Surface surface;
        surface.vertices = face;
        sector.surfaces.push_back(surface);
    }
    return sector;
}

/** Two rooms side by side, adjoined through sector 0's surface 0; a player start, a slab door in sector 0, and props. */
Level fixture()
{
    Level level;
    level.templates.push_back(pathTemplate());
    level.sectors.push_back(boxSector(0.0));
    level.sectors.push_back(boxSector(1.0));
    level.sectors[0].surfaces[0].adjoin = SurfaceRef{ 1, 0 };
    level.sectors[1].surfaces[0].adjoin = SurfaceRef{ 0, 0 };
    const auto add = [&](const char* name, const char* templ, const std::int32_t sector, const double x) {
        Thing& thing = level.newThing();
        thing.name = name;
        thing.templateName = templ;
        thing.sector = sector;
        thing.x = x;
        thing.y = 0.5;
        thing.z = 0.5;
    };
    add("start", "walkplayer", 1, 1.5);   // 0
    add("left", "slab", 0, 0.25);         // 1
    add("right", "slab", 0, 0.75);        // 2
    add("lift", "slab", 1, 1.5);          // 3
    add("crate", "crate", 1, 1.2);        // 4
    add("camera", "slab", 1, 1.6);        // 5
    return level;
}

bool sameLevel(const Level& a, const Level& b)
{
    if (a.things.size() != b.things.size() || a.cogs.size() != b.cogs.size()) return false;
    for (std::size_t i = 0; i < a.things.size(); ++i)
    {
        if (a.things[i].values.size() != b.things[i].values.size()) return false;
    }
    return true;
}

std::string value(const Level& level, const std::int32_t cog, const char* name)
{
    for (const CogValue& v : level.cogs[static_cast<std::size_t>(cog)].values)
    {
        if (sed::text::iequalsAscii(v.name, name)) return v.asJedString();
    }
    return "?";
}

void report(const level::AssistantCheck& check)
{
    if (check.passed) return;
    for (const std::string& line : check.expectations) std::fprintf(stderr, "  %s\n", line.c_str());
    for (const std::string& line : check.timeline) std::fprintf(stderr, "    %s\n", line.c_str());
}

void testDoor()
{
    Level level = fixture();
    level::DoorAssistantRequest request;
    request.pieces = { 1, 2 };
    request.openOffset = { 0.0, 0.0, 0.9 };
    const auto result = level::levelDoorAssistant(level, nullptr, request, symbolsOf(kDoorScript));
    check(result.ok() && result.cog >= 0, "door assistant: two pieces become a door");
    check(thingCountFrames(level.things[1]) == 2 && thingCountFrames(level.things[2]) == 2,
        "door assistant: each piece gets a closed and an open frame");
    check((thingFlags(level, level.things[1]) & 0x408) == 0x408, "door assistant: the pieces are part of the world and COG-linked");
    check(value(level, result.cog, "door0") == "1" && value(level, result.cog, "door1") == "2" && value(level, result.cog, "door2") == "-1",
        "door assistant: the pieces fill door0 and door1, and the rest are unbound");
    const auto check1 = level::checkAssistant(level, AssistantKind::door, result.cog, kDoorScript);
    report(check1);
    check(check1.passed && check1.expectations.size() >= 4 && !check1.timeline.empty(),
        "door assistant: in the runtime it opens on use, shows and hides its sector's adjoins, and closes by itself");

    const Level before = level;
    const auto again = level::levelDoorAssistant(level, nullptr, request, symbolsOf(kDoorScript));
    check(!again.ok() && again.failure->code == "assistantAlreadyMoves" && sameLevel(level, before),
        "door assistant: a piece that already has frames is refused and nothing changes");
    Level fresh = fixture();
    request.pieces = { 4 };
    check(level::levelDoorAssistant(fresh, nullptr, request, symbolsOf(kDoorScript)).failure->code == "assistantNotPathMover",
        "door assistant: a Thing that doesn't move on a path is refused");
    request.pieces = { 1, 1 };
    check(level::levelDoorAssistant(fresh, nullptr, request, symbolsOf(kDoorScript)).failure->code == "assistantDuplicateTarget",
        "door assistant: a piece listed twice is refused");
    request.pieces = { 1 };
    request.openOffset = { 0.0, 0.0, 0.0 };
    check(level::levelDoorAssistant(fresh, nullptr, request, symbolsOf(kDoorScript)).failure->code == "assistantInvalidSetting",
        "door assistant: a door that doesn't move is refused");
    request.openOffset = { 0.0, 0.0, 1.0 };
    check(level::levelDoorAssistant(fresh, nullptr, request, symbolsOf(kElevatorScript)).failure->code == "assistantScriptMismatch",
        "door assistant: a script without door0..door3 is refused");
    check(fresh.cogs.empty() && thingCountFrames(fresh.things[1]) == 0, "door assistant: refusals leave the level unchanged");

    // A door whose script never closes it fails the check.
    std::string broken = kDoorScript;
    broken.replace(broken.find("timer:\n  call go0;"), 18, "timer:\n  return;  ");
    const auto check2 = level::checkAssistant(level, AssistantKind::door, result.cog, broken);
    check(!check2.passed && std::any_of(check2.expectations.begin(), check2.expectations.end(),
                                [](const std::string& line) { return line.rfind("failed: it closes again by itself", 0) == 0; }),
        "door assistant: the check catches a door that never closes");
}

void testElevator()
{
    Level level = fixture();
    level::ElevatorAssistantRequest request;
    request.elevator = 3;
    request.travel = { 0.0, 0.0, 2.0 };
    request.button = SurfaceRef{ 1, 1 };
    request.callAdjoins = { SurfaceRef{ 0, 0 } };
    const auto result = level::levelElevatorAssistant(level, nullptr, request, symbolsOf(kElevatorScript));
    check(result.ok(), "elevator assistant: a lift with a button and a call adjoin");
    check(value(level, result.cog, "lower_adjoin0") == std::to_string(level.getGlobalSurfaceNumber(0, 0))
            && value(level, result.cog, "button") == std::to_string(level.getGlobalSurfaceNumber(1, 1)),
        "elevator assistant: the surfaces are bound by their level-wide numbers");
    const auto ran = level::checkAssistant(level, AssistantKind::elevator, result.cog, kElevatorScript);
    report(ran);
    check(ran.passed, "elevator assistant: in the runtime the button and the call adjoin each send it up, and it comes back down");
    Level fresh = fixture();
    request.callAdjoins = { SurfaceRef{ 1, 2 } };
    check(level::levelElevatorAssistant(fresh, nullptr, request, symbolsOf(kElevatorScript)).failure->code == "assistantNotAdjoin",
        "elevator assistant: a call surface that isn't an adjoin is refused");
    request.callAdjoins.clear();
    request.button.reset();
    check(level::levelElevatorAssistant(fresh, nullptr, request, symbolsOf(kElevatorScript)).failure->code == "assistantNothingSelected",
        "elevator assistant: a lift nothing can call is refused");
}

void testSwitch()
{
    Level level = fixture();
    level::SwitchAssistantRequest request;
    request.pieces = { 1 };
    request.switches = { SurfaceRef{ 1, 1 } };
    const auto result = level::levelSwitchAssistant(level, nullptr, request, symbolsOf(kSwitchScript));
    check(result.ok(), "switch assistant: a locked door with a switch");
    const auto ran = level::checkAssistant(level, AssistantKind::switchDoor, result.cog, kSwitchScript);
    report(ran);
    check(ran.passed, "switch assistant: in the runtime the door is locked, the switch opens it and shows cel 1, and it closes");
}

void testObjectives()
{
    Level level = fixture();
    level::ObjectivesAssistantRequest request;
    request.goalBase = 3000;
    request.goals = { { level::GoalTrigger::reach, 1 }, { level::GoalTrigger::use, 4 }, { level::GoalTrigger::kill, 3 } };
    request.exit = SurfaceRef{ 1, 0 };
    const auto result = level::levelObjectivesAssistant(level, request);
    check(result.ok() && level.cogs[static_cast<std::size_t>(result.cog)].name == "red_goals.cog",
        "objectives assistant: three goals and an exit");
    check(value(level, result.cog, "reach0") == "1" && value(level, result.cog, "use1") == "4" && value(level, result.cog, "kill2") == "3"
            && value(level, result.cog, "use0") == "-1",
        "objectives assistant: each goal's target fills its own slot");
    const auto ran = level::checkAssistant(level, AssistantKind::objectives, result.cog,
        level::assistantScriptText(level::kObjectivesAssistantScript));
    report(ran);
    check(ran.passed, "objectives assistant: in the runtime the goals show, complete in turn, and the exit ends the level only after them");
    check(level::levelObjectivesAssistant(level, request).failure->code == "assistantAlreadyPlaced",
        "objectives assistant: a second objectives COG is refused");

    Level noExit = fixture();
    request.exit.reset();
    request.goals = { { level::GoalTrigger::reach, 0 } };
    const auto ended = level::levelObjectivesAssistant(noExit, request);
    check(level::checkAssistant(noExit, AssistantKind::objectives, ended.cog, level::assistantScriptText(level::kObjectivesAssistantScript))
              .passed,
        "objectives assistant: without an exit the last goal ends the level");
    request.goals = { { level::GoalTrigger::reach, 9 } };
    Level empty = fixture();
    check(level::levelObjectivesAssistant(empty, request).failure->code == "assistantTargetOutOfRange" && empty.cogs.empty(),
        "objectives assistant: a goal in a sector that doesn't exist is refused");
    for (const std::string_view script : { level::kObjectivesAssistantScript, level::kCutsceneAssistantScript })
    {
        for (const auto target : { assets::CogPreflightTarget::jk, assets::CogPreflightTarget::mots })
        {
            const auto compiled = assets::compileCog(script, level::assistantScriptText(script), target);
            check(compiled.loads && compiled.count(assets::CogDiagnosticSeverity::error) == 0
                    && compiled.count(assets::CogDiagnosticSeverity::warning) == 0,
                "assistants: RED's own scripts compile cleanly for JK and MotS");
        }
    }
}

void testCutscene()
{
    Level level = fixture();
    level::CutsceneAssistantRequest request;
    request.camera = 5;
    request.trigger = 1;
    check(level::levelCutsceneAssistant(level, nullptr, request).failure->code == "assistantNoCameraPath",
        "cutscene assistant: a camera without a path is refused");
    for (const char* frame : { "(1.6/0.5/0.5:0/0/0)", "(1.6/0.5/1.5:0/0/0)", "(1.2/0.5/1.5:0/90/0)" })
        level.things[5].values.push_back(deserializeTplValue("frame", frame));
    thingSyncNumFrames(level.things[5]);
    const auto result = level::levelCutsceneAssistant(level, nullptr, request);
    check(result.ok() && value(level, result.cog, "lastframe") == "2" && value(level, result.cog, "onstartup") == "0",
        "cutscene assistant: the camera's last frame and the trigger are bound");
    const auto ran = level::checkAssistant(level, AssistantKind::cutscene, result.cog,
        level::assistantScriptText(level::kCutsceneAssistantScript));
    report(ran);
    check(ran.passed, "cutscene assistant: in the runtime the view follows the camera, holds the player, returns, and plays once");
}

} // namespace

void runGameplayAssistantsTests()
{
    testDoor();
    testElevator();
    testSwitch();
    testObjectives();
    testCutscene();
}

} // namespace sed::tests
