#include "gl_view.hpp"
#include "viewport/saved_camera.hpp"

#include <QThread>
#include <algorithm>

namespace sed
{
app::CameraPose GlView::captureCameraPose() const
{
    app::CameraPose pose;
    pose.orbit = camera_.orbit;
    pose.projection = camera_.projectionType();
    pose.fovXDeg = camera_.fovXDeg();
    pose.orthoScale = camera_.orthoScale();
    pose.nearPlane = camera_.nearPlane();
    pose.farPlane = camera_.farPlane();
    pose.clipEnabled = camera_.clipEnabled();
    pose.logicalSize = QSize(std::max(1, width()), std::max(1, height()));
    pose.devicePixelRatio = devicePixelRatioF();
    return pose;
}

bool GlView::applyCameraPose(const app::CameraPose& pose, QString* error)
{
    if ( QThread::currentThread() != thread() )
    {
        if ( error )
        {
            *error = QStringLiteral("Camera restoration must run on the viewport thread.");
        }
        return false;
    }
    auto candidate = camera_;
    if ( !app::applyCameraPoseToCamera(
             candidate, pose, QSize(std::max(1, width()), std::max(1, height())), devicePixelRatioF(), error) )
    {
        return false;
    }
    cancelViewAnimation();
    camera_ = std::move(candidate);
    markViewChanged();
    Q_EMIT viewportStateChanged();
    update();
    return true;
}
} // namespace sed
