#include "test_common.hpp"

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "resource_release_test_fixtures.hpp"
#include "sed/assets/sed.hpp"
#include "ui/project/sed_editor_file_utils.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextCursor>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <filesystem>

namespace sed::app_tests
{
namespace
{

QString proposal(const QString& id, const int material = 1)
{
    const QJsonObject change{
        {"op", "setSurfaceMaterial"}, {"sector", 0}, {"surface", 0}, {"materialIndex", material}
    };
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"operationId", id}, {"operations", QJsonArray{change}}
    }).toJson(QJsonDocument::Compact));
}

QDialog* openReview(MainWindow& window, QAction& action, const QString& plan)
{
    const auto previous = window.findChildren<QDialog*>(QStringLiteral("agentReview.dialog"));
    action.trigger();
    for ( QDialog* dialog : window.findChildren<QDialog*>(QStringLiteral("agentReview.dialog")) )
    {
        if ( !previous.contains(dialog) )
        {
            auto* input = dialog->findChild<QPlainTextEdit*>(QStringLiteral("agentReview.plan"));
            check(input != nullptr, "a new review accepts a proposal in its visible plan field");
            if ( input )
            {
                input->setPlainText(plan);
                return dialog;
            }
        }
    }
    check(false, "the Tools action opens an independent native review dialog");
    return nullptr;
}

bool clickForOutcome(QDialog* dialog, const QString& button, const QString& expected)
{
    if ( !dialog )
    {
        return false;
    }
    auto* control = dialog->findChild<QAbstractButton*>(QStringLiteral("agentReview.") + button);
    auto* status = dialog->findChild<QLabel*>(QStringLiteral("agentReview.status"));
    if ( control && control->isEnabled() && status )
    {
        control->click();
        if ( status->property("outcome").toString() == expected )
        {
            return true;
        }
    }
    const QString detail = QStringLiteral("review %1 should report %2, got %3 (%4)")
        .arg(button, expected, status ? status->property("outcome").toString() : QStringLiteral("missing"),
            status ? status->text() : QString{});
    check(false, qPrintable(detail));
    return false;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size() && file.flush();
}

QPlainTextEdit* activeCogBuffer(MainWindow& window)
{
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("cogScriptEditorTabs"));
    return tabs && tabs->currentWidget()
        ? tabs->currentWidget()->findChild<QPlainTextEdit*>() : nullptr;
}

void appendCogCommand(QPlainTextEdit& buffer, const QString& text)
{
    QTextCursor command = buffer.textCursor();
    command.movePosition(QTextCursor::End);
    command.beginEditBlock();
    command.insertText(text);
    command.endEditBlock();
    buffer.setTextCursor(command);
}

bool workspaceIntact(MainWindow& window, const QString& draft)
{
    const editor::Selection& selection = window.documentStateForTest().selection;
    const QPlainTextEdit* buffer = activeCogBuffer(window);
    return buffer && buffer->toPlainText() == draft && window.hasUnsavedCogScriptChangesForTest()
        && window.glViewForTest()->selectedSurfaceRef() == SurfaceRef{0, 0}
        && selection.surfaces.size() == 1 && selection.surfaces.contains({0, 0})
        && selection.sectors.empty() && selection.surfaceVertices.empty()
        && selection.frames.empty() && selection.things.empty() && selection.lights.empty()
        && selection.cogs.empty();
}

bool triggerDocumentAction(MainWindow& window, const QString& name)
{
    // Save and Undo route keyboard focus inside a COG editor to its local document.
    if ( QWidget* focus = QApplication::focusWidget(); focus && window.isAncestorOf(focus) )
    {
        focus->clearFocus();
    }
    auto* action = window.findChild<QAction*>(name);
    if ( !action || !action->isEnabled() )
    {
        return false;
    }
    bool modalError = false;
    QTimer modalCheck;
    if ( name == QStringLiteral("file.save") )
    {
        QObject::connect(&modalCheck, &QTimer::timeout, [&]()
        {
            if ( auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) )
            {
                const QString reason = box->windowTitle() + " â€” " + box->text() + " " + box->informativeText();
                check(false, qPrintable(reason));
                modalError = true;
                box->reject();
            }
            else if ( auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()) )
            {
                check(false, "Save unexpectedly requested a path for a named native document");
                modalError = true;
                picker->reject();
            }
        });
        modalCheck.start(20);
    }
    action->trigger();
    return !modalError;
}

void applyExpectedMaterial(Level& level)
{
    Surface& face = level.sectors.at(0).surfaces.at(0);
    face.material = level.materials.at(1);
    face.nMat = 1;
}

