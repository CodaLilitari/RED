#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "sed/assets/cog_script.hpp"
#include "sed/assets/episode.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/gameplay_assistants.hpp"
#include "sed/level/thing.hpp"
#include "ui/assistants/gameplay_assistant_dialog.hpp"
#include "../../core/src/assets/atomic_file_write.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QSpinBox>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <filesystem>
#include <cstdlib>
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

void opposingDoorAction()
{
    QTemporaryDir scratch;
    if ( !scratch.isValid() ) { check(false, "paired door fixture has temporary storage"); return; }
    const std::filesystem::path root(scratch.path().toStdWString());
    std::filesystem::create_directory(root / "cog");
    std::ofstream(root / "cog" / "00_door.cog", std::ios::binary) << kDoorScript;
    MainWindow window;
    editor::DocumentState state;
    state.level = fixture();
    state.selection.things = { 2, 1 };
    const Level before = state.level;
    window.setDocumentStateForTest(std::move(state));
    const auto levelPath = root / "fixture.sed";
    window.setCurrentPathForTitleForTest(levelPath);
    window.setResourcePathsForTest({}, {}, levelPath);
    auto* action = window.findChild<QAction*>("assistant.door");
    if ( !action ) { check(false, "paired door fixture finds the Door Assistant action"); return; }
    bool reviewed = false;
    QTimer drive;
    QObject::connect(&drive, &QTimer::timeout, &window, [&] {
        if ( auto* dialog = dynamic_cast<GameplayAssistantDialog*>(QApplication::activeModalWidget()) )
        {
            auto* opposite = dialog->findChild<QCheckBox*>("opposingLeaves");
            auto* preview = dialog->findChild<QLabel*>("doorOpenPoses");
            auto* x = dialog->findChild<QDoubleSpinBox*>("offset.x");
            auto* z = dialog->findChild<QDoubleSpinBox*>("offset.z");
            if ( !opposite || !preview || !x || !z ) { dialog->reject(); return; }
            check(opposite->isEnabled() && !opposite->isChecked(), "opposed leaves are available but opt-in for two pieces");
            opposite->setChecked(true);
            check(x->value() == -0.5 && z->value() == 0.0,
                "the initial opposing offset moves the two leaves away from one another");
            opposite->setChecked(false);
            check(x->value() == 0.0 && z->value() > 0.0, "returning to shared motion restores its own offset");
            opposite->setChecked(true);
            x->setValue(-0.25);
            z->setValue(0);
            reviewed = preview->isVisible() && preview->text().contains("Thing 1")
                && preview->text().contains("(0/0.5/0.5:0/0/0)") && preview->text().contains("Thing 2")
                && preview->text().contains("(1/0.5/0.5:0/0/0)") && dialog->tryIt().passed
                && assets::sedLevelsEqual(window.mutableDocumentStateForTest().level, before);
            dialog->accept();
        }
        else if ( auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) )
        {
            message->accept();
        }
    });
    drive.start(10);
    action->trigger();
    drive.stop();
    const Level committed = window.mutableDocumentStateForTest().level;
    check(reviewed && committed.cogs.size() == 1 && level::thingCountFrames(committed.things[1]) == 2
            && level::thingCountFrames(committed.things[2]) == 2, "the ordinary Door Assistant previews both exact open poses and applies the pair");
    check(window.undoDocumentForTest() && assets::sedLevelsEqual(window.mutableDocumentStateForTest().level, before)
            && !window.undoDocumentForTest(), "one Undo restores both leaves and removes the shared COG");
    check(window.redoDocumentForTest() && assets::sedLevelsEqual(window.mutableDocumentStateForTest().level, committed),
        "one Redo restores the reviewed opposing leaves and COG");

    editor::Selection one;
    one.things = { 1 };
    GameplayAssistantDialog single(level::AssistantKind::door, before, one, nullptr, {}, kDoorScript);
    const auto* unavailable = single.findChild<QCheckBox*>("opposingLeaves");
    check(unavailable && !unavailable->isEnabled(), "the assistant requires exactly two selected leaves for mirrored motion");
}

