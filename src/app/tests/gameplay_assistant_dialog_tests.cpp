#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "sed/assets/cog_script.hpp"
#include "sed/level/gameplay_assistants.hpp"
#include "sed/level/thing.hpp"
#include "ui/assistants/gameplay_assistant_dialog.hpp"

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMenu>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace sed::app_tests
{
namespace
{

using ui::assistants::GameplayAssistantDialog;

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


void doorFromSelection()
{
    const Level level = fixture();
    editor::Selection selection;
    selection.things = { 1, 2 };
    GameplayAssistantDialog dialog(level::AssistantKind::door, level, selection, nullptr,
        assets::loadCogSymbolsFromTextResult(kDoorScript, false).symbols, kDoorScript);
    check(dialog.selectionProblem().isEmpty(), "door assistant dialog: two selected Things are its pieces");
    auto* z = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("offset.z"));
    check(z != nullptr, "door assistant dialog: has the opening offset");
    if (z) z->setValue(0.9);
    const level::AssistantCheck tried = dialog.tryIt();
    check(tried.passed && dialog.resultText().contains(QStringLiteral("It works in the COG runtime"))
            && dialog.resultText().contains(QStringLiteral("Timeline:")),
        "door assistant dialog: Try it runs the door in the runtime and shows the timeline");
    check(level.cogs.empty() && level::thingCountFrames(level.things[1]) == 0, "door assistant dialog: Try it leaves the level unchanged");
    Level copy = level;
    const auto result = dialog.applyTo(copy);
    check(result.ok() && copy.cogs.size() == 1 && level::thingCountFrames(copy.things[2]) == 2,
        "door assistant dialog: applying makes the door");

    editor::Selection none;
    GameplayAssistantDialog empty(level::AssistantKind::door, level, none, nullptr, {}, kDoorScript);
    check(!empty.selectionProblem().isEmpty(), "door assistant dialog: an empty selection is turned away before the dialog opens");
}

void objectivesFromSelection()
{
    const Level level = fixture();
    editor::Selection selection;
    selection.sectors = { 1 };
    selection.things = { 3 };
    selection.surfaces.insert(SurfaceRef{ 1, 0 });
    const std::string text(level::assistantScriptText(level::kObjectivesAssistantScript));
    GameplayAssistantDialog dialog(level::AssistantKind::objectives, level, selection, nullptr,
        assets::loadCogSymbolsFromTextResult(text, false).symbols, text);
    auto* trigger = dialog.findChild<QComboBox*>(QStringLiteral("goal1"));
    check(trigger != nullptr, "objectives assistant dialog: the selected Thing is goal 1 with a trigger to choose");
    if (trigger) trigger->setCurrentIndex(1);
    const level::AssistantCheck tried = dialog.tryIt();
    check(tried.passed && dialog.resultText().contains(QStringLiteral("goal 1 is completed by killing Thing 3"))
            && dialog.resultText().contains(QStringLiteral("crossing the exit then ends the level")),
        "objectives assistant dialog: reach a sector, kill a Thing, then the selected adjoin is the exit");
}

void pursuitFromSelection()
{
    std::ifstream in(std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "data" / "cog_templates" / "kcswoop.cog",
        std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const Level level = fixture();
    editor::Selection selection;
    selection.things = { 3, 1 };
    selection.sectors = { 1 };
    GameplayAssistantDialog dialog(level::AssistantKind::pursuit, level, selection, nullptr,
        assets::loadCogSymbolsFromTextResult(text, false).symbols, text);
    check(!text.empty() && dialog.selectionProblem().isEmpty(), "pursuit dialog: the selected Things are the enemy and its route");
    check(dialog.tryIt().passed, "pursuit dialog: Try it starts the controller without errors");
    Level copy = level;
    const auto result = dialog.applyTo(copy);
    check(result.ok() && copy.cogs.size() == 1 && copy.cogs[0].name == "kcswoop.cog",
        "pursuit dialog: applying places Lily's kcswoop.cog");
}

void menuHasTheAssistants()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    bool all = window.findChild<QMenu*>(QStringLiteral("thing.assistants")) != nullptr;
    for (const char* name : { "assistant.door", "assistant.elevator", "assistant.switch", "assistant.objectives", "assistant.cutscene",
             "assistant.pursuit" })
        all = all && window.findChild<QAction*>(QString::fromLatin1(name)) != nullptr;
    check(all, "Thing > Assistants has Door, Elevator, Switch, Objectives and Cutscene");
}

} // namespace

void runGameplayAssistantDialogTests()
{
    doorFromSelection();
    objectivesFromSelection();
    pursuitFromSelection();
    menuHasTheAssistants();
}

} // namespace sed::app_tests
