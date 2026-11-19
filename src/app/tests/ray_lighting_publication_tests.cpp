#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/level/build.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>

namespace sed::app_tests
{
namespace
{

bool writeMat(const QString& path, std::uint8_t red)
{
    assets::RgbaImage image{8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 255)};
    for ( std::size_t i = 0; i < image.rgba.size(); i += 4 )
    {
        image.rgba[i] = red;
    }
    const auto payload = assets::buildRgb565Mat({image}).bytes;
    QFile file(path);
    if ( !file.open(QIODevice::WriteOnly | QIODevice::Truncate) )
    {
        return false;
    }
    const auto size = static_cast<qint64>(payload.size());
    return file.write(reinterpret_cast<const char*>(payload.data()), size) == size && file.flush();
}

std::vector<ColorF> savedCorners(MainWindow& window)
{
    std::vector<ColorF> out;
    for ( const auto& sector : window.mutableDocumentStateForTest().level.sectors )
    {
        for ( const auto& surface : sector.surfaces )
        {
            for ( const auto& vertex : surface.texVertices )
            {
                out.push_back(vertex.color);
            }
        }
    }
    return out;
}

enum class PublicationChange
{
    Material,
    Lock,
    WaterTint
};

void checkPendingPublication(PublicationChange change)
{
    QTemporaryDir root;
    const QString matPath = root.filePath("publication.mat");
    if ( !root.isValid() || !writeMat(matPath, 255) )
    {
        check(false, "ray bake publication: write synthetic MAT");
        return;
    }

    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    editor::DocumentState state;
    state.level.layers.emplace_back("Layer0");
    level::levelCreateSectorPrimitiveBox(state.level, level::SectorPrimitive::Cube, {0, 0, 0}, {1, 1, 1});
    auto& sector = state.level.sectors.front();
    sector.layer = 0;
    if ( change == PublicationChange::WaterTint )
    {
        sector.flags |= SECF_Underwater;
        sector.tint = ColorWhite;
        auto& light = state.level.newLight();
        light.position = {.35, .35, .35};
        light.range = 2;
        light.color = ColorWhite;
        light.intensity = .5;
    }
    for ( auto& face : sector.surfaces )
    {
        face.material.clear();
        if ( face.normal.x < -0.9 )
        {
            face.material = "publication.mat";
            face.surfaceFlags |= SF_SkyHorizon;
        }
        for ( auto& vertex : face.texVertices )
        {
            vertex.color = {.23f, .23f, .23f, .23f};
        }
    }
    window.setDocumentStateForTest(state);
    window.addResourceRootForTest(std::filesystem::path(root.path().toStdU16String()));
    window.activateBakedLightEditModeForTest();
    const auto saved = savedCorners(window);
    const auto history = window.undoDepthForTest();
    auto* button = window.findChild<QToolButton*>(QStringLiteral("rayLightingBakeButton"));
    if ( !button )
    {
        check(false, "ray bake publication: find command");
        return;
    }
    button->click();
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("rayLightingBakeDialog"));
    auto* backend = dialog ? dialog->findChild<QComboBox*>(QStringLiteral("rayBake.backend")) : nullptr;
    auto* water = dialog ? dialog->findChild<QCheckBox*>(QStringLiteral("rayBake.waterAbsorption")) : nullptr;
    if ( !backend || !water )
    {
        check(false, "ray bake publication: select CPU calculation");
        return;
    }
    backend->setCurrentIndex(1);
    water->setChecked(change == PublicationChange::WaterTint);
    dialog->accept();
    auto* watcher = window.findChild<QFutureWatcherBase*>(QStringLiteral("rayLightingFutureWatcher"));
    if ( !watcher )
    {
        check(false, "ray bake publication: worker started");
        return;
    }

    // The mutation must happen after the worker finishes, before GUI delivery.
    QElapsedTimer timer;
    timer.start();
    while ( !watcher->isFinished() && timer.elapsed() < 15000 )
    {
        QThread::msleep(2);
    }
    if ( !watcher->isFinished() )
    {
        check(false, "ray bake publication: worker finished before input change");
        return;
    }
    switch ( change )
    {
    case PublicationChange::Material:
        if ( !writeMat(matPath, 0) )
        {
            check(false, "ray bake publication: replace loose MAT after worker completion");
            return;
        }
        break;
    case PublicationChange::Lock:
    {
        auto* lock = window.findChild<QAction*>(QStringLiteral("map.layer.0.lock"));
        if ( !lock || !lock->isEnabled() || lock->isChecked() )
        {
            check(false, "ray bake publication: targeted layer is editable before triggering Map Layer Locks");
            return;
        }
        lock->trigger();
        break;
    }
    case PublicationChange::WaterTint:
        window.mutableDocumentStateForTest().level.sectors.front().tint = {.2f, .6f, .8f, 1};
        break;
    }
    timer.restart();
    while ( window.lightBakeRunningForTest() && timer.elapsed() < 15000 )
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    const bool unchanged = !window.lightBakeRunningForTest() && window.undoDepthForTest() == history &&
                           savedCorners(window) == saved;
    switch ( change )
    {
    case PublicationChange::Material:
    {
        const QString message = window.statusBar()->currentMessage();
        check(message.contains(QStringLiteral("resource"), Qt::CaseInsensitive) &&
                  message.contains(QStringLiteral("chang"), Qt::CaseInsensitive),
              "ray bake publication: report the changed loose MAT");
        check(unchanged, "ray bake publication: stale loose MAT cannot change corners or append Undo");
        break;
    }
    case PublicationChange::Lock:
        check(unchanged, "ray bake publication: locking a target before delivery preserves corners and history");
        break;
    case PublicationChange::WaterTint:
        check(unchanged, "ray bake publication: changed underwater tint before delivery preserves corners and history");
        break;
    }
}

} // namespace

void runRayLightingPublicationTests()
{
    checkPendingPublication(PublicationChange::Material);
    checkPendingPublication(PublicationChange::Lock);
    checkPendingPublication(PublicationChange::WaterTint);
}

} // namespace sed::app_tests