void panelSwitchAction()
{
    QTemporaryDir scratch;
    if ( !scratch.isValid() ) { check(false, "panel switch fixture has temporary storage"); return; }
    const auto samples = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
        / "tools" / "engine-capture" / "fixtures" / "panel-switch";
    const std::filesystem::path root(scratch.path().toStdWString());
    std::filesystem::create_directory(root/"3do");
    std::filesystem::create_directory(root/"cog");
    std::filesystem::copy_file(samples/"panelwall.3do", root/"3do"/"panelwall.3do");
    std::filesystem::copy_file(samples/"paneluse.cog", root/"cog"/"paneluse.cog");
    std::ifstream in(samples/"paneluse.cog");
    const std::string text(std::istreambuf_iterator<char>(in), {});
    editor::DocumentState state;
    state.level = fixture();
    ThingTemplate wall;
    wall.name = "panelwall"; wall.parent = "none";
    wall.values = { deserializeTplValue("type","cog"), deserializeTplValue("collide","3"),
        deserializeTplValue("model3d","panelwall.3do") };
    state.level.templates.push_back(wall);
    state.level.things[1].templateName = wall.name;
    auto& cog = state.level.newCog();
    cog.name = "paneluse.cog";
    cog.values = assets::loadCogSymbolsFromTextResult(text,false).symbols;
    state.selection.things = {1};
    const Level before = state.level;
    MainWindow window;
    window.setDocumentStateForTest(std::move(state));
    const auto levelPath = root/"fixture.sed";
    window.setCurrentPathForTitleForTest(levelPath);
    window.setResourcePathsForTest({root}, {}, levelPath);
    auto* action = window.findChild<QAction*>("assistant.panelSwitch");
    if ( !action ) { check(false, "3DO Panel Switch has an ordinary assistant menu action"); return; }
    bool previewed = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        if ( auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); dialog && dialog->objectName() == "panelSwitch" )
        {
            auto* face = dialog->findChild<QSpinBox*>("panelFace");
            auto* view = dialog->findChild<QGraphicsView*>("panelPreview");
            auto* target = dialog->findChild<QComboBox*>("panelTarget");
            if ( !face || !view || !target ) { dialog->reject(); return; }
            face->setValue(2);
            previewed = view->scene() && view->scene()->items().size() == 2 && target->currentText().contains("button")
                && assets::sedLevelsEqual(before,window.mutableDocumentStateForTest().level);
            dialog->accept();
        }
        else if ( auto* msg = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) ) msg->accept();
    });
    timer.start(10); action->trigger(); timer.stop();
    const Level after = window.mutableDocumentStateForTest().level;
    check(previewed && after.things.size() == before.things.size()+1
            && after.cogs[0].values[0].asJedString() == std::to_string(before.things.size()),
        "the GUI previews the selected face and button sphere and binds the new Thing");
    check(window.undoDocumentForTest() && assets::sedLevelsEqual(before,window.mutableDocumentStateForTest().level)
            && window.redoDocumentForTest() && assets::sedLevelsEqual(after,window.mutableDocumentStateForTest().level),
        "one Undo and Redo restores the exact native wall, template, button and script binding");
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
    const level::AssistantCheck tried = dialog.tryIt();
    const QString report = dialog.resultText();
    check(report.contains(QStringLiteral(" s  verb  PlayKey(")) && !report.contains(QStringLiteral(" s  error  ")),
        "pursuit dialog: Try it executes the parked animation at startup without runtime errors");
    check(!tried.passed && !tried.unsimulated.empty() && report.contains(QStringLiteral("requires a target check")),
        "pursuit dialog: Try it identifies the pursuit operations that require a target check");
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

bool saveCampaignWithStartup(MainWindow& window, bool& reported, const std::function<void()>& onReport = {})
{
    auto* action = window.findChild<QAction*>(QStringLiteral("edit.episode_campaign_editor"));
    check(action != nullptr, "the campaign opens through its ordinary menu action");
    if ( !action )
    {
        return false;
    }
    bool accepted = false;
    QTimer responder;
    QObject::connect(&responder,
                     &QTimer::timeout,
                     [&]
                     {
                         auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                         if ( !dialog )
                         {
                             return;
                         }
                         if ( auto* box = qobject_cast<QMessageBox*>(dialog) )
                         {
                             reported = true;
                             if ( onReport )
                             {
                                 onReport();
                             }
                             box->accept();
                         }
                         else if ( dialog->objectName() == QStringLiteral("episodeCampaignEditorDialog") && !accepted )
                         {
                             auto* generate =
                                 dialog->findChild<QCheckBox*>(QStringLiteral("episodeGenerateStartupCogCheck"));
                             auto* buttons =
                                 dialog->findChild<QDialogButtonBox*>(QStringLiteral("episodeCampaignButtons"));
                             auto* title = dialog->findChild<QLineEdit*>(QStringLiteral("episodeNameEdit"));
                             if ( !generate || !buttons || !title )
                             {
                                 dialog->reject();
                                 return;
                             }
                             title->setText(QStringLiteral("Valley after the evacuation"));
                             generate->setChecked(true);
                             accepted = true;
                             buttons->button(QDialogButtonBox::Save)->click();
                         }
                         else
                         {
                             dialog->reject();
                         }
                     });
    responder.start(5);
    action->trigger();
    return accepted;
}

