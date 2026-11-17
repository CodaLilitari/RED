#include "test_common.hpp"

#include "sed/assets/cog_runtime.hpp"
#include "sed/level.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

using sed::assets::CogPreflightTarget;
using sed::assets::CogRuntime;
using sed::assets::CogRuntimeBinding;
using sed::assets::CogRuntimeEvent;
using sed::assets::CogRuntimeValue;
using sed::assets::CogWorld;

std::string script(const std::string& symbols, const std::string& code)
{
    return "symbols\n" + symbols + "end\ncode\n" + code + "end\n";
}

std::int32_t add(CogRuntime& runtime, const std::string& text, const std::vector<CogRuntimeBinding>& bindings = {})
{
    const auto compiled = sed::assets::compileCog("t.cog", text, CogPreflightTarget::jk);
    std::string error;
    const std::int32_t index = runtime.addCog(compiled, "t.cog", bindings, error);
    check(index >= 0, ("cog runtime: the test script loads (" + error + ")").c_str());
    return index;
}

std::vector<CogRuntimeEvent> ofKind(const CogRuntime& runtime, const std::string& kind)
{
    std::vector<CogRuntimeEvent> out;
    for (const auto& e : runtime.events())
    {
        if (e.kind == kind) out.push_back(e);
    }
    return out;
}

bool near(const double a, const double b, const double tolerance = 0.02) { return std::abs(a - b) <= tolerance; }

CogWorld doorWorld()
{
    CogWorld world;
    sed::assets::CogWorldThing door;
    door.name = "door";
    door.frames = { { 0, 0, 0 }, { 0, 0, 1 } };
    world.things.push_back(door);
    sed::assets::CogWorldThing player;
    player.name = "walkplayer";
    world.things.push_back(player);
    world.player = 1;
    return world;
}

void testMachine()
{
    CogRuntime runtime(CogWorld{}, CogPreflightTarget::jk);
    const std::int32_t cog = add(runtime, script("message startup\nint i local\nflex f local\nint ne local\nflex q local\nflex d local\n",
        "startup:\n  i = 1;\n  i = i + 1;\n  d = 5 - 3 % 2;\n  f = 7 / 0;\n  ne = 0.5 != 0.7;\n  call half;\n  return;\nhalf:\n  q = 3 / 2;\n  return;\n"));
    runtime.startup();
    const auto i = runtime.symbolValue(cog, "i");
    check(i && i->type == CogRuntimeValue::Type::Flex && i->f == 2.0f,
        "cog runtime: arithmetic always gives a flex, as in the engine");
    check(runtime.symbolValue(cog, "f")->f == 0.0f, "cog runtime: dividing by zero gives 0");
    check(runtime.symbolValue(cog, "d")->f == 4.0f, "cog runtime: subtraction and % keep their operand order");
    check(runtime.symbolValue(cog, "ne")->i == 0, "cog runtime: != compares integers, so 0.5 != 0.7 is false");
    check(runtime.symbolValue(cog, "q")->f == 1.5f, "cog runtime: call runs a label and returns");

    CogRuntime loop(CogWorld{}, CogPreflightTarget::jk, { 5000, 1000 });
    add(loop, script("message startup\nint x local\n", "startup:\n  for (x = 0; ; x = x + 1) Print(\"spin\");\n  return;\n"));
    loop.startup();
    check(!ofKind(loop, "error").empty() && ofKind(loop, "error").front().text.find("freeze") != std::string::npos,
        "cog runtime: a for loop without a condition runs forever (an empty stack reads -1) and is stopped");

    CogRuntime jump(CogWorld{}, CogPreflightTarget::jk);
    add(jump, script("message startup\n", "startup:\n  goto missing;\n  return;\n"));
    jump.startup();
    check(!ofKind(jump, "error").empty(), "cog runtime: a jump to a missing label is caught");

    CogRuntime stack(CogWorld{}, CogPreflightTarget::jk);
    std::string pushes = "startup:\n";
    for (int k = 0; k < 70; ++k) pushes += "  Rand();\n";
    pushes += "  Print(\"done\");\n  return;\n";
    add(stack, script("message startup\n", pushes));
    stack.startup();
    check(ofKind(stack, "print").size() == 1, "cog runtime: 70 leftover values overflow the 64-value stack harmlessly");
}

