#include "app_settings.hpp"
#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"
#include "ui/project/sed_editor_file_utils.hpp"
#include "ui/recovery/document_recovery_utils.hpp"

#include <QAction>
#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>

namespace
{
int failures{};
void check(bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
QCborMap namedViewpoints()
{
    const QCborArray pose{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 10.0, 0.0, 0.0, 0.0, 0, 90.0, 64.0, 0.015625, 5000.0,
        true, 800, 600, 1.0 };
    QCborMap entry;
    entry.insert(QStringLiteral("name"), QStringLiteral("North room"));
    entry.insert(QStringLiteral("pose"), pose);
    QCborMap result;
    // EDIT sorts map keys before encoding. QCborMap equality also compares
    // insertion order, so the exact expected snapshot uses that same order.
    result.insert(QStringLiteral("cameras"), QCborArray{ entry });
    result.insert(QStringLiteral("version"), 1);
    return result;
}
QByteArray payload(const QCborMap& root)
{
    return sed::ui::recovery::serializeCanonicalCbor(root);
}
QCborMap captured(sed::MainWindow& window)
{
    const auto bytes = window.captureRecoveryEditorStateForTest();
    check(!bytes.isEmpty(), "capture complete workspace fixture");
    return QCborValue::fromCbor(bytes).toMap();
}
std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().toStdString());
#endif
}
void install(sed::MainWindow& window, const std::filesystem::path& recoveryDirectory)
{
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(recoveryDirectory);
    sed::editor::DocumentState state;
    state.level.kind = sed::ProjectType::MOTS;
    state.level.uvCoordinateSpace = sed::UvCoordinateSpace::Normalized;
    state.level.layers = { "Fixture" };
    const auto sector = sed::level::levelCreateSectorPrimitiveBoxAxes(
        state.level, sed::level::SectorPrimitive::Cube, { -2.0, -2.0, -2.0 }, { 2.0, 2.0, 2.0 }, 0, 1, 2, 0);
    check(sector == 0, "create project-owned camera fixture");
    window.setDocumentStateForTest(std::move(state));
    window.glViewForTest()->setLevel(&window.mutableDocumentStateForTest().level);
    window.markCurrentDocumentAsSavedForTest();
}
void workspaceRoundtrip(const std::filesystem::path& root)
{
    sed::MainWindow window;
    install(window, root / "roundtrip-recovery");
    const auto base = captured(window);
    auto supplied = base;
    supplied.insert(QStringLiteral("savedCameras"), namedViewpoints());
    const auto levelBefore = sed::assets::saveToSedBytes(window.documentStateForTest().level);
    const auto undoBefore = window.undoDepthForTest();
    QString warning;
    check(window.restoreRecoveryEditorStateForTest(payload(supplied), &warning), "valid saved viewpoints restore");
    check(captured(window).value(QStringLiteral("savedCameras")) == namedViewpoints(),
        "named list survives EDIT capture and restore");
    const auto beforeMalformed = captured(window);
    auto malformed = supplied;
    malformed.insert(QStringLiteral("savedCameras"), true);
    check(!window.validateRecoveryEditorStateForTest(payload(malformed), &warning),
        "malformed known saved-camera field fails validation");
    check(!window.restoreRecoveryEditorStateForTest(payload(malformed), &warning),
        "malformed known saved-camera field fails restoration");
    check(captured(window) == beforeMalformed, "failed saved-camera restore preserves complete live workspace");
    check(sed::assets::saveToSedBytes(window.documentStateForTest().level) == levelBefore &&
              window.undoDepthForTest() == undoBefore && !window.hasUnsavedChangesForTest(),
        "viewpoint persistence leaves Level and dirty/Undo state unchanged");
    check(window.restoreRecoveryEditorStateForTest(payload(base), &warning),
        "legacy EDIT without saved cameras restores");
    const auto absent = captured(window).value(QStringLiteral("savedCameras"));
    check(absent.isUndefined() || absent.toMap().value(QStringLiteral("cameras")).toArray().isEmpty(),
        "absent field clears previous named viewpoints");
}
void ordinaryMenu(const std::filesystem::path& root)
{
    sed::MainWindow window;
    install(window, root / "menu-recovery");
    auto* action = window.findChild<QAction*>(QStringLiteral("viewport.camera.saved"));
    check(action != nullptr && action->isEnabled(), "View menu exposes saved camera GUI");
    if ( !action )
    {
        return;
    }
    const auto levelBefore = sed::assets::saveToSedBytes(window.documentStateForTest().level);
    const auto undoBefore = window.undoDepthForTest();
    bool visited = false;
    QTimer::singleShot(0, &window,
        [&]
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(dialog != nullptr, "saved camera action opens ordinary dialog");
            if ( !dialog )
            {
                return;
            }
            auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("savedCameraName"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("savedCameraAdd"));
            check(name && save, "named camera actions are available through real menu");
            if ( name && save )
            {
                name->setText(QStringLiteral("From GUI"));
                save->click();
                visited = true;
            }
            dialog->reject();
        });
    action->trigger();
    check(visited, "menu callback completed");
    const auto entries =
        captured(window).value(QStringLiteral("savedCameras")).toMap().value(QStringLiteral("cameras")).toArray();
    check(entries.size() == 1 && entries[0].toMap().value(QStringLiteral("name")).toString() == "From GUI",
        "real GUI store reaches native workspace serializer");
    check(sed::assets::saveToSedBytes(window.documentStateForTest().level) == levelBefore &&
              window.undoDepthForTest() == undoBefore && !window.hasUnsavedChangesForTest(),
        "real GUI store does not author Level state");
}
void restoreNamed(sed::MainWindow& window)
{
    auto supplied = captured(window);
    supplied.insert(QStringLiteral("savedCameras"), namedViewpoints());
    check(window.restoreRecoveryEditorStateForTest(payload(supplied)),
        "install named viewpoints through workspace parser");
}
QCborValue savedViews(sed::MainWindow& window)
{
    return captured(window).value(QStringLiteral("savedCameras"));
}
QCborValue fileSavedViews(const std::filesystem::path& path)
{
    if ( !std::filesystem::exists(path) )
    {
        return {};
    }
    const auto file = sed::assets::loadSedFile(path);
    const auto section = std::find_if(file.optionalSections.begin(), file.optionalSections.end(),
        [](const auto& entry) { return entry.type == sed::ui::project::SedEditorStateSection; });
    if ( section == file.optionalSections.end() )
    {
        return {};
    }
    const QByteArray bytes(
        reinterpret_cast<const char*>(section->payload.data()), static_cast<qsizetype>(section->payload.size()));
    return QCborValue::fromCbor(bytes).toMap().value(QStringLiteral("savedCameras"));
}
void triggerFileAction(sed::MainWindow& window, const QString& actionName, const std::filesystem::path& selected = {})
{
    auto* action = window.findChild<QAction*>(actionName);
    check(action != nullptr, "ordinary file action exists");
    QTimer driver;
    driver.setInterval(5);
    QObject::connect(&driver, &QTimer::timeout,
        [&]
        {
            if ( auto* chooser = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()) )
            {
                check(!selected.empty(), "file action requests only the expected synthetic destination");
                if ( selected.empty() )
                {
                    chooser->reject();
                }
                else
                {
                    chooser->setOption(QFileDialog::DontConfirmOverwrite, true);
                    chooser->selectFile(QString::fromStdWString(selected.wstring()));
                    QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
                }
            }
            else if ( auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) )
            {
                check(false, "native viewpoint Save has no error or confirmation dialog");
                message->reject();
            }
        });
    driver.start();
    if ( action )
    {
        action->trigger();
    }
}
void nativeSaveAndReplacement(const std::filesystem::path& root)
{
    sed::MainWindow window;
    install(window, root / "save-recovery");
    restoreNamed(window);
    const auto savePath = root / "named.sed";
    window.setCurrentPathForTitleForTest(savePath);
    triggerFileAction(window, QStringLiteral("file.save"));
    check(fileSavedViews(savePath) == namedViewpoints(), "native Save writes named viewpoints into EDIT");
    check(savedViews(window) == namedViewpoints(), "native Save retains the live named list");
    const auto saveAsPath = root / "renamed.sed";
    triggerFileAction(window, QStringLiteral("file.save_as"), saveAsPath);
    check(fileSavedViews(saveAsPath) == namedViewpoints() && savedViews(window) == namedViewpoints(),
        "native Save As preserves viewpoints in the file and live workspace");

    window.createNewProjectForTest(sed::ProjectType::JKDF2);
    check(savedViews(window).isUndefined(), "New clears the prior document named list");
    window.markCurrentDocumentAsSavedForTest();
    if ( std::filesystem::exists(savePath) )
    {
        window.openLevelFromFilenameForTest(QString::fromStdWString(savePath.wstring()));
        check(savedViews(window) == namedViewpoints(), "native Open restores the saved viewpoints");
    }
    const auto legacyPath = root / "without-edit.sed";
    const auto bytes = sed::assets::saveToSedBytes(window.documentStateForTest().level);
    QFile legacy(QString::fromStdWString(legacyPath.wstring()));
    check(legacy.open(QIODevice::WriteOnly) &&
              legacy.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
                  static_cast<qint64>(bytes.size()),
        "write synthetic native file without EDIT");
    legacy.close();
    window.markCurrentDocumentAsSavedForTest();
    window.openLevelFromFilenameForTest(QString::fromStdWString(legacyPath.wstring()));
    check(savedViews(window).isUndefined(), "Open without EDIT cannot inherit another document's viewpoints");
    window.discardCurrentRecoveryForTest();
}
void recoveryPersistence(const std::filesystem::path& root)
{
    sed::MainWindow window;
    install(window, root / "live-recovery");
    window.beginRecoveryDocumentForTest(root / "source.sed", false);
    const auto before = sed::assets::saveToSedBytes(window.documentStateForTest().level);
    restoreNamed(window);
    window.triggerPeriodicRecoverySnapshotForTest();
    window.waitForRecoverySnapshotForTest();
    const auto livePath = window.currentRecoveryPathForTest();
    const auto clean = sed::ui::recovery::readRecoveryRecord(livePath);
    check(clean && !clean->metadata.requiresUserRecovery &&
              QCborValue::fromCbor(clean->editorStatePayload).toMap().value(QStringLiteral("savedCameras")) ==
                  namedViewpoints(),
        "workspace-only periodic checkpoint retains viewpoints without offering an authored recovery");
    check(sed::assets::saveToSedBytes(window.documentStateForTest().level) == before &&
              !window.hasUnsavedChangesForTest() && window.undoDepthForTest() == 0,
        "workspace-only checkpoint preserves Level bytes, dirty state and Undo");

    window.pushUndoForTitleForTest();
    window.mutableDocumentStateForTest().level.header.gravity = 3.125;
    window.triggerRecoverySnapshotForTest();
    window.waitForRecoverySnapshotForTest();
    window.pushUndoForTitleForTest();
    window.mutableDocumentStateForTest().level.header.gravity = 6.25;
    window.triggerPeriodicRecoverySnapshotForTest();
    window.waitForRecoverySnapshotForTest();
    const auto archives = sed::ui::recovery::scanAutosaveHistory(root / "live-recovery");
    check(archives.size() == 1, "periodic checkpoint archives the prior authored recovery");
    if ( archives.size() == 1 )
    {
        const auto historical = sed::ui::recovery::readRecoveryRecord(archives.front().recoveryPath);
        check(historical && historical->decodedLevel && historical->decodedLevel->header.gravity == 3.125 &&
                  QCborValue::fromCbor(historical->editorStatePayload).toMap().value(QStringLiteral("savedCameras")) ==
                      namedViewpoints(),
            "historical autosave binds named viewpoints to the prior authored Level");
        sed::MainWindow restored;
        install(restored, root / "restored-recovery");
        check(restored.applyRecoveryFileForTest(archives.front().recoveryPath) &&
                  savedViews(restored) == namedViewpoints() &&
                  restored.documentStateForTest().level.header.gravity == 3.125,
            "recovery installation restores named viewpoints with its document");
        restored.waitForRecoverySnapshotForTest();
        restored.discardCurrentRecoveryForTest();
    }
    window.discardCurrentRecoveryForTest();
}
void staleOpenDialog(const std::filesystem::path& root)
{
    sed::MainWindow window;
    install(window, root / "stale-recovery");
    restoreNamed(window);
    auto* action = window.findChild<QAction*>(QStringLiteral("viewport.camera.saved"));
    if ( !action )
    {
        return;
    }
    bool visited = false;
    QTimer::singleShot(0, &window,
        [&]
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(dialog != nullptr, "stale owner case opens dialog");
            if ( !dialog )
            {
                return;
            }
            auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("savedCameraName"));
            auto* save = dialog->findChild<QPushButton*>(QStringLiteral("savedCameraAdd"));
            install(window, root / "replacement-recovery");
            if ( name && save )
            {
                name->setText(QStringLiteral("Obsolete viewpoint"));
                save->click();
                visited = true;
            }
            dialog->reject();
        });
    action->trigger();
    check(visited && savedViews(window).isUndefined(), "old dialog cannot store viewpoints after document replacement");
}
} // namespace

