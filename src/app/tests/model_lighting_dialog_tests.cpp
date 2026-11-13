#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "sed/assets/model_lighting.hpp"
#include "../../../tools/sed-agent/tests/model_lighting_fixture.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

namespace sed::app_tests
{
namespace
{

QByteArray readModelLightingFixture(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

} // namespace

void runModelLightingDialogTests()
{
    // Regresses missing GUI access and accidental source overwrite/stale saves.
    // Expected ambient-only values are analytic, not computed by the baker.
    QTemporaryDir scratch;
    check(scratch.isValid(), "model lighting GUI: isolated scratch exists");
    if ( !scratch.isValid() )
    {
        return;
    }
    const QString source = scratch.filePath(QStringLiteral("original.3do"));
    const QString output = scratch.filePath(QStringLiteral("variant.3do"));
    const QString staleOutput = scratch.filePath(QStringLiteral("stale.3do"));
    const QByteArray original(kModelLightingFixture);
    {
        QFile file(source);
        check(file.open(QIODevice::WriteOnly), "model lighting GUI: synthetic source opens");
        check(file.write(original) == original.size(), "model lighting GUI: source is complete");
    }

    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    editor::DocumentState state;
    state.level.kind = ProjectType::JKDF2;
    window.setDocumentStateForTest(std::move(state));
    const auto undoDepth = window.undoDepthForTest();
    const auto title = window.windowTitle();
    QAction* const action = window.findChild<QAction*>(QStringLiteral("tools.model_lighting"));
    check(action != nullptr, "model lighting GUI: Tools exposes ordinary model lighting");
    if ( !action )
    {
        return;
    }

    int stage = 0;
    bool controlsPresent = false;
    bool sourcePreserved = false;
    bool invalidated = false;
    bool existingPreserved = false;
    bool staleRefused = false;
    bool inspected = false;
    bool fullOptionsPresent = false;
    bool profileRefused = false;
    QByteArray firstOutput;
    QTimer driver;
    driver.setInterval(5);
    QObject::connect(&driver, &QTimer::timeout, [&]()
    {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if ( !dialog || dialog->objectName() != QStringLiteral("modelLightingDialog") )
        {
            return;
        }
        auto* sourceEdit = dialog->findChild<QLineEdit*>(QStringLiteral("modelLightingSource"));
        auto* outputEdit = dialog->findChild<QLineEdit*>(QStringLiteral("modelLightingOutput"));
        auto* load = dialog->findChild<QPushButton*>(QStringLiteral("modelLightingLoad"));
        auto* calculate = dialog->findChild<QPushButton*>(QStringLiteral("modelLightingCalculate"));
        auto* save = dialog->findChild<QPushButton*>(QStringLiteral("modelLightingSave"));
        auto* red = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("modelLightingAmbientRed"));
        auto* green = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("modelLightingAmbientGreen"));
        auto* blue = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("modelLightingAmbientBlue"));
        controlsPresent = sourceEdit && outputEdit && load && calculate && save && red && green && blue;
        if ( !controlsPresent )
        {
            dialog->reject();
            return;
        }
        if ( stage == 0 )
        {
            sourceEdit->setText(source);
            load->click();
            stage = 1;
        }
        else if ( stage == 1 && calculate->isEnabled() )
        {
            auto* profile = dialog->findChild<QComboBox*>(QStringLiteral("modelLightingProfile"));
            auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("modelLightingBlend"));
            auto* lights = dialog->findChild<QTableWidget*>(QStringLiteral("modelLightingLights"));
            auto* occlusion = dialog->findChild<QCheckBox*>(QStringLiteral("modelLightingOcclusion"));
            auto* bias = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("modelLightingShadowBias"));
            auto* lod = dialog->findChild<QSpinBox*>(QStringLiteral("modelLightingPreviewLod"));
            fullOptionsPresent = profile && profile->count() == 3 && blend && blend->count() == 2
                && lights && lights->columnCount() == 9 && occlusion && bias
                && bias->minimum() == 1e-12 && lod;
            if ( !fullOptionsPresent )
            {
                dialog->reject();
                return;
            }
            profile->setCurrentIndex(2);
            calculate->click();
            stage = 10;
        }
        else if ( stage == 10 && calculate->isEnabled() )
        {
            auto* status = dialog->findChild<QLabel*>(QStringLiteral("modelLightingStatus"));
            profileRefused = !save->isEnabled() && status && !status->text().isEmpty();
            dialog->findChild<QComboBox*>(QStringLiteral("modelLightingProfile"))->setCurrentIndex(0);
            red->setValue(0.5);
            green->setValue(0.5);
            blue->setValue(0.5);
            calculate->click();
            stage = 2;
        }
        else if ( stage == 2 && save->isEnabled() )
        {
            outputEdit->setText(output);
            save->click();
            firstOutput = readModelLightingFixture(output);
            sourcePreserved = readModelLightingFixture(source) == original;
            if ( !firstOutput.isEmpty() )
            {
                try
                {
                    const auto bytes = std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(firstOutput.constData()),
                        static_cast<std::size_t>(firstOutput.size()));
                    const auto result = assets::inspectModelLighting(bytes);
                    inspected = result.vertices == 3 && result.meshes.size() == 1
                        && close(result.meshes[0].minimumVertexLight.r, 0.5)
                        && close(result.meshes[0].maximumVertexLight.r, 0.5)
                        && firstOutput.contains("# first alpha")
                        && firstOutput.contains("0.375")
                        && firstOutput.contains("do not change this opaque record");
                }
                catch ( const std::exception& )
                {
                    inspected = false;
                }
            }
            red->setValue(0.75);
            invalidated = !save->isEnabled();
            green->setValue(0.75);
            blue->setValue(0.75);
            calculate->click();
            stage = 3;
        }
        else if ( stage == 3 && save->isEnabled() )
        {
            save->click();
            existingPreserved = readModelLightingFixture(output) == firstOutput;
            QFile file(source);
            if ( file.open(QIODevice::Append) )
            {
                file.write("\n# external change\n");
                file.close();
            }
            outputEdit->setText(staleOutput);
            save->click();
            staleRefused = !QFile::exists(staleOutput);
            stage = 4;
            dialog->reject();
        }
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, [&]()
    {
        if ( auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()) )
        {
            dialog->reject();
        }
    });
    deadline.start(10000);
    driver.start();
    action->trigger();
    driver.stop();
    deadline.stop();
    check(controlsPresent, "model lighting GUI: source, bake options and fresh-variant save are human controls");
    check(stage == 4, "model lighting GUI: inspection and background bake complete within the test deadline");
    check(sourcePreserved && inspected, "model lighting GUI: ambient bake is analytic and preserves source/alpha/opaque data");
    check(fullOptionsPresent, "model lighting GUI: profile, blend, ordered lights, self-occlusion, bias and LOD are human controls");
    check(profileRefused, "model lighting GUI: an incompatible IJIM recipe refuses without a saveable result");
    check(invalidated, "model lighting GUI: editing options invalidates the reviewed output");
    check(existingPreserved, "model lighting GUI: saving refuses an existing variant");
    check(staleRefused, "model lighting GUI: source drift prevents fresh variant publication");
    check(window.undoDepthForTest() == undoDepth && window.windowTitle() == title,
        "model lighting GUI: independent asset authoring does not edit or dirty the level");

    // Close immediately after starting a real worker. No timer delay is used
    // as evidence of cancellation: the invariant is no publication or document edit.
    int cancelStage = 0;
    const QByteArray cancellationBaseline = readModelLightingFixture(source);
    QTimer cancelDriver;
    cancelDriver.setInterval(5);
    QObject::connect(&cancelDriver, &QTimer::timeout, [&]()
    {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if ( !dialog || dialog->objectName() != QStringLiteral("modelLightingDialog") )
        {
            return;
        }
        auto* sourceEdit = dialog->findChild<QLineEdit*>(QStringLiteral("modelLightingSource"));
        auto* load = dialog->findChild<QPushButton*>(QStringLiteral("modelLightingLoad"));
        auto* calculate = dialog->findChild<QPushButton*>(QStringLiteral("modelLightingCalculate"));
        if ( !sourceEdit || !load || !calculate )
        {
            dialog->reject();
            return;
        }
        if ( cancelStage == 0 )
        {
            sourceEdit->setText(source);
            load->click();
            cancelStage = 1;
        }
        else if ( cancelStage == 1 && calculate->isEnabled() )
        {
            calculate->click();
            cancelStage = 2;
            dialog->reject();
        }
    });
    deadline.start(10000);
    cancelDriver.start();
    action->trigger();
    cancelDriver.stop();
    deadline.stop();
    check(cancelStage == 2, "model lighting GUI: a real calculation can be cancelled by closing its owner");
    check(readModelLightingFixture(source) == cancellationBaseline
            && readModelLightingFixture(output) == firstOutput
            && !QFile::exists(staleOutput),
        "model lighting GUI: cancelled worker never publishes an asset or changes existing bytes");
}

} // namespace sed::app_tests