void testDoor()
{
    CogRuntime runtime(doorWorld(), CogPreflightTarget::jk);
    const std::int32_t cog = add(runtime,
        script("message activated\nmessage arrived\nthing door0 linkid=0\nflex speed=8.0\nflex wait=2.0\n",
            "activated:\n  if (GetSenderId() != 0) return;\n  if (IsMoving(door0)) return;\n"
            "  if (GetCurFrame(door0) == 0) MoveToFrame(door0, 1, speed);\n  return;\n"
            "arrived:\n  if (GetCurFrame(door0) == 1) { Sleep(wait); MoveToFrame(door0, 0, speed); }\n  return;\n"),
        { { "door0", CogRuntimeValue::ofInt(0) } });
    check(cog == 0, "cog runtime: the door script is cog 0");
    runtime.advance(0.5);
    runtime.sendLinked("activated", 3, 0, 3, 1);
    runtime.advance(0.06);
    const auto& door = runtime.world().things[0];
    check(door.moving && door.position[2] > 0.2 && door.position[2] < 0.8, "cog runtime: activating the door starts it moving");
    runtime.advance(1.0);
    std::vector<CogRuntimeEvent> arrived = ofKind(runtime, "arrived");
    check(arrived.size() == 1 && near(arrived[0].time, 0.625), "cog runtime: the door reaches frame 1 after 1 JKU at 8 JKU/s");
    runtime.advance(2.0);
    arrived = ofKind(runtime, "arrived");
    check(arrived.size() == 2 && near(arrived[1].time, 2.75, 0.03) && runtime.world().things[0].frame == 0,
        "cog runtime: it waits two seconds and closes");
    runtime.sendLinked("activated", 3, 0, 3, 1);
    runtime.advance(0.05);
    runtime.sendLinked("activated", 3, 0, 3, 1);   // ignored while it moves
    runtime.advance(0.2);
    check(ofKind(runtime, "arrived").size() == 3, "cog runtime: a second activation while moving is ignored by the script");
}

void testPairedDoor()
{
    // Two door halves through a symbol array, the way stock doors are
    // written, and a handler named "activate": the engine matches messages
    // by id, and "activate" and "activated" share one.
    CogWorld world = doorWorld();
    sed::assets::CogWorldThing right;
    right.name = "right";
    right.position = { 2, 0, 0 };
    right.frames = { { 2, 0, 0 }, { 2.5, 0, 0 } };
    world.things.push_back(right);
    CogRuntime runtime(world, CogPreflightTarget::jk);
    add(runtime,
        script("message activate\nthing piece0 linkid=0\nthing piece1 linkid=1\nthing piece2 linkid=2\nint i local\nint n local\n",
            "activate:\n  n = 0;\n  for (i = 0; i <= 2; i = i + 1)\n    if (piece0[i] >= 0) { n = n + 1; MoveToFrame(piece0[i], 1, 4); }\n"
            "  PrintInt(n);\n  return;\n"),
        { { "piece0", CogRuntimeValue::ofInt(0) }, { "piece1", CogRuntimeValue::ofInt(2) }, { "piece2", CogRuntimeValue::ofInt(-1) } });
    runtime.sendLinked("activated", 3, 2, 3, 1);
    const auto prints = ofKind(runtime, "print");
    check(prints.size() == 1 && prints[0].text == "2",
        "cog runtime: an 'activate' handler runs for 'activated', and piece0[i] walks the symbols after piece0");
    runtime.advance(0.5);
    check(runtime.world().things[0].frame == 1 && runtime.world().things[2].frame == 1 && ofKind(runtime, "arrived").size() == 2,
        "cog runtime: both halves open; the unbound third is skipped");
}