constexpr int interruptedWriterExit = 83;

void campaignPublicationProcess(const QString& role, const std::filesystem::path& root)
{
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(root / "recovery");
    const auto level = root / "valley.sed";
    if ( role == QStringLiteral("writer") )
    {
        window.createNewProjectForTest(ProjectType::JKDF2);
        window.setCurrentPathForTitleForTest(level);
        window.setResourcePathsForTest({}, {}, level);
        assets::saveToSED(window.documentStateForTest().level, level);
        std::ofstream(root / "cogstrings.uni", std::ios::binary)
            << "MSGS 1\n\"CONVOY_BRIEF\" 17 \"Three transports remain\"\nEND\n";
        assets::detail::ScopedStagedAssetFileFaultForTests interrupted(
            assets::detail::StagedAssetFileFaultPointForTests::publish, 1);
        bool reported = false;
        saveCampaignWithStartup(window,
                                reported,
                                [&]
                                {
                                    if ( interrupted.triggered() )
                                    {
                                        std::_Exit(interruptedWriterExit);
                                    }
                                });
        check(false, "the publisher must terminate after the first campaign artifact becomes visible");
        return;
    }
    window.openLevelFromFilenameForTest(QString::fromStdWString(level.wstring()));
    check(window.currentPathForTest() == level && window.documentStateForTest().level.cogs.empty() &&
              std::filesystem::is_regular_file(root / "episode.jk") &&
              std::filesystem::is_regular_file(root / "misc/cogstrings.uni") &&
              std::filesystem::is_regular_file(root / "valley_start.cog"),
          "a fresh editor process recovers all campaign files from disk and preserves the last saved level");
    if ( std::filesystem::is_regular_file(root / "episode.jk")
        && std::filesystem::is_regular_file(root / "misc/cogstrings.uni") )
    {
        const auto episode = assets::loadFromEpisodeJk(root / "episode.jk");
        const auto strings = assets::loadFromCogStringsUni(root / "misc/cogstrings.uni");
        const auto* brief = strings.find("CONVOY_BRIEF");
        check(episode.name == "Valley after the evacuation" && brief && *brief == "Three transports remain"
                && strings.entries.size() == 1 && strings.entries.front().unusedNumber == 17
                && !std::filesystem::exists(root / "cogstrings.uni"),
            "the fresh reader restores the unpublished dialog title and exact legacy string with its numeric field");
    }
}

void campaignRecoveryAfterProcessExit()
{
    QTemporaryDir directory;
    if ( !directory.isValid() )
    {
        check(false, "process recovery has a temporary project");
        return;
    }
    const std::filesystem::path root(directory.path().toStdWString());
    const auto child = [&](const QString& role, int expected)
    {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("RED_TEST_CAMPAIGN_PROCESS"), role);
        environment.insert(QStringLiteral("RED_TEST_CAMPAIGN_DIRECTORY"), directory.path());
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--gameplay-assistant-dialog")});
        const bool ok = process.waitForStarted(10000) && process.waitForFinished(45000) &&
                        process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected;
        if ( !ok )
        {
            process.kill();
            process.waitForFinished(5000);
            std::cerr << process.readAllStandardError().toStdString();
        }
        return ok;
    };
    const bool interrupted = child(QStringLiteral("writer"), interruptedWriterExit);
    check(interrupted && std::filesystem::is_regular_file(root / "valley_start.cog") &&
              !std::filesystem::exists(root / "episode.jk") && !std::filesystem::exists(root / "misc/cogstrings.uni"),
          "the terminated writer leaves exactly the approved startup COG public before restart");
    if ( interrupted )
    {
        check(child(QStringLiteral("reader"), 0),
              "a newly launched native test process can reopen and finish the interrupted project");
    }
}

