#include "main_window/common.hpp"
#include "ui/viewport/saved_cameras_dialog.hpp"

namespace sed
{
void MainWindow::resetSavedCameraWorkspace() noexcept
{
    savedCameras_.clear();
    ++savedCameraWorkspaceRevision_;
}

void MainWindow::showSavedCamerasDialog()
{
    if ( !glView_ )
    {
        return;
    }
    const QPointer<MainWindow> owner(this);
    const QPointer<GlView> view(glView_);
    const auto documentGeneration = documentLifetimeGeneration_;
    const auto resourceContext = resourceEditorDocumentContextGeneration_;
    const auto pipeline = view->renderPipeline();
    const auto revision = std::make_shared<std::uint64_t>(savedCameraWorkspaceRevision_);
    const auto current = [owner, view, documentGeneration, resourceContext, pipeline, revision]
    {
        return owner && view && owner->glView_ == view && owner->documentLifetimeGeneration_ == documentGeneration &&
               owner->resourceEditorDocumentContextGeneration_ == resourceContext &&
               owner->savedCameraWorkspaceRevision_ == *revision && view->renderPipeline() == pipeline;
    };
    ui::viewport::SavedCamerasDialog::Context context;
    context.cameras = savedCameras_;
    context.isCurrent = current;
    context.capture = [view] { return view->captureCameraPose(); };
    context.apply = [view, current](const app::CameraPose& pose, QString* error)
    {
        if ( !current() )
        {
            if ( error )
            {
                *error = QStringLiteral("The document or camera workspace changed.");
            }
            return false;
        }
        return view->applyCameraPose(pose, error);
    };
    context.store = [owner, current, revision](const std::vector<app::SavedCamera>& cameras, QString* error)
    {
        if ( !current() )
        {
            if ( error )
            {
                *error = QStringLiteral("The document or camera workspace changed.");
            }
            return false;
        }
        (void)app::savedCamerasToCbor(cameras);
        auto prepared = cameras;
        owner->savedCameras_.swap(prepared);
        *revision = ++owner->savedCameraWorkspaceRevision_;
        owner->noteRecoveryStateChanged(false);
        return true;
    };
    ui::viewport::SavedCamerasDialog dialog(std::move(context), this);
    dialog.exec();
}
} // namespace sed
