#include "test_common.hpp"
#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "app_settings.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <QToolButton>
#include <QTimer>
#include <iostream>

namespace sed::app_tests
{
// Opt-in because the engine and its local stock installation are supplied by the tester.
void runRelayEditorWorkflow(const QString &directory, const QString &machine)
{
    auto settings = settingsStore();
    settings.setValue("resources/jkdf2/gobs", QStringList{directory + "/relay-art.gob"});
    settings.sync();
    MainWindow window;
    window.show();
    window.openLevelFromFilenameForTest(directory + "/redlift.jkl");
    auto &state = window.mutableDocumentStateForTest();
    if (state.level.sectors.size() != 2 || state.level.cogs.size() != 1)
    {
        check(false, "open the original two-room relay project");
        return;
    }
    window.activateSurfaceEditModeForTest();
    window.activateSurfaceFaceSubModeForTest();
    state.selection.clear();
    state.selection.surfaces.insert({0, 0});
    window.glViewForTest()->setSelectedSurface(0, 0);
    window.rebuildPropertiesForTest();
    auto *material = window.findChild<QLineEdit *>("surface.materialName");
    if (!material || material->text() != "rlwall.mat")
    {
        check(false, "Properties identifies the floor material");
        return;
    }
    material->setText("rlmark.mat");
    QMetaObject::invokeMethod(material, "editingFinished", Qt::DirectConnection);
    if (window.documentStateForTest().level.sectors[0].surfaces[0].material != "rlmark.mat")
    {
        check(false, "Properties applies the marked floor material");
        return;
    }

    const auto wait = [](auto predicate, int limit) {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!predicate() && elapsed.elapsed() < limit)
        {
            QApplication::processEvents(QEventLoop::AllEvents, 25);
            QThread::msleep(10);
        }
        return predicate();
    };
    auto *light = window.findChild<QAction *>("sector.place_lights");
    window.activateSectorEditModeForTest();
    auto &roomSelection = window.mutableDocumentStateForTest().selection;
    roomSelection.clear();
    roomSelection.sectors.insert(0);
    window.glViewForTest()->setSelectedSector(0);
    window.updateEditActionsForTest();
    if (!light || !light->isEnabled())
    {
        check(false, "sector light tool is available");
        return;
    }
    QTimer::singleShot(0, [] {
        if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))
        {
            input->setDoubleValue(0.75);
            input->accept();
        }
    });
    light->trigger();
    window.activateBakedLightEditModeForTest();
    auto *bake = window.findChild<QToolButton *>("lightBakeRecalculateButton");
    if (!bake || !bake->isEnabled())
    {
        check(false, "regular vertex lighting is available");
        return;
    }
    const auto beforeBake = window.undoDepthForTest();
    bake->click();
    if (!wait([&] { return !window.lightBakeRunningForTest() && window.undoDepthForTest() > beforeBake; }, 30000))
    {
        check(false, "completed vertex bake creates an undo step");
        return;
    }

    auto *cockpit = window.findChild<QAction *>("tools.project_cockpit");
    cockpit->trigger();
    auto *dialog = window.findChild<QDialog *>("projectCockpit");
    if (!dialog)
    {
        check(false, "open Project Cockpit");
        return;
    }
    auto *profile = dialog->findChild<QLineEdit *>("projectProfilePath");
    profile->setText(directory + "/red-project.json");
    dialog->findChild<QLineEdit *>("projectMachineRequest")->setText(machine);
    auto *test = dialog->findChild<QPushButton *>("projectRunTest");
    auto *status = dialog->findChild<QLabel *>("projectStatus");
    dialog->findChild<QPushButton *>("prepareProject")->click();
    if (!wait([&] { return test->isEnabled(); }, 30000))
    {
        check(false, qPrintable("Prepare: " + status->text()));
        return;
    }
    bool reviewed = false;
    QString unexpected;
    QTimer modals;
    QObject::connect(&modals, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets())
        {
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (!box || !box->isVisible())
                continue;
            if (box->windowTitle() == "Review test inputs")
            {
                reviewed = true;
                box->button(QMessageBox::Ok)->click();
            }
            else
            {
                unexpected = box->windowTitle() + " " + box->text();
                box->reject();
            }
        }
    });
    modals.start(25);
    test->click();
    const bool ran =
        wait([&] { return status->text().endsWith("project-test.json") || !unexpected.isEmpty(); }, 125000);
    modals.stop();
    if (!reviewed || !ran || !unexpected.isEmpty())
    {
        check(false, qPrintable("Project Test: " + status->text() + " " + unexpected));
        return;
    }
    const auto path = status->text().section(QStringLiteral(" · "), -1);
    QFile bytes(path);
    if (!bytes.open(QIODevice::ReadOnly))
    {
        check(false, "read the project test receipt");
        return;
    }
    const auto report = QJsonDocument::fromJson(bytes.readAll()).object();
    check(report["status"].toString() == "passed" && report["editorCurrentAtReview"].toBool() &&
              report["projectCurrentAtReview"].toBool(),
          "the exact tested revision is still current on return");
    const auto captures = report["gameplay"].toObject()["captures"].toArray();
    QStringList points;
    for (const auto &capture : captures)
        points.append(capture.toObject()["step"].toString());
    check(points == QStringList{"spawn", "closed-door", "on-lift", "upper-stop"},
          "observed all four door/lift checkpoints");
    std::cout << "Editor project receipt: " << path.toStdString() << '\n';
    for (auto *child : dialog->findChildren<QDialog *>())
        child->close();
    dialog->close();
    const auto depth = window.undoDepthForTest();
    auto *undo = window.findChild<QAction *>("edit.undo");
    undo->trigger();
    check(window.undoDepthForTest() + 1 == depth &&
              window.documentStateForTest().level.sectors[0].surfaces[0].material == "rlmark.mat",
          "return from playtest and undo the bake in the same edited level");
}
} // namespace sed::app_tests