void campaignPreservesConflictingStartupScript()
{
    QTemporaryDir directory;
    if ( !directory.isValid() )
    {
        check(false, "campaign conflict has an isolated project directory");
        return;
    }
    const std::filesystem::path root(directory.path().toStdWString());
    const auto script = root / "valley_start.cog";
    const std::string userScript = "# existing designer script\n";
    std::ofstream(script, std::ios::binary) << userScript;
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(root / "recovery");
    window.createNewProjectForTest(ProjectType::JKDF2);
    window.setCurrentPathForTitleForTest(root / "valley.sed");
    const auto depth = window.undoDepthForTest();
    bool reported = false;
    const bool accepted = saveCampaignWithStartup(window, reported);
    std::ifstream in(script, std::ios::binary);
    const std::string retained((std::istreambuf_iterator<char>(in)), {});
    check(accepted && reported && retained == userScript && !std::filesystem::exists(root / "episode.jk") &&
              !std::filesystem::exists(root / "misc/cogstrings.uni") &&
              window.documentStateForTest().level.cogs.empty() && window.undoDepthForTest() == depth,
          "F8 refuses conflicting startup COG before changing any campaign file, level placement or history");
}

void campaignRecoversInterruptedPublication()
{
    QTemporaryDir directory;
    if ( !directory.isValid() )
    {
        check(false, "campaign recovery has an isolated project directory");
        return;
    }
    const std::filesystem::path root(directory.path().toStdWString());
    const auto level = root / "valley.sed";
    const auto script = root / "valley_start.cog";
    const auto recovery = root / "recovery";
    {
        MainWindow window;
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.setRecoveryDirectoryForTest(recovery);
        window.createNewProjectForTest(ProjectType::JKDF2);
        window.setCurrentPathForTitleForTest(level);
        window.setResourcePathsForTest({}, {}, level);
        assets::saveToSED(window.documentStateForTest().level, level);
        bool reported = false;
        assets::detail::ScopedStagedAssetFileFaultForTests secondFile(
            assets::detail::StagedAssetFileFaultPointForTests::publish, 1);
        const bool accepted = saveCampaignWithStartup(window, reported);
        check(accepted && reported && secondFile.triggered() && std::filesystem::exists(script) &&
                  !std::filesystem::exists(root / "episode.jk") && window.documentStateForTest().level.cogs.empty(),
              "an interruption after the generated startup COG retains a recoverable campaign without placing a "
              "dangling COG");
        if ( !secondFile.triggered() )
        {
            return;
        }
    }
    MainWindow reopened;
    reopened.setAttribute(Qt::WA_DontShowOnScreen);
    reopened.setRecoveryDirectoryForTest(recovery);
    const auto read = [](const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto reviewedScript = read(script);
    std::ofstream(script, std::ios::binary | std::ios::trunc) << "# designer edited while RED was closed";
    bool conflict = false;
    QTimer warning;
    QObject::connect(&warning,
                     &QTimer::timeout,
                     [&]
                     {
                         if ( auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) )
                         {
                             conflict = true;
                             box->accept();
                         }
                     });
    warning.start(5);
    reopened.openLevelFromFilenameForTest(QString::fromStdWString(level.wstring()));
    check(conflict && reopened.currentPathForTest() != level &&
              read(script) == "# designer edited while RED was closed" &&
              !std::filesystem::exists(root / "misc/cogstrings.uni") && !std::filesystem::exists(root / "episode.jk"),
          "ordinary reopen preserves an externally changed interrupted file and refuses further campaign publication");
    std::ofstream(script, std::ios::binary | std::ios::trunc) << reviewedScript;
    reopened.openLevelFromFilenameForTest(QString::fromStdWString(level.wstring()));
    warning.stop();
    check(reopened.currentPathForTest() == level && std::filesystem::is_regular_file(root / "episode.jk") &&
              std::filesystem::is_regular_file(root / "misc/cogstrings.uni") &&
              std::filesystem::is_regular_file(script) && reopened.documentStateForTest().level.cogs.empty(),
          "ordinary native reopen completes the approved campaign files and keeps the previously saved level and "
          "history");
    check(assets::loadFromEpisodeJk(root / "episode.jk").sequences.at(0).fileName == "valley.jkl" &&
              assets::loadFromCogStringsUni(root / "misc/cogstrings.uni").entries.empty() &&
              read(script) == reviewedScript,
          "recovered campaign contents reopen in both game parsers and keep the exact reviewed startup script");
}

