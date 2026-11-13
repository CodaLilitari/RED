#include "main_window/common.hpp"

namespace sed
{
bool MainWindow::setInstallationCheckFrame(const unsigned frame, QString* error)
{
    auto* view = installationCheckViewport();
    if ( !view || frame > 1 )
    {
        if ( error )
        {
            *error = QStringLiteral("Synthetic installation frames require their isolated diagnostic window and frame 0 or 1.");
        }
        return false;
    }
    Level level;
    level.kind = ProjectType::MOTS;
    level.layers = {"Installation check"};
    const double center = frame == 0 ? -2.0 : 2.0;
    Sector sector;
    sector.ambient = ColorWhite;
    sector.vertices = {{center - 0.6, -0.6, 0.0}, {center + 0.6, -0.6, 0.0},
        {center + 0.6, 0.6, 0.0}, {center - 0.6, 0.6, 0.0}};
    Surface face;
    face.geo = static_cast<std::int32_t>(TGeoMode::Solid);
    face.faceFlags = FF_DoubleSided;
    face.normal = {0.0, 0.0, 1.0};
    face.vertices = {0, 1, 2, 3};
    face.texVertices.resize(4);
    for ( auto& vertex : face.texVertices )
    {
        vertex.color = ColorWhite;
    }
    sector.surfaces.push_back(std::move(face));
    level.sectors.push_back(std::move(sector));
    document_.level() = std::move(level);
    pendingEditorViewRefitAfterResize_ = false;
    if ( editorViewRefitTimer_ )
    {
        editorViewRefitTimer_->stop();
    }
    view->setBackgroundClearColor(Qt::black);
    view->setShowGrid(false);
    view->setShowWireframe(false);
    view->setShowVertices(false);
    view->setShowSolid(true);
    view->setShowTextures(false);
    view->setShowFog(false);
    view->setShowThings(false);
    view->setShowThingGizmos(false);
    view->setShowAxisWidget(false);
    view->setShowToastOverlays(false);
    view->setShowEditorCubes(false);
    view->setLightingMode(GlView::LightingMode::FullyLit);
    view->setLevel(&document_.level());
    view->notifyLevelGeometryChanged();
    if ( frame == 0 )
    {
        view->setCameraProjectionType(app::ViewportCamera::ProjectionType::Orthographic);
        view->frameWorldBounds({-4.0f, -3.0f, -1.0f}, {4.0f, 3.0f, 1.0f}, false);
        view->setOrbitViewImmediate(0.0f, 90.0f, 0.0f);
    }
    if ( error )
    {
        error->clear();
    }
    return true;
}
}
