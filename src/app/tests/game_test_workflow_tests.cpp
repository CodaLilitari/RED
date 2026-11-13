#include "test_common.hpp"

#include "app_settings.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/assets/static_resources.hpp"
#include "ui/project/game_test_workflow.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDirIterator>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QTextDocument>
#include <QToolButton>

namespace sed::app_tests
{
void runGameTestWorkflowTests()
{
    QTemporaryDir scratch;
    check(scratch.isValid(), "game test: isolated scratch exists");
    if ( !scratch.isValid() ) return;
    const auto root = std::filesystem::path(scratch.path().toStdWString());
    std::filesystem::create_directories(root / "game" / "resource");
    std::filesystem::create_directories(root / "tests");
    for ( const auto& path : { root / "game" / "engine.exe", root / "game" / "resource" / "jk_.cd" } )
    {
        QFile file(QString::fromStdWString(path.wstring()));
        check(file.open(QIODevice::WriteOnly), "game test: synthetic runtime marker created");
        file.write("synthetic data; not executable");
    }
    auto settings = settingsStore();
    settings.remove(QStringLiteral("game_test"));
    for ( const auto kind : { ProjectType::JKDF2, ProjectType::MOTS } )
    {
        for ( const std::string suffix : { ".sed", ".jed", "" } )
        {
            MainWindow window;
            window.setAttribute(Qt::WA_DontShowOnScreen);
            editor::DocumentState state;
            state.level.kind = kind;
            state.level.header.version = 1;
            state.level.header.gravity = 4.0;
            state.level.uvCoordinateSpace = UvCoordinateSpace::TexturePixels;
            window.setDocumentStateForTest(std::move(state));
            const auto source = suffix.empty() ? std::filesystem::path{} : root / ("source" + suffix);
            window.setCurrentPathForTitleForTest(source);
            window.pushUndoForTitleForTest();
            const auto title = window.windowTitle();
            const auto undo = window.undoDepthForTest();
            QAction* action = window.findChild<QAction*>(QStringLiteral("file.save_jkl_test"));
            check(action != nullptr, "game test: File exposes Save JKL & Test for JK and MotS");
            if ( !action ) continue;
            bool dialogSeen = false;
            bool profilesExact = false;
            QTimer driver;
            driver.setInterval(5);
            QObject::connect(&driver, &QTimer::timeout, [&]()
            {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if ( !dialog ) return;
                if ( dialog->objectName() == QStringLiteral("gameTestDialog") )
                {
                    dialogSeen = true;
                    const auto* engines = dialog->findChild<QComboBox*>(QStringLiteral("gameTestEngine"));
                    profilesExact = engines && engines->count() == 2
                        && engines->itemData(0).toString() == QStringLiteral("OpenJKDF2")
                        && engines->itemData(1).toString() == QStringLiteral("Retail");
                }
                dialog->reject();
            });
            driver.start();
            action->trigger();
            driver.stop();
            check(dialogSeen && profilesExact, "game test: explicit OpenJKDF2 and Retail profiles are selectable");
            check(window.currentPathForTest() == source && window.windowTitle() == title
                    && window.undoDepthForTest() == undo,
                "game test: Cancel preserves native/legacy/Untitled ownership, dirty title and history");
            check(!settings.contains(QStringLiteral("game_test/jkdf2/engine"))
                    && !settings.contains(QStringLiteral("game_test/mots/engine")),
                "game test: Cancel does not persist launch configuration");
        }
    }

    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    editor::DocumentState state;
    state.level.kind = ProjectType::JKDF2;
    state.level.header.version = 1;
    state.level.header.gravity = 6.25;
    state.level.uvCoordinateSpace = UvCoordinateSpace::TexturePixels;
    // The production exporter includes canonical engine templates from the
    // editor database. Shadow them with resource-free synthetic definitions so
    // this process-boundary test needs no licensed game data.
    for ( const auto name : assets::staticTemplates(ProjectType::JKDF2) )
    {
        ThingTemplate fixtureTemplate;
        fixtureTemplate.name = name;
        fixtureTemplate.parent = "none";
        state.level.templates.push_back(std::move(fixtureTemplate));
    }
    window.setDocumentStateForTest(std::move(state));
    const auto native = root / "editable.sed";
    window.setCurrentPathForTitleForTest(native);
    window.pushUndoForTitleForTest();
    const auto undo = window.undoDepthForTest();
    const auto title = window.windowTitle();
    QAction* action = window.findChild<QAction*>(QStringLiteral("file.save_jkl_test"));
    check(action != nullptr, "game test: launch-failure case can invoke real File action");
    if ( action )
    {
        bool submitted = false;
        bool processFailureSeen = false;
        QString observedFailure;
        QTimer driver;
        driver.setInterval(5);
        QObject::connect(&driver, &QTimer::timeout, [&]()
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if ( !dialog ) return;
            if ( dialog->objectName() == QStringLiteral("gameTestDialog") && !submitted )
            {
                const auto set = [&](const char* name, const QString& value)
                {
                    if ( auto* edit = dialog->findChild<QLineEdit*>(QString::fromLatin1(name)) ) edit->setText(value);
                };
                set("gameTestGameDirectory", QString::fromStdWString((root / "game").wstring()));
                set("gameTestExecutable", QString::fromStdWString((root / "game" / "engine.exe").wstring()));
                set("gameTestStageRoot", QString::fromStdWString((root / "tests").wstring()));
                set("gameTestLevelName", QStringLiteral("current.jkl"));
                if ( auto* button = dialog->findChild<QPushButton*>(QStringLiteral("gameTestRun")) )
                {
                    submitted = true;
                    button->click();
                }
                else dialog->reject();
            }
            else if ( auto* message = qobject_cast<QMessageBox*>(dialog) )
            {
                observedFailure = message->text();
                processFailureSeen = message->text().contains(QStringLiteral("start"), Qt::CaseInsensitive);
                message->accept();
            }
        });
        driver.start();
        action->trigger();
        driver.stop();
        if ( !processFailureSeen )
            std::cerr << "game test launch-failure diagnostic: " << observedFailure.toStdString() << '\n';
        check(submitted && processFailureSeen, "game test: failed native process creation is reported");
        QDirIterator files(QString::fromStdWString((root / "tests").wstring()),
            QStringList{ QStringLiteral("current.jkl") }, QDir::Files, QDirIterator::Subdirectories);
        bool freshMap = false;
        if ( files.hasNext() )
        {
            const auto map = assets::loadFromJKL(std::filesystem::path(files.next().toStdWString()));
            freshMap = map.header.gravity == 6.25;
        }
        check(freshMap, "game test: stages the current unsaved map before attempting process creation");
        check(window.currentPathForTest() == native && window.windowTitle() == title
                && window.undoDepthForTest() == undo,
            "game test: export and launch failure preserve editable source, dirty state and history");
        check(!std::filesystem::exists(root / "game" / "episode"),
            "game test: staging never writes into the game episode directory");
    }
    settings.remove(QStringLiteral("game_test"));