void testSchedule()
{
    CogRuntime runtime(CogWorld{}, CogPreflightTarget::jk);
    add(runtime, script("message startup\nmessage user0\nmessage timer\nmessage pulse\nint n local\n",
        "startup:\n  SetTimer(0.5);\n  SetPulse(0.25);\n  Sleep(1.0);\n  Print(\"after sleep\");\n  return;\n"
        "user0:\n  Print(\"user0\");\n  return;\n"
        "timer:\n  Print(\"timer\");\n  return;\n"
        "pulse:\n  n = n + 1;\n  if (n == 4) SetPulse(0);\n  return;\n"));
    runtime.startup();
    runtime.advance(0.3);
    runtime.sendMessage(0, "user0");
    runtime.advance(1.5);
    const auto prints = ofKind(runtime, "print");
    check(prints.size() == 3 && prints[0].text == "user0" && prints[1].text == "timer" && near(prints[1].time, 0.5)
            && prints[2].text == "after sleep" && near(prints[2].time, 1.0),
        "cog runtime: a message runs during a sleep, the timer fires on time, and the sleep resumes");
    check(runtime.symbolValue(0, "n")->f == 4.0f, "cog runtime: the pulse runs four times, then stops");

    CogRuntime pair(CogWorld{}, CogPreflightTarget::jk);
    add(pair, script("message startup\ncog other\nflex got local\n",
        "startup:\n  got = SendMessageEx(other, user1, 2, 3, 0, 0);\n  return;\n"),
        { { "other", CogRuntimeValue::ofInt(1) } });
    add(pair, script("message user1\n", "user1:\n  ReturnEx(GetParam(0) * GetParam(1));\n  return;\n"));
    pair.startup();
    check(pair.symbolValue(0, "got")->f == 6.0f, "cog runtime: SendMessageEx returns the other script's ReturnEx");

    CogRuntime wait(doorWorld(), CogPreflightTarget::jk);
    add(wait, script("message startup\nthing door0\n",
        "startup:\n  MoveToFrame(door0, 1, 2);\n  WaitForStop(door0);\n  Print(\"stopped\");\n  return;\n"),
        { { "door0", CogRuntimeValue::ofInt(0) } });
    wait.startup();
    wait.advance(1.0);
    const auto stopped = ofKind(wait, "print");
    check(stopped.size() == 1 && near(stopped[0].time, 0.5), "cog runtime: WaitForStop resumes when the thing arrives");
}

void testUnsimulated()
{
    CogRuntime runtime(doorWorld(), CogPreflightTarget::jk);
    add(runtime, script("message startup\nthing door0\nint t local\n",
        "startup:\n  t = GetThingTemplate(door0);\n  Print(\"carried on\");\n  return;\n"),
        { { "door0", CogRuntimeValue::ofInt(0) } });
    runtime.startup();
    const auto un = ofKind(runtime, "unsimulated");
    check(!un.empty() && un[0].text.find("getthingtemplate(0)") == 0 && ofKind(runtime, "print").size() == 1
            && runtime.symbolValue(0, "t")->i == 0,
        "cog runtime: a verb that isn't simulated is logged, returns 0 and the script carries on");
    check(std::find(sed::assets::cogSimulatedVerbs().begin(), sed::assets::cogSimulatedVerbs().end(), "movetoframe")
            != sed::assets::cogSimulatedVerbs().end(),
        "cog runtime: MoveToFrame is simulated");
}

void testSmallVerbs()
{
    CogWorld world = doorWorld();
    world.things[0].sector = 4;
    world.things[1].sector = 4;
    world.difficulty = 2;
    CogRuntime runtime(world, CogPreflightTarget::jk);
    add(runtime, script("message startup\nthing door0\nkeyframe opening=open.key\nint a local\nint b local\nint d local\nint k local\nint n local\n",
        "startup:\n  a = BitTest(6, 3);\n  d = GetDifficulty();\n  k = PlayKey(door0, opening, 1, 0);\n"
        "  SetThingUserData(door0, 7);\n  n = GetSectorThingCount(4);\n  b = NextThingInSector(FirstThingInSector(4));\n  return;\n"),
        { { "door0", CogRuntimeValue::ofInt(0) } });
    runtime.startup();
    check(runtime.symbolValue(0, "a")->i == 2 && runtime.symbolValue(0, "d")->i == 2,
        "cog runtime: BitTest masks, GetDifficulty reads the world's difficulty");
    check(runtime.symbolValue(0, "k")->i > 0 && runtime.world().things[0].userData == 7.0,
        "cog runtime: PlayKey gives a track and SetThingUserData stores");
    check(runtime.symbolValue(0, "n")->i == 2 && runtime.symbolValue(0, "b")->i == 1,
        "cog runtime: sector thing counts and the first/next walk");
    check(ofKind(runtime, "unsimulated").empty(), "cog runtime: none of these verbs is left unsimulated");
}