void objectivesProjectPublication()
{
    enum class Case { absent, equal, conflict, blockedFolder, publishFailure, shadowed, changedProject };
    const std::string name(level::kObjectivesAssistantScript);
    const std::string reviewed(level::assistantScriptText(name));
    for ( const Case scenario : { Case::absent, Case::equal, Case::conflict, Case::blockedFolder,
             Case::publishFailure, Case::shadowed, Case::changedProject } )
    {
        QTemporaryDir scratch;
        if ( !scratch.isValid() ) { check(false, "assistant publication has temporary storage"); return; }
        const std::filesystem::path root(scratch.path().toStdWString());
        const auto script = root / "cog" / name;
        if ( scenario == Case::equal || scenario == Case::conflict )
        {
            std::filesystem::create_directories(script.parent_path());
            std::ofstream(script, std::ios::binary) << (scenario == Case::equal ? reviewed : "# existing user script");
        }
        if ( scenario == Case::blockedFolder ) std::ofstream(root / "cog") << "occupied";
        if ( scenario == Case::shadowed ) std::ofstream(root / name) << "# higher priority";

        MainWindow window;
        editor::DocumentState state;
        state.level = fixture();
        state.selection.sectors = { 1 };
        window.setDocumentStateForTest(std::move(state));
        const auto levelPath = root / "fixture.sed";
        window.setCurrentPathForTitleForTest(levelPath);
        window.setResourcePathsForTest({}, {}, levelPath);
        QAction* action = window.findChild<QAction*>(QStringLiteral("assistant.objectives"));
        if ( !action ) { check(false, "project publication uses the Objectives menu action"); return; }
        bool accepted = false;
        QString failure;
        QTimer drive;
        QObject::connect(&drive, &QTimer::timeout, &window, [&] {
            if ( auto* dialog = dynamic_cast<GameplayAssistantDialog*>(QApplication::activeModalWidget()) )
            {
                accepted = true;
                if ( scenario == Case::changedProject ) window.setCurrentPathForTitleForTest(root / "other" / "new.sed");
                dialog->accept();
            }
            else if ( auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) )
            {
                failure = box->text();
                box->accept();
            }
        });
        drive.start(10);
        assets::detail::ScopedStagedAssetFileFaultForTests fault(scenario == Case::publishFailure
            ? assets::detail::StagedAssetFileFaultPointForTests::publish
            : assets::detail::StagedAssetFileFaultPointForTests::none);
        action->trigger();
        drive.stop();
        const bool success = scenario == Case::absent || scenario == Case::equal;
        const auto read = [](const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        check(accepted && (success == failure.isEmpty()), "Objectives action reports resource publication failure before accepting the edit");
        check(window.mutableDocumentStateForTest().level.cogs.size() == (success ? 1u : 0u),
            "Objectives places exactly one COG only after verifying its effective script");
        if ( success )
        {
            check(read(script) == reviewed, "Objectives publishes or reuses the reviewed script bytes");
            std::ofstream(script, std::ios::binary) << "# edited after placement";
            check(window.undoDocumentForTest() && window.mutableDocumentStateForTest().level.cogs.empty()
                    && read(script) == "# edited after placement" && !window.undoDocumentForTest(),
                "one Undo removes the COG placement and preserves the user's later edit");
            check(window.redoDocumentForTest() && window.mutableDocumentStateForTest().level.cogs.size() == 1
                    && read(script) == "# edited after placement" && !window.redoDocumentForTest(),
                "one Redo restores the placement and preserves the current project script");
        }
        else
        {
            check(!window.undoDocumentForTest() && window.mutableDocumentStateForTest().selection.sectors.contains(1),
                "refusing a project script preserves selection and all history");
            if ( scenario == Case::conflict ) check(read(script) == "# existing user script", "conflict preserves the user's bytes");
            if ( scenario == Case::blockedFolder ) check(read(root / "cog") == "occupied", "unwritable destination preserves its owner");
            if ( scenario == Case::publishFailure ) check(fault.triggered() && !std::filesystem::exists(script), "failed publish leaves no script");
            if ( scenario == Case::shadowed ) check(read(root / name) == "# higher priority", "shadowing script is preserved");
            if ( scenario == Case::changedProject )
                check(!std::filesystem::exists(script) && !std::filesystem::exists(root / "other"),
                    "changing the project during review writes to neither destination");
        }
    }
}

} // namespace

void runGameplayAssistantDialogTests()
{
    const auto role = qEnvironmentVariable("RED_TEST_CAMPAIGN_PROCESS");
    if (role == QStringLiteral("writer") || role == QStringLiteral("reader"))
    {
        campaignPublicationProcess(role, std::filesystem::path(qEnvironmentVariable("RED_TEST_CAMPAIGN_DIRECTORY").toStdWString()));
        return;
    }
    campaignRecoveryAfterProcessExit();
    campaignPreservesConflictingStartupScript();
    campaignRecoversInterruptedPublication();
    doorFromSelection();
    opposingDoorAction();
    panelSwitchAction();
    objectivesFromSelection();
    pursuitFromSelection();
    menuHasTheAssistants();
    objectivesProjectPublication();
}

} // namespace sed::app_tests