    // Inspect physical staging names before process creation. An exists() check
    // on Windows would also accept the wrong case and conceal this regression.
    {
        ui::project::GameTestContext stagedContext;
        stagedContext.game = ProjectType::JKDF2;
        const std::string materialBytes = "synthetic staged material payload";
        const auto gob = assets::buildGobBytes({ { "mat/Tile.MAT", materialBytes } });
        const auto* data = reinterpret_cast<const std::byte*>(gob.data());
        auto resources = std::make_shared<assets::ResourceLocator>();
        resources->mountGob(assets::GobArchive::fromBytes({ data, data + gob.size() }));
        stagedContext.resources = resources;
        std::filesystem::path stagedDirectory;
        const QByteArray levelBytes("SECTION: MATERIALS\nWorld Materials 1\nTile.MAT\nEND\n");
        stagedContext.exportMap = [&](const std::filesystem::path& destination)
        {
            stagedDirectory = destination.parent_path().parent_path();
            QFile file(QString::fromStdWString(destination.wstring()));
            return file.open(QIODevice::WriteOnly) && file.write(levelBytes) == levelBytes.size();
        };
        bool inspectedStage = false;
        bool canonicalPhysicalName = false;
        bool exactPayloads = false;
        stagedContext.snapshotStillCurrent = [&]()
        {
            const auto materialDirectory = stagedDirectory / "mat";
            if ( !std::filesystem::is_directory(materialDirectory) ) return true;
            inspectedStage = true;
            for ( const auto& entry : std::filesystem::directory_iterator(materialDirectory) )
            {
                canonicalPhysicalName = entry.path().filename().string() == "tile.mat";
                QFile material(QString::fromStdWString(entry.path().wstring()));
                QFile map(QString::fromStdWString((stagedDirectory / "jkl" / "staged.jkl").wstring()));
                exactPayloads = material.open(QIODevice::ReadOnly) && map.open(QIODevice::ReadOnly)
                    && material.readAll() == QByteArray::fromStdString(materialBytes) && map.readAll() == levelBytes;
            }
            return false; // Explicitly refuse after inspection; never create a process.
        };
        bool submittedStage = false;
        QTimer stageDriver;
        stageDriver.setInterval(5);
        QObject::connect(&stageDriver, &QTimer::timeout, [&]()
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if ( !dialog || dialog->objectName() != QStringLiteral("gameTestDialog") || submittedStage ) return;
            const auto set = [&](const char* name, const QString& value)
            {
                if ( auto* edit = dialog->findChild<QLineEdit*>(QString::fromLatin1(name)) ) edit->setText(value);
            };
            set("gameTestGameDirectory", QString::fromStdWString((root / "game").wstring()));
            set("gameTestExecutable", QString::fromStdWString((root / "game" / "engine.exe").wstring()));
            set("gameTestStageRoot", QString::fromStdWString((root / "tests").wstring()));
            set("gameTestLevelName", QStringLiteral("staged.jkl"));
            if ( auto* button = dialog->findChild<QPushButton*>(QStringLiteral("gameTestRun")) )
            {
                submittedStage = true;
                button->click();
            }
            else dialog->reject();
        });
        stageDriver.start();
        bool refusedLaunch = false;
        try { (void)ui::project::saveJklAndTest(nullptr, stagedContext); }
        catch ( const std::exception& ) { refusedLaunch = true; }
        stageDriver.stop();
        check(submittedStage && inspectedStage && canonicalPhysicalName && exactPayloads,
            "game test: staging uses canonical physical names and preserves authored map/resource bytes");
        check(refusedLaunch && !stagedDirectory.empty() && !std::filesystem::exists(stagedDirectory),
            "game test: naming inspection cancels before process creation and removes its owned stage");
        settings.remove(QStringLiteral("game_test"));
    }

    // A stock AI file is already available to the selected engine, but the
    // package graph deliberately reports its outgoing dependencies unverified.
    // Quick testing must present that concrete limitation for review instead
    // of making every ordinary AI-backed map impossible to test.
    std::filesystem::create_directories(root / "game" / "resource" / "misc" / "ai");
    QFile ai(QString::fromStdWString((root / "game" / "resource" / "misc" / "ai" / "walker.ai").wstring()));
    check(ai.open(QIODevice::WriteOnly), "game test: synthetic stock AI is writable");
    ai.write("# synthetic stock AI class\n");
    ai.close();
    ui::project::GameTestContext context;
    context.game = ProjectType::JKDF2;
    context.exportMap = [](const std::filesystem::path& destination)
    {
        QFile file(QString::fromStdWString(destination.wstring()));
        if ( !file.open(QIODevice::WriteOnly) ) return false;
        const QByteArray data("SECTION: AICLASS\nWorld AI Classes 1\nwalker.ai\nEND\n");
        return file.write(data) == data.size();
    };
    context.snapshotStillCurrent = [] { return true; };
    bool submittedReview = false;
    bool reviewSeen = false;
    QTimer reviewDriver;
    reviewDriver.setInterval(5);
    QObject::connect(&reviewDriver, &QTimer::timeout, [&]()
    {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if ( !dialog ) return;
        if ( dialog->objectName() == QStringLiteral("gameTestDialog") && !submittedReview )
        {
            const auto set = [&](const char* name, const QString& value)
            {
                if ( auto* edit = dialog->findChild<QLineEdit*>(QString::fromLatin1(name)) ) edit->setText(value);
            };
            set("gameTestGameDirectory", QString::fromStdWString((root / "game").wstring()));
            set("gameTestExecutable", QString::fromStdWString((root / "game" / "engine.exe").wstring()));
            set("gameTestStageRoot", QString::fromStdWString((root / "tests").wstring()));
            set("gameTestLevelName", QStringLiteral("review.jkl"));
            if ( auto* button = dialog->findChild<QPushButton*>(QStringLiteral("gameTestRun")) )
            {
                submittedReview = true;
                button->click();
            }
            else dialog->reject();
        }
        else
        {
            reviewSeen = dialog->objectName() == QStringLiteral("gameTestDependencyReview");
            dialog->reject();
        }
    });
    reviewDriver.start();
    bool reviewCancelled = false;
    try { reviewCancelled = !ui::project::saveJklAndTest(nullptr, context).started; }
    catch ( const std::exception& ) { }
    reviewDriver.stop();
    check(reviewSeen && reviewCancelled,
        "game test: stock AI dependencies get an explicit cancellable review, not a blanket refusal");
    settings.remove(QStringLiteral("game_test"));

    for ( const bool cog : { true, false } )
    {
        MainWindow dirtyWindow;
        dirtyWindow.setAttribute(Qt::WA_DontShowOnScreen);
        dirtyWindow.setProjectTypeForTest(ProjectType::JKDF2);
        if ( cog )
        {
            dirtyWindow.createNewCogScriptForTest(QStringLiteral("unsaved-test.cog"));
            auto* tabs = dirtyWindow.findChild<QTabWidget*>(QStringLiteral("cogScriptEditorTabs"));
            auto* page = tabs ? tabs->currentWidget() : nullptr;
            auto* editor = page ? page->findChild<QPlainTextEdit*>() : nullptr;
            check(editor != nullptr, "game test: COG draft editor exists");
            if ( editor )
            {
                editor->insertPlainText(QStringLiteral("\n# unsaved launch regression\n"));
                editor->document()->setModified(true);
            }
        }
        else
        {
            auto* create = dirtyWindow.findChild<QAction*>(QStringLiteral("tools.resource_editors.sprite_spr.new"));
            check(create != nullptr, "game test: Sprite draft creation action exists");
            if ( create ) create->trigger();
            auto* dock = dirtyWindow.findChild<QDockWidget*>(QStringLiteral("resourceEditorsDock"));
            QToolButton* createDraft = nullptr;
            for ( auto* candidate : dock
                    ? dock->findChildren<QToolButton*>(QStringLiteral("resource_authoring.new"))
                    : QList<QToolButton*>{} )
            {
                if ( !candidate->isHidden() )
                {
                    createDraft = candidate;
                    break;
                }
            }
            check(createDraft != nullptr, "game test: Sprite group New control exists");
            if ( createDraft )
            {
                createDraft->click();
            }
            auto* tabs = dirtyWindow.findChild<QTabWidget*>(QStringLiteral("resourceEditorTabs"));
            auto* page = tabs ? tabs->currentWidget() : nullptr;
            auto* material = page ? page->findChild<QLineEdit*>(QStringLiteral("spr.material")) : nullptr;
            check(material != nullptr, "game test: Sprite resource draft exists");
            if ( material ) material->setText(QStringLiteral("unsaved-material.mat"));
        }
        const auto beforePath = dirtyWindow.currentPathForTest();
        const auto beforeTitle = dirtyWindow.windowTitle();
        const auto beforeHistory = dirtyWindow.undoDepthForTest();
        bool actionableRefusal = false;
        bool launchSetupShown = false;
        QTimer dirtyDriver;
        dirtyDriver.setInterval(5);
        QObject::connect(&dirtyDriver, &QTimer::timeout, [&]()
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if ( !dialog ) return;
            if ( auto* message = qobject_cast<QMessageBox*>(dialog) )
            {
                actionableRefusal = message->text().contains(QStringLiteral("Save modified COG and resource files"));
                message->accept();
            }
            else
            {
                launchSetupShown = dialog->objectName() == QStringLiteral("gameTestDialog");
                dialog->reject();
            }
        });
        dirtyDriver.start();
        if ( auto* launch = dirtyWindow.findChild<QAction*>(QStringLiteral("file.save_jkl_test")) ) launch->trigger();
        dirtyDriver.stop();
        check(actionableRefusal && !launchSetupShown,
            "game test: modified COG/resource buffers refuse before stale saved resources can be tested");
        check(dirtyWindow.currentPathForTest() == beforePath && dirtyWindow.windowTitle() == beforeTitle
                && dirtyWindow.undoDepthForTest() == beforeHistory,
            "game test: resource-draft refusal preserves source ownership, dirty state and history");
    }
}
} // namespace sed::app_tests