int runSavedCameraIntegrationTests()
{
    failures = 0;
    QTemporaryDir directory;
    check(directory.isValid(), "camera integration uses isolated settings");
    if ( !directory.isValid() )
    {
        return 1;
    }
    QFile settingsFile(directory.filePath("red.ini"));
    check(settingsFile.open(QIODevice::WriteOnly), "camera integration precreates isolated settings");
    if ( !settingsFile.isOpen() )
    {
        return 1;
    }
    settingsFile.close();
    const auto oldSettings = sed::appSettingsIniPath();
#ifdef Q_OS_WIN
    sed::setAppSettingsIniPathOverrideForTest(std::filesystem::path(directory.filePath("red.ini").toStdWString()));
#else
    sed::setAppSettingsIniPathOverrideForTest(
        std::filesystem::u8path(directory.filePath("red.ini").toUtf8().toStdString()));
#endif
    const auto root = nativePath(directory.path());
    workspaceRoundtrip(root);
    ordinaryMenu(root);
    nativeSaveAndReplacement(root);
    recoveryPersistence(root);
    staleOpenDialog(root);
    sed::setAppSettingsIniPathOverrideForTest(oldSettings);
    std::cout << "Saved camera integration failures: " << failures << '\n';
    return failures ? 1 : 0;
}
#ifdef SED_SAVED_CAMERA_INTEGRATION_STANDALONE
int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    return runSavedCameraIntegrationTests();
}
#endif
