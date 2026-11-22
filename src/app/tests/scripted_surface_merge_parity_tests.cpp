#include "test_common.hpp"

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/merge.hpp"

#include <QAction>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QApplication>

namespace sed::app_tests
{

namespace
{

bool sameSelection(const editor::Selection& left, const editor::Selection& right)
{
    return left.sectors == right.sectors && left.surfaces == right.surfaces
        && left.surfaceVertices == right.surfaceVertices && left.things == right.things
        && left.frames == right.frames && left.lights == right.lights && left.cogs == right.cogs;
}

Level portalReviewFixture(const bool scripted)
{
    Level level{};
    level.kind = ProjectType::MOTS;
    level.header.version = 1;
    level.layers = {"Main", "Mirror"};
    level.lvisstring = "11";
    for ( int sectorIndex = 0; sectorIndex < 3; ++sectorIndex )
    {
        auto& sector = level.newSector();
        sector.num = sectorIndex;
        sector.layer = sectorIndex == 1 ? 1 : 0;
        for ( int face = 0; face < 2; ++face )
        {
            const double x = face + (sectorIndex == 2 ? 4.0 : 0.0);
            const auto a = sector.addVertex(x, 0.0, 0.0);
            const auto b = sector.addVertex(x + 1.0, 0.0, 0.0);
            const auto c = sector.addVertex(x + 1.0, 1.0, 0.0);
            const auto d = sector.addVertex(x, 1.0, 0.0);
            Surface surface{};
            surface.vertices = sectorIndex == 1
                ? std::vector<std::int32_t>{d, c, b, a}
                : std::vector<std::int32_t>{a, b, c, d};
            surface.texVertices.resize(4);
            surface.normal = sed::level::surfaceCalcNormalNewell(sector, surface);
            if ( sectorIndex < 2 )
            {
                surface.adjoin = SurfaceRef{1 - sectorIndex, face};
            }
            (void)sector.addSurface(std::move(surface));
        }
    }
    if ( scripted )
    {
        Cog cog{};
        cog.name = "switch%1-%10.cog";
        for ( int sector = 0; sector < 2; ++sector )
        {
            CogValue value{};
            value.name = "door%2-%9";
            value.type = ValueType::ptr;
            value.cogType = CogType::surface;
            value.intValue = level.getGlobalSurfaceNumber(sector, 1);
            cog.values.push_back(value);
        }
        level.cogs.push_back(std::move(cog));
    }
    return level;
}

void preparePortalSelection(MainWindow& window, const bool mixed)
{
    window.activateSurfaceEditModeForTest();
    window.activateSurfaceFaceSubModeForTest();
    auto& selection = window.mutableDocumentStateForTest().selection;
    selection.clear();
    selection.surfaces = {{0, 0}, {0, 1}};
    if ( mixed )
    {
        selection.surfaces.insert({2, 0});
        selection.surfaces.insert({2, 1});
    }
    window.glViewForTest()->setSelectedSurface(0, 0);
    window.updateEditActionsForTest();
}

void reviewPortalCountsNamesAndLocks(const std::filesystem::path& root)
{
    for ( const bool mixed : {false, true} )
    {
        const auto path = root / (mixed ? "mixed-review.sed" : "portal-review.sed");
        assets::saveToSED(portalReviewFixture(true), path);
        MainWindow window{};
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.setRecoveryDirectoryForTest(root / (mixed ? "mixed-recovery" : "portal-recovery"));
        window.openLevelFromFilenameForTest(QString::fromStdWString(path.wstring()));
        preparePortalSelection(window, mixed);
        auto* merge = window.findChild<QAction*>(QStringLiteral("viewport.surface.merge"));
        check(merge && merge->isEnabled(), "portal review fixture exposes an enabled merge action");
        if ( !merge ) return;
        const auto before = window.documentStateForTest();
        const auto depth = window.undoDepthForTest();
        bool reviewed = false;
        QTimer reviewer;
        reviewer.setSingleShot(true);
        QObject::connect(&reviewer, &QTimer::timeout, [&]()
        {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if ( !box ) return;
            reviewed = true;
            check(box->text().startsWith(mixed
                    ? "3 faces will be removed, including 2 reciprocal portal faces."
                    : "2 faces will be removed, including 2 reciprocal portal faces."),
                "review counts both sides of a reciprocal removal in pure and mixed batches");
            check(box->detailedText() == QStringLiteral(
                    "COG 0 (switch%1-%10.cog), argument 0 (door%2-%9): face 0:1 to 0:0; global 1 to 0\n"
                    "COG 0 (switch%1-%10.cog), argument 1 (door%2-%9): face 1:1 to 1:0; global 3 to 1"),
                "review preserves literal placeholder-like names and every numeric binding address");
            box->reject();
        });
        reviewer.start(0);
        merge->trigger();
        reviewer.stop();
        check(reviewed && assets::sedLevelsEqual(before.level, window.documentStateForTest().level)
                && sameSelection(before.selection, window.documentStateForTest().selection) && window.undoDepthForTest() == depth,
            "canceling a portal review preserves both owners, selection and history");

        QTimer accept;
        accept.setSingleShot(true);
        QObject::connect(&accept, &QTimer::timeout, [&]()
        {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->done(QMessageBox::Yes);
        });
        accept.start(0);
        merge->trigger();
        accept.stop();
        const auto after = window.documentStateForTest();
        check(after.level.sectors[0].surfaces.size() == 1
                && after.level.sectors[1].surfaces.size() == 1
                && after.level.sectors[2].surfaces.size() == (mixed ? 1u : 2u)
                && after.level.cogs[0].values[0].intValue == 0
                && after.level.cogs[0].values[1].intValue == 1
                && window.undoDepthForTest() == depth + 1,
            "accepting an editable reciprocal merge updates both owners and scripted targets in one transaction");
        check(window.undoForTest() && assets::sedLevelsEqual(before.level, window.documentStateForTest().level)
                && sameSelection(before.selection, window.documentStateForTest().selection)
                && window.redoForTest() && assets::sedLevelsEqual(after.level, window.documentStateForTest().level)
                && sameSelection(after.selection, window.documentStateForTest().selection),
            "accepted reciprocal merge Undo and Redo preserve exact geometry, bindings and selection");
    }

    for ( int mode = 0; mode < 4; ++mode )
    {
        const bool scripted = mode != 2;
        const bool duringReview = mode == 1 || mode == 3;
        const auto suffix = std::to_string(mode);
        const auto path = root / ("locked-mirror-" + suffix + ".sed");
        assets::saveToSED(portalReviewFixture(scripted), path);
        MainWindow window{};
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.setRecoveryDirectoryForTest(root / ("locked-recovery-" + suffix));
        window.openLevelFromFilenameForTest(QString::fromStdWString(path.wstring()));
        window.setLayerVisualStateForTest({true, true}, {false, !duringReview});
        preparePortalSelection(window, false);
        auto* merge = window.findChild<QAction*>(QStringLiteral("viewport.surface.merge"));
        check(merge, "mirror editability fixture exposes the merge action");
        if ( !merge ) return;
        const auto before = window.documentStateForTest();
        auto expectedLevel = before.level;
        const auto depth = window.undoDepthForTest();
        bool reviewed = false;
        QTimer reviewer;
        reviewer.setSingleShot(true);
        QObject::connect(&reviewer, &QTimer::timeout, [&]()
        {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if ( !box ) return;
            reviewed = true;
            if ( duringReview )
            {
                window.setLayerVisualStateForTest({true, mode != 3}, {false, mode == 1});
                // The user's visibility change is legitimate document state;
                // refusing Merge must preserve it while leaving topology intact.
                expectedLevel.lvisstring = window.documentStateForTest().level.lvisstring;
            }
            box->done(QMessageBox::Yes);
        });
        reviewer.start(0);
        merge->trigger();
        reviewer.stop();
        check(!duringReview || reviewed, "mirror layer changes are exercised inside the actual confirmation");
        check(assets::sedLevelsEqual(expectedLevel, window.documentStateForTest().level)
                && sameSelection(before.selection, window.documentStateForTest().selection) && window.undoDepthForTest() == depth,
            "a locked or newly hidden unselected mirror owner refuses the complete merge without history");
    }
}

} // namespace

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

    reviewPortalCountsNamesAndLocks(root);
}

} // namespace sed::app_tests