bool containsWorkspace(const assets::SedFile& file)
{
    const auto contains = [&file](const auto& type)
    {
        return std::any_of(file.optionalSections.begin(), file.optionalSections.end(),
            [&type](const auto& section) { return section.type == type && !section.payload.empty(); });
    };
    return contains(ui::project::SedEditorStateSection) && contains(ui::project::SedHistorySection);
}

} // namespace

void runReviewedMaterialEditTests()
{
    QTemporaryDir temp;
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    auto* snapshots = window.findChild<QAction*>(QStringLiteral("tools.compare_saved_snapshots"));
    const auto* tools = snapshots ? qobject_cast<QMenu*>(snapshots->parent()) : nullptr;
    check(tools && tools->title().remove(QLatin1Char('&')) == QStringLiteral("Tools")
            && tools->actions().contains(snapshots), "the Tools menu exposes comparison of saved level snapshots");
    if ( snapshots )
    {
        const auto history = window.undoDepthForTest();
        snapshots->trigger();
        auto* baseline = window.findChild<QLineEdit*>(QStringLiteral("semantic.baseline"));
        auto* dialog = baseline ? qobject_cast<QDialog*>(baseline->window()) : nullptr;
        check(dialog && dialog->isVisible() && baseline->text().isEmpty()
                && dialog->findChild<QLineEdit*>(QStringLiteral("semantic.receipt")),
            "the Tools action opens the saved-file and recorded-receipt viewer with no implicit live input");
        if ( dialog ) dialog->close();
        check(history == window.undoDepthForTest(), "opening a saved comparison preserves document history");
    }
    auto* review = window.findChild<QAction*>(QStringLiteral("tools.review_agent_edit"));
    check(review, "Tools exposes reviewed material editing on the native editing window");
    if ( !review || !temp.isValid() )
    {
        check(temp.isValid(), "reviewed edit has an isolated project directory");
        return;
    }

    const std::filesystem::path root(temp.path().toStdWString());
    const auto project = root / "reviewed.sed";
    const QString matDir = temp.filePath(QStringLiteral("mat"));
    const QString resource = matDir + QStringLiteral("/reviewed.mat");
    const QByteArray originalMat = fixtures::makeIndexedMatFixture(23);
    const bool resourcesReady = QDir().mkpath(matDir)
        && writeBytes(matDir + QStringLiteral("/dflt.mat"), fixtures::makeIndexedMatFixture(7))
        && writeBytes(resource, originalMat);
    check(resourcesReady, "the project contains two distinct, valid loose MAT resources");
    if ( !resourcesReady )
    {
        return;
    }
    window.setRecoveryDirectoryForTest(root / "recovery");
    window.createNewProjectForTest(ProjectType::MOTS);
    window.setCurrentPathForTitleForTest(project);
    window.setResourcePathsForTest({root}, {}, project);
    window.activateSurfaceEditModeForTest();
    window.activateSurfaceFaceSubModeForTest();
    auto& selection = window.mutableDocumentStateForTest().selection;
    selection.clear();
    selection.surfaces.insert({0, 0});
    check(window.applyStructuralHistoryEditForTest([](editor::DocumentState& state)
        {
            state.level.materials.push_back("reviewed.mat");
            return true;
        }), "the existing history includes the addition of the target material");
    check(window.applyStructuralHistoryEditForTest([](editor::DocumentState& state)
        {
            state.level.header.gravity = 3.125;
            return true;
        }), "the existing history also contains a distinct authored header edit");

    window.createNewCogScriptForTest(QStringLiteral("review_draft"));
    auto* buffer = activeCogBuffer(window);
    check(buffer, "the new native document exposes its COG draft");
    if ( !buffer )
    {
        return;
    }
    appendCogCommand(*buffer, QStringLiteral("\n# R02 authored draft 37\n"));
    const QString draft = buffer->toPlainText();
    // Creating the draft also creates its normal COG instance and selects it.
    window.activateSurfaceEditModeForTest();
    auto& faceSelection = window.mutableDocumentStateForTest().selection;
    faceSelection.clear();
    faceSelection.surfaces.insert({0, 0});
    window.glViewForTest()->setSelectedSurface(0, 0);
    window.rebuildPropertiesForTest();
    check(triggerDocumentAction(window, "file.save"), "the File menu can save the starting native project");
    check(std::filesystem::is_regular_file(project), "native Save creates the starting .sed");
    if ( !std::filesystem::is_regular_file(project) )
    {
        return;
    }
    const auto initialFile = assets::loadSedFile(project);
    check(containsWorkspace(initialFile) && workspaceIntact(window, draft),
        "the starting native file carries EDIT and HIST and its live session retains the dirty draft and face");
    const auto startDepth = window.undoDepthForTest();
    check(startDepth >= 2 && window.redoDepthForTest() == 0,
        "the starting native document has a nontrivial committed history");
    const auto originalLevel = window.documentStateForTest().level;
    check(originalLevel.sectors.size() == 1 && originalLevel.sectors[0].surfaces.size() > 1
            && originalLevel.materials.size() == 2 && originalLevel.materials[1] == "reviewed.mat",
        "the normal new-project room supplies the addressed face and untouched neighbors");
    if ( originalLevel.sectors.empty() || originalLevel.sectors[0].surfaces.empty()
        || originalLevel.materials.size() != 2 )
    {
        return;
    }

    auto* cancelled = openReview(window, *review, proposal("cancelled"));
    clickForOutcome(cancelled, "prepare", "reviewed");
    clickForOutcome(cancelled, "cancel", "cancelled");
    if ( cancelled )
    {
        cancelled->close();
    }
    auto* cancelRetry = openReview(window, *review, proposal("cancelled"));
    clickForOutcome(cancelRetry, "query", "cancelled");
    clickForOutcome(cancelRetry, "prepare", "cancelled");

    auto* staleMat = openReview(window, *review, proposal("changed-mat"));
    clickForOutcome(staleMat, "prepare", "reviewed");
    // Same filename, file size, and mtime. A timestamp-only check would miss this change.
    const std::filesystem::path matPath(resource.toStdWString());
    const auto oldTime = std::filesystem::last_write_time(matPath);
    check(writeBytes(resource, fixtures::makeIndexedMatFixture(24)), "the consumed MAT changes on disk");
    std::filesystem::last_write_time(matPath, oldTime);
    clickForOutcome(staleMat, "accept", "stale");
    check(writeBytes(resource, originalMat), "the reviewed resource bytes can be restored");
    check(window.undoDepthForTest() == startDepth
            && assets::sedLevelsEqual(originalLevel, window.documentStateForTest().level)
            && workspaceIntact(window, draft),
        "cancel, reconnect and rejected resource changes leave the whole session intact");

    auto* staleDraft = openReview(window, *review, proposal("changed-draft"));
    clickForOutcome(staleDraft, "prepare", "reviewed");
    appendCogCommand(*buffer, QStringLiteral("# user revision\n"));
    const QString revisedDraft = buffer->toPlainText();
    clickForOutcome(staleDraft, "accept", "stale");
    check(revisedDraft != draft && workspaceIntact(window, revisedDraft)
            && window.undoDepthForTest() == startDepth
            && assets::sedLevelsEqual(originalLevel, window.documentStateForTest().level),
        "editing only the COG buffer invalidates the review and preserves the user's new text");
    buffer->undo();
    check(buffer->toPlainText() == draft, "one COG text Undo restores the previous dirty buffer");

    auto* staleDoc = openReview(window, *review, proposal("changed-document"));
    clickForOutcome(staleDoc, "prepare", "reviewed");
    check(window.applyStructuralHistoryEditForTest([](editor::DocumentState& state)
        {
            state.level.header.gravity = 6.25;
            return true;
        }), "an ordinary authored edit advances the reviewed document");
    const auto beforeLevel = window.documentStateForTest().level;
    const auto beforeDepth = window.undoDepthForTest();
    clickForOutcome(staleDoc, "accept", "stale");
    check(window.undoDepthForTest() == startDepth + 1
            && assets::sedLevelsEqual(beforeLevel, window.documentStateForTest().level),
        "the stale proposal leaves the newer author edit and timeline intact");

    auto* first = openReview(window, *review, proposal("first"));
    auto* second = openReview(window, *review, proposal("second"));
    clickForOutcome(first, "prepare", "reviewed");
    clickForOutcome(second, "prepare", "reviewed");
    check(window.undoDepthForTest() == beforeDepth
            && assets::sedLevelsEqual(beforeLevel, window.documentStateForTest().level),
        "two simultaneous reviews can be inspected before either mutates the level");
    auto* preview = first ? first->findChild<QLabel*>(QStringLiteral("agentReview.status")) : nullptr;
    check(preview && preview->text().contains(QStringLiteral("dflt.mat"))
            && preview->text().contains(QStringLiteral("reviewed.mat")),
        "the review presents both the old and proposed material before the user accepts");
    clickForOutcome(first, "accept", "applied");
    clickForOutcome(second, "accept", "stale");
    Level expected = beforeLevel;
    applyExpectedMaterial(expected);
    check(window.undoDepthForTest() == beforeDepth + 1 && window.redoDepthForTest() == 0
            && assets::sedLevelsEqual(expected, window.documentStateForTest().level)
            && workspaceIntact(window, draft),
        "only the winning proposal changes its one face and appends exactly one undo step");

    if ( first )
    {
        first->close();
    }
    auto* reconnect = openReview(window, *review, proposal("first"));
    clickForOutcome(reconnect, "query", "applied");
    clickForOutcome(reconnect, "prepare", "applied");
    auto* conflictingId = openReview(window, *review, proposal("first", 0));
    clickForOutcome(conflictingId, "prepare", "conflict");
    check(window.undoDepthForTest() == beforeDepth + 1
            && assets::sedLevelsEqual(expected, window.documentStateForTest().level),
        "query, identical retry and conflicting reuse do not reapply or overwrite the accepted operation");

    check(triggerDocumentAction(window, "edit.undo") && window.undoDepthForTest() == beforeDepth
            && window.redoDepthForTest() == 1
            && assets::sedLevelsEqual(beforeLevel, window.documentStateForTest().level)
            && workspaceIntact(window, draft),
        "one Undo restores the complete pre-edit level, selected face, and dirty COG buffer");
    clickForOutcome(reconnect, "query", "applied");
    check(window.undoDepthForTest() == beforeDepth && window.redoDepthForTest() == 1,
        "querying the recorded operation after Undo preserves the redo branch");
    check(triggerDocumentAction(window, "edit.redo") && window.undoDepthForTest() == beforeDepth + 1
            && window.redoDepthForTest() == 0
            && assets::sedLevelsEqual(expected, window.documentStateForTest().level)
            && workspaceIntact(window, draft),
        "one Redo restores the reviewed material with the workspace intact");

    check(triggerDocumentAction(window, "file.save"), "the File menu can save the accepted edit");
    const auto saved = assets::loadSedFile(project);
    check(containsWorkspace(saved) && saved.level.sectors.at(0).surfaces.at(0).material == "reviewed.mat"
            && saved.level.header.gravity == 6.25 && !QFile::exists(temp.filePath("cog/review_draft.cog")),
        "native Save persists the result and workspace without publishing the COG draft as a loose script");

    MainWindow reopened{};
    reopened.setAttribute(Qt::WA_DontShowOnScreen);
    reopened.setRecoveryDirectoryForTest(root / "reopened-recovery");
    reopened.openLevelFromFilenameForTest(QString::fromStdWString(project.wstring()));
    check(reopened.currentPathForTest() == project && reopened.undoDepthForTest() == beforeDepth + 1
            && reopened.redoDepthForTest() == 0 && workspaceIntact(reopened, draft)
            && assets::sedLevelsEqual(saved.level, reopened.documentStateForTest().level),
        "opening the .sed in a fresh native window restores the level, selected face, unsaved COG and history");
    if ( reopened.documentStateForTest().level.sectors.empty() || reopened.documentStateForTest().level.sectors[0].surfaces.empty() )
    {
        return;
    }
    check(reopened.undoForTest() && reopened.documentStateForTest().level.sectors.at(0).surfaces.at(0).material
                == beforeLevel.sectors.at(0).surfaces.at(0).material
            && reopened.documentStateForTest().level.header.gravity == 6.25
            && reopened.undoForTest() && reopened.documentStateForTest().level.header.gravity == 3.125,
        "the reopened timeline retains both the precise reviewed transaction and its authored predecessor");

    // Point at a valid MAT inside our own temporary root. Even an existing resource may
    // only be addressed by its project name, including after whitespace normalization.
    auto* reopenedReview = reopened.findChild<QAction*>(QStringLiteral("tools.review_agent_edit"));
    check(reopenedReview, "the reopened native window owns its review action");
    if ( !reopenedReview )
    {
        return;
    }
    const int paddedSlot = static_cast<int>(reopened.documentStateForTest().level.materials.size());
    check(reopened.applyStructuralHistoryEditForTest([&resource](editor::DocumentState& state)
        {
            state.level.materials.push_back(("  " + resource + "  ").toStdString());
            return true;
        }), "the source level can represent a padded absolute material slot");
    const auto refusalDepth = reopened.undoDepthForTest();
    const auto refusalLevel = reopened.documentStateForTest().level;
    clickForOutcome(openReview(reopened, *reopenedReview, proposal("absolute-name", paddedSlot)),
        "prepare", "invalid");
    QJsonObject suppliedSource = QJsonDocument::fromJson(proposal("wrong-schema").toUtf8()).object();
    suppliedSource.insert("source", "unused.sed");
    clickForOutcome(openReview(reopened, *reopenedReview,
        QString::fromUtf8(QJsonDocument(suppliedSource).toJson(QJsonDocument::Compact))), "prepare", "invalid");
    check(reopened.undoDepthForTest() == refusalDepth
            && assets::sedLevelsEqual(refusalLevel, reopened.documentStateForTest().level),
        "refused source and material names leave the authoritative document and history intact");
}

} // namespace sed::app_tests
