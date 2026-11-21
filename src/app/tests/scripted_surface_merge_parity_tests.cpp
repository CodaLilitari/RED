#include "test_common.hpp"

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"

#include <QAction>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QApplication>

namespace sed::app_tests
{

void runScriptedSurfaceMergeParity(const QString& fixtureExecutable)
{
    QTemporaryDir temp;
    const QString path = temp.filePath(QStringLiteral("agent"));
    QProcess agent;
    agent.setProgram(fixtureExecutable);
    agent.setArguments({QStringLiteral("--write-fixture"), path});
    agent.start();
    const bool ran = agent.waitForFinished(60000) && agent.exitStatus() == QProcess::NormalExit && agent.exitCode() == 0;
    if (!ran)
    {
        check(false, qPrintable(QStringLiteral("the native parity test requires its reviewed agent outputs: ")
            + QString::fromUtf8(agent.readAllStandardError())));
        return;
    }

    const std::filesystem::path root(path.toStdWString());
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(root / "recovery");
    window.openLevelFromFilenameForTest(QString::fromStdWString((root/"source.sed").wstring()));
    window.activateSurfaceEditModeForTest();
    window.activateSurfaceFaceSubModeForTest();
    auto* merge = window.findChild<QAction*>(QStringLiteral("viewport.surface.merge"));
    check(merge, "the GUI exposes its normal selected-face merge action");
    if (!merge) return;
    for (const int room : {0,1})
    {
        auto& selection = window.mutableDocumentStateForTest().selection;
        selection.clear();
        selection.surfaces = {{room,0},{room,6},{room,7},{room,8}};
        window.glViewForTest()->setSelectedSurface(room,0);
        window.updateEditActionsForTest();
        const auto before = window.documentStateForTest().level;
        const auto depth = window.undoDepthForTest();
        bool cancelledReview = false;
        QTimer::singleShot(0, &window, [&]()
        {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            cancelledReview = box->objectName() == "surfaceMergeReview"
                && box->detailedText().contains("paired-switch.cog");
            box->reject();
        });
        merge->trigger();
        check(cancelledReview && window.undoDepthForTest() == depth
                && assets::sedLevelsEqual(window.documentStateForTest().level, before),
            "the normal merge action discloses affected COG arguments and cancel changes nothing");

        bool staleReview = false;
        QTimer::singleShot(0, &window, [&]()
        {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            staleReview = true;
            window.mutableDocumentStateForTest().selection.surfaces.erase({room,8});
            box->done(QMessageBox::Yes);
        });
        merge->trigger();
        check(staleReview && window.undoDepthForTest() == depth
                && assets::sedLevelsEqual(window.documentStateForTest().level, before),
            "confirmation cannot publish against a replaced surface selection");
        window.mutableDocumentStateForTest().selection.surfaces.insert({room,8});

        QTimer::singleShot(0, &window, []()
        {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->done(QMessageBox::Yes);
        });
        merge->trigger();
        const auto& document = window.documentStateForTest();
        const auto expected = assets::loadFromSED(root/(room == 0 ? "unrelated.sed" : "agent.sed"));
        check(assets::sedLevelsEqual(document.level, expected) && window.undoDepthForTest() == depth + 1
                && document.selection.surfaces.size() == 1 && document.selection.surfaces.contains({room,0}),
            "GUI and reviewed agent reach identical authored levels and GUI selects the surviving door/face");
        check(window.undoForTest() && assets::sedLevelsEqual(window.documentStateForTest().level, before),
            "document Undo restores the pre-merge geometry and original scripted bindings");
        check(window.redoForTest() && assets::sedLevelsEqual(window.documentStateForTest().level, expected),
            "document Redo restores the same typed references as the agent's saved result");
    }

    const auto guiFile = root/"gui.sed";
    window.setCurrentPathForTitleForTest(guiFile);
    auto* save = window.findChild<QAction*>(QStringLiteral("file.save"));
    check(save, "native parity test can save the GUI-created timeline");
    if (!save) return;
    QTimer messages;
    QObject::connect(&messages, &QTimer::timeout, []()
    {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            check(false, qPrintable(box->windowTitle() + " " + box->text()));
            box->reject();
        }
    });
    messages.start(25);
    save->trigger();
    messages.stop();
    if (!std::filesystem::is_regular_file(guiFile))
    {
        check(false, "the native editor saved the scripted merge result");
        return;
    }
    MainWindow reopened{};
    reopened.setAttribute(Qt::WA_DontShowOnScreen);
    reopened.setRecoveryDirectoryForTest(root / "reopened-recovery");
    reopened.openLevelFromFilenameForTest(QString::fromStdWString(guiFile.wstring()));
    const auto expected = assets::loadFromSED(root/"agent.sed");
    const auto& state = reopened.documentStateForTest();
    check(state.level.cogs.size() == 1 && state.level.cogs[0].values[0].intValue == expected.cogs[0].values[0].intValue
            && state.level.cogs[0].values[1].intValue == expected.cogs[0].values[1].intValue
            && state.selection.surfaces.contains({1,0}) && reopened.undoDepthForTest() == 2,
        "native Save/Open retains both script targets, selected survivor and the two real edit steps");
}

} // namespace sed::app_tests
