#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "sed/level/build.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>

namespace sed::app_tests
{
namespace
{

editor::DocumentState skyRoom()
{
    editor::DocumentState state{};
    state.level.layers.emplace_back("Layer0");
    level::levelCreateSectorPrimitiveBox(state.level, level::SectorPrimitive::Cube,
        { 0, 0, 0 }, { 1, 1, 1 });
    Sector& sector = state.level.sectors[0];
    sector.layer = 0;
    sector.ambient = { 0.27f, 0.27f, 0.27f, 0.27f };
    for ( Surface& surface : sector.surfaces )
    {
        // This synthetic room deliberately has no external asset roots.
        surface.material.clear();
        if ( surface.normal.x < -0.9 )
        {
            surface.surfaceFlags |= SF_SkyHorizon;
            surface.light = 0;
        }
        for ( auto& vertex : surface.texVertices )
        {
            vertex.color = { 0.19f, 0.19f, 0.19f, 0.19f };
        }
    }
    return state;
}

std::vector<float> colors(const MainWindow& window)
{
    std::vector<float> result{};
    for ( const Sector& sector : window.documentStateForTest().level.sectors )
    {
        for ( const Surface& surface : sector.surfaces )
        {
            for ( const TexVertex& vertex : surface.texVertices )
            {
                result.insert(result.end(), { vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a });
            }
        }
    }
    return result;
}

QDialog* openDialog(MainWindow& window)
{
    auto* button = window.findChild<QToolButton*>(QStringLiteral("rayLightingBakeButton"));
    check(button != nullptr, "ray bake: Light strip exposes the opt-in manual ray bake");
    if ( !button )
    {
        return nullptr;
    }
    button->click();
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("rayLightingBakeDialog"));
    check(dialog != nullptr, "ray bake: opens settings before starting work");
    return dialog;
}

bool acceptCpuBake(QDialog* dialog)
{
    if ( !dialog )
    {
        return false;
    }
    auto* backend = dialog->findChild<QComboBox*>(QStringLiteral("rayBake.backend"));
    auto* samples = dialog->findChild<QSpinBox*>(QStringLiteral("rayBake.samples"));
    auto* bounces = dialog->findChild<QSpinBox*>(QStringLiteral("rayBake.bounces"));
    auto* sky = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("rayBake.skyIntensity"));
    check(backend && samples && bounces && sky, "ray bake: exposes backend, samples, bounce count and sky light");
    if ( !backend || !samples || !bounces || !sky )
    {
        return false;
    }
    backend->setCurrentIndex(1);
    samples->setValue(32);
    bounces->setValue(2);
    sky->setValue(1);
    dialog->accept();
    return true;
}

void wait(MainWindow& window)
{
    QElapsedTimer timer{};
    timer.start();
    while ( window.lightBakeRunningForTest() && timer.elapsed() < 15000 )
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    check(!window.lightBakeRunningForTest(), "ray bake: worker finishes without blocking the GUI event loop");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

} // namespace

void runRayLightingDialogTests()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setDocumentStateForTest(skyRoom());
    window.activateBakedLightEditModeForTest();
    const auto original = colors(window);
    const auto history = window.undoDepthForTest();
    QPointer<QDialog> dialog = openDialog(window);
    if ( !dialog )
    {
        return;
    }
    check(openDialog(window) == dialog, "ray bake: one options dialog per document window");
    dialog->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(colors(window) == original && window.undoDepthForTest() == history,
        "ray bake: canceling settings changes no level or history");

    if ( !acceptCpuBake(openDialog(window)) )
    {
        return;
    }
    wait(window);
    check(colors(window) != original && window.undoDepthForTest() == history + 1,
        "ray bake: publishes new surface lighting as exactly one undo entry");
    check(window.documentStateForTest().level.sectors[0].ambient.r == 0.27f,
        "ray bake: preserves authored sector ambient");
    check(window.statusBar()->currentMessage().contains(QStringLiteral("CPU")),
        "ray bake: completion identifies the actual CPU backend");
    check(window.undoDocumentForTest() && colors(window) == original,
        "ray bake: one Undo restores all original corner channels");

    window.setDocumentStateForTest(skyRoom());
    const auto beforeStale = colors(window);
    const auto historyBeforeStale = window.undoDepthForTest();
    if ( acceptCpuBake(openDialog(window)) )
    {
        window.mutableDocumentStateForTest().level.sectors[0].vertices[0].x += 0.125;
        wait(window);
        check(colors(window) == beforeStale && window.undoDepthForTest() == historyBeforeStale,
            "ray bake: a geometry edit during work rejects the whole stale result");
    }

    window.setDocumentStateForTest(skyRoom());
    const auto beforeStop = colors(window);
    const auto historyBeforeStop = window.undoDepthForTest();
    if ( acceptCpuBake(openDialog(window)) )
    {
        auto* stop = window.findChild<QPushButton*>(QStringLiteral("lightBakeStopButton"));
        check(stop != nullptr, "ray bake: reuses the existing Stop control");
        if ( stop )
        {
            stop->click();
        }
        wait(window);
        check(colors(window) == beforeStop && window.undoDepthForTest() == historyBeforeStop,
            "ray bake: cancellation publishes nothing and adds no history");
    }
}

} // namespace sed::app_tests