CogRuntimeValue text(const std::string& value)
{
    CogRuntimeValue v;
    v.type = CogRuntimeValue::Type::String;
    v.text = value;
    return v;
}

void testBindings()
{
    CogRuntime runtime(doorWorld(), CogPreflightTarget::jk);
    add(runtime, script("flex speed=1.0\nvector offset\nint count\nthing door0\nsound click=beep.wav\n", "startup:\n  return;\n"),
        { { "speed", text("2.5") }, { "offset", text("(1/-2/0.5)") }, { "count", CogRuntimeValue::ofFlex(3.0f) },
            { "door0", text("0") }, { "click", text("other.wav") } });
    const auto speed = runtime.symbolValue(0, "speed");
    check(speed && speed->type == CogRuntimeValue::Type::Flex && speed->f == 2.5f,
        "cog runtime: a placed COG's text value fills a flex symbol as a number");
    const auto offset = runtime.symbolValue(0, "offset");
    check(offset && offset->type == CogRuntimeValue::Type::Vector && offset->v[0] == 1.0f && offset->v[1] == -2.0f
            && offset->v[2] == 0.5f,
        "cog runtime: (x/y/z) fills a vector symbol");
    check(runtime.symbolValue(0, "count")->type == CogRuntimeValue::Type::Int && runtime.symbolValue(0, "count")->i == 3,
        "cog runtime: an int symbol takes a number as an int");
    check(runtime.symbolValue(0, "door0")->i == 0, "cog runtime: a thing symbol takes an index written as text");
    const auto click = runtime.symbolValue(0, "click");
    check(click && click->type == CogRuntimeValue::Type::Int && click->i > 0, "cog runtime: a resource name becomes a handle");
}

void testWorldFromLevel()
{
    Level level;
    Thing door;
    door.name = "door";
    door.templateName = "_structure";
    door.z = 0.5;
    door.sector = 0;
    for (const char* frame : { "(0/0/0.5:0/0/0)", "(0/0/1.5:0/0/0)" })
    {
        TplValue value;
        value.name = "frame";
        value.type = ValueType::frame;
        value.stringValue = frame;
        door.values.push_back(value);
    }
    level.things.push_back(door);
    Thing start;
    start.templateName = "walkplayer";
    level.things.push_back(start);
    Sector sector;
    sector.vertices = { Vertex{ 0, 0, 0, 0 }, Vertex{ 2, 0, 0, 0 }, Vertex{ 2, 2, 0, 0 } };
    Surface surface;
    surface.vertices = { 0, 1, 2 };
    sector.surfaces.push_back(surface);
    level.sectors.push_back(sector);
    const CogWorld world = sed::assets::cogWorldFromLevel(level);
    check(world.things.size() == 2 && world.things[0].frames.size() == 2 && world.things[0].frames[1][2] == 1.5
            && world.player == 1,
        "cog runtime: things, path frames and the player come from the level");
    check(world.surfaces.size() == 1 && near(world.surfaces[0].center[0], 4.0 / 3.0, 1e-9),
        "cog runtime: surfaces are numbered sector by sector, with their centres");
}

} // namespace

void runCogRuntimeTests()
{
    testMachine();
    testDoor();
    testPairedDoor();
    testSchedule();
    testUnsimulated();
    testBindings();
    testSmallVerbs();
    testWorldFromLevel();
}

} // namespace sed::tests
