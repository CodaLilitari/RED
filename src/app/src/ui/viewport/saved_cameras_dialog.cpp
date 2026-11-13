#include "saved_cameras_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <array>
#include <stdexcept>

namespace sed::ui::viewport
{
class SavedCamerasDialog::Impl
{
public:
    Impl(SavedCamerasDialog& dialog, Context context);

private:
    bool current();
    void capture();
    void populate(const app::CameraPose& pose);
    app::CameraPose formPose() const;
    void apply(bool recall);
    void mutate(int operation);
    void refresh(int selected);
    void report(const QString& text);
    SavedCamerasDialog& dialog_;
    Context context_;
    app::CameraPose pose_;
    QListWidget* list_{};
    QLineEdit* name_{};
    QLabel* status_{};
    QComboBox* projection_{};
    QCheckBox* clip_{};
    std::array<QDoubleSpinBox*, 8> orbit_{};
    QDoubleSpinBox *fov_{}, *scale_{}, *near_{}, *far_{};
    bool valid_{};
};
SavedCamerasDialog::Impl::Impl(SavedCamerasDialog& dialog, Context context)
    : dialog_(dialog), context_(std::move(context))
{
    dialog_.setWindowTitle("Camera pose and saved viewpoints");
    dialog_.resize(QSize(850, 640).boundedTo(dialog_.screen()->availableGeometry().size() - QSize(40, 80)));
    auto* layout = new QVBoxLayout(&dialog_);
    auto* warning = new QLabel(
        "Editor workspace viewpoints only: no Level, spawn, selection or undo changes. XYZ is the eye position in "
        "native map units (X right, Y forward, Z up). Angles are orbit degrees: yaw 0 looks +Y; positive pitch looks "
        "down. "
        "Apply/Recall navigates explicitly; saving a name stores the form without navigating.",
        &dialog_);
    warning->setWordWrap(true);
    warning->setObjectName("savedCameraWarning");
    layout->addWidget(warning);
    list_ = new QListWidget(&dialog_);
    list_->setObjectName("savedCameraList");
    list_->setMaximumHeight(100);
    list_->setMinimumHeight(50);
    layout->addWidget(list_);
    auto makeButton = [&](const char* label, const char* objectName, QHBoxLayout* row, auto action)
    {
        auto* button = new QPushButton(QString::fromUtf8(label), &dialog_);
        button->setObjectName(objectName);
        row->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, &dialog_, action);
        return button;
    };
    auto* names = new QHBoxLayout;
    name_ = new QLineEdit(&dialog_);
    name_->setObjectName("savedCameraName");
    name_->setMaxLength(4096);
    name_->setPlaceholderText("Viewpoint name");
    names->addWidget(name_, 1);
    makeButton("Save new", "savedCameraAdd", names, [this] { mutate(0); });
    makeButton("Replace pose", "savedCameraReplace", names, [this] { mutate(1); });
    makeButton("Rename", "savedCameraRename", names, [this] { mutate(2); });
    makeButton("Delete", "savedCameraDelete", names, [this] { mutate(3); });
    layout->addLayout(names);
    auto* scroll = new QScrollArea(&dialog_);
    scroll->setObjectName("savedCameraScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* fields = new QWidget(scroll);
    auto* form = new QFormLayout(fields);
    auto real = [&](const char* label, const char* objectName, double low, double high)
    {
        auto* spin = new QDoubleSpinBox(fields);
        spin->setObjectName(objectName);
        spin->setDecimals(17);
        spin->setRange(low, high);
        form->addRow(QString::fromUtf8(label), spin);
        return spin;
    };
    const std::array<const char*, 8> labels{ "Eye X", "Eye Y", "Eye Z", "Orbit pitch (degrees)", "Orbit yaw (degrees)",
        "Orbit roll (degrees)", "Orbit distance", "Orbit bound radius" };
    const std::array<const char*, 8> namesForOrbit{ "savedCameraX", "savedCameraY", "savedCameraZ", "savedCameraPitch",
        "savedCameraYaw", "savedCameraRoll", "savedCameraDistance", "savedCameraRadius" };
    for ( std::size_t i = 0; i < orbit_.size(); ++i )
    {
        orbit_[i] = real(labels[i], namesForOrbit[i], i >= 6 ? 0.0 : -1.0e9, 1.0e9);
    }
    orbit_[3]->setRange(-180.0, 180.0);
    orbit_[4]->setRange(-1.0e6, 1.0e6);
    orbit_[5]->setRange(-1.0e6, 1.0e6);
    projection_ = new QComboBox(fields);
    projection_->setObjectName("savedCameraProjection");
    projection_->addItem("Perspective", 0);
    projection_->addItem("Orthographic", 1);
    form->addRow("Projection", projection_);
    fov_ = real("Horizontal FOV (degrees)", "savedCameraFov", 1.0, 179.0);
    scale_ = real("Orthographic scale", "savedCameraScale", 0.01, 1.0e6);
    near_ = real("Near plane", "savedCameraNear", -1.0e9, 1.0e9);
    far_ = real("Far plane", "savedCameraFar", -1.0e9, 1.0e9);
    clip_ = new QCheckBox("Enable clipping", fields);
    clip_->setObjectName("savedCameraClip");
    form->addRow(clip_);
    auto* scaleNote =
        new QLabel("Orthographic recall preserves horizontal world units per logical pixel. "
                   "Fractional display scaling can slightly change vertical scale through framebuffer rounding.",
            fields);
    scaleNote->setWordWrap(true);
    form->addRow(scaleNote);
    scroll->setWidget(fields);
    layout->addWidget(scroll, 1);
    status_ = new QLabel(&dialog_);
    status_->setObjectName("savedCameraStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    auto* actions = new QHBoxLayout;
    makeButton("Capture current", "savedCameraCapture", actions, [this] { capture(); });
    makeButton("Apply form", "savedCameraApply", actions, [this] { apply(false); });
    makeButton("Recall selected", "savedCameraRecall", actions, [this] { apply(true); });
    actions->addStretch();
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog_);
    actions->addWidget(close);
    layout->addLayout(actions);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog_, &QDialog::reject);
    QObject::connect(list_, &QListWidget::currentRowChanged, &dialog_,
        [this](int index)
        {
            if ( index >= 0 && static_cast<std::size_t>(index) < context_.cameras.size() )
            {
                const auto& selected = context_.cameras[static_cast<std::size_t>(index)];
                name_->setText(selected.name);
                populate(selected.pose);
                report("Selected viewpoint loaded into the form. Recall or Apply navigates; selection alone does not.");
            }
        });
    try
    {
        (void)app::savedCamerasToCbor(context_.cameras);
        valid_ = context_.capture && context_.apply && context_.store && context_.isCurrent;
        if ( !valid_ )
        {
            report("Camera workspace callbacks are unavailable.");
            return;
        }
        refresh(-1);
        capture();
    }
    catch ( const std::exception& error )
    {
        report(QString::fromUtf8(error.what()));
    }
}

void SavedCamerasDialog::Impl::report(const QString& text)
{
    status_->setText(text);
}
bool SavedCamerasDialog::Impl::current()
{
    if ( !valid_ || !context_.isCurrent || !context_.isCurrent() )
    {
        report("The document or camera workspace changed. Close and reopen this dialog.");
        return false;
    }
    return true;
}
void SavedCamerasDialog::Impl::populate(const app::CameraPose& pose)
{
    pose_ = pose;
    const auto eye =
        pose.orbit.target() +
        app::ViewportCamera::SphericalDirection(pose.orbit.yawDeg, pose.orbit.pitchDeg) * pose.orbit.distance;
    const std::array<double, 8> values{ eye.x(), eye.y(), eye.z(), pose.orbit.pitchDeg, pose.orbit.yawDeg,
        pose.orbit.rollDeg, pose.orbit.distance, pose.orbit.radius };
    for ( std::size_t i = 0; i < orbit_.size(); ++i )
    {
        orbit_[i]->setValue(values[i]);
    }
    projection_->setCurrentIndex(pose.projection == app::ViewportCamera::ProjectionType::Orthographic ? 1 : 0);
    fov_->setValue(pose.fovXDeg);
    scale_->setValue(pose.orthoScale);
    near_->setValue(pose.nearPlane);
    far_->setValue(pose.farPlane);
    clip_->setChecked(pose.clipEnabled);
}
app::CameraPose SavedCamerasDialog::Impl::formPose() const
{
    auto result = pose_;
    const auto oldEye =
        pose_.orbit.target() +
        app::ViewportCamera::SphericalDirection(pose_.orbit.yawDeg, pose_.orbit.pitchDeg) * pose_.orbit.distance;
    const QVector3D eye(static_cast<float>(orbit_[0]->value()), static_cast<float>(orbit_[1]->value()),
        static_cast<float>(orbit_[2]->value()));
    result.orbit.pitchDeg = static_cast<float>(orbit_[3]->value());
    result.orbit.yawDeg = static_cast<float>(orbit_[4]->value());
    result.orbit.rollDeg = static_cast<float>(orbit_[5]->value());
    result.orbit.distance = static_cast<float>(orbit_[6]->value());
    result.orbit.radius = static_cast<float>(orbit_[7]->value());
    if ( eye != oldEye || result.orbit.pitchDeg != pose_.orbit.pitchDeg || result.orbit.yawDeg != pose_.orbit.yawDeg ||
         result.orbit.distance != pose_.orbit.distance )
    {
        result.orbit.center =
            eye -
            app::ViewportCamera::SphericalDirection(result.orbit.yawDeg, result.orbit.pitchDeg) * result.orbit.distance;
        result.orbit.pan = {};
    }
    result.projection = projection_->currentIndex() == 1 ? app::ViewportCamera::ProjectionType::Orthographic
                                                         : app::ViewportCamera::ProjectionType::Perspective;
    result.fovXDeg = static_cast<float>(fov_->value());
    result.orthoScale = static_cast<float>(scale_->value());
    result.nearPlane = static_cast<float>(near_->value());
    result.farPlane = static_cast<float>(far_->value());
    result.clipEnabled = clip_->isChecked();
    return result;
}
void SavedCamerasDialog::Impl::capture()
{
    if ( !current() )
    {
        return;
    }
    try
    {
        const auto candidate = context_.capture();
        QString error;
        if ( !app::cameraPoseIsValid(candidate, &error) )
        {
            report(error);
            return;
        }
        populate(candidate);
        report("Current camera captured. Changes stay in this form until Apply or a named-viewpoint action.");
    }
    catch ( const std::exception& error )
    {
        report(QString::fromUtf8(error.what()));
    }
}
void SavedCamerasDialog::Impl::apply(bool recall)
{
    if ( !current() )
    {
        return;
    }
    try
    {
        const int row = list_->currentRow();
        if ( recall && (row < 0 || static_cast<std::size_t>(row) >= context_.cameras.size()) )
        {
            report("Select a named viewpoint to recall.");
            return;
        }
        const auto candidate = recall ? context_.cameras[static_cast<std::size_t>(row)].pose : formPose();
        QString error;
        if ( !app::cameraPoseIsValid(candidate, &error) || !context_.apply(candidate, &error) )
        {
            report(error.isEmpty() ? "Camera restoration was refused." : error);
            return;
        }
        capture();
        if ( current() )
        {
            report("Camera restored. Level content, selection and undo are unchanged.");
        }
    }
    catch ( const std::exception& error )
    {
        report(QString::fromUtf8(error.what()));
    }
}
void SavedCamerasDialog::Impl::refresh(int selected)
{
    const QSignalBlocker blocker(list_);
    list_->clear();
    for ( const auto& camera : context_.cameras )
    {
        list_->addItem(camera.name);
    }
    list_->setCurrentRow(selected);
    if ( selected >= 0 && static_cast<std::size_t>(selected) < context_.cameras.size() )
    {
        name_->setText(context_.cameras[static_cast<std::size_t>(selected)].name);
    }
}
void SavedCamerasDialog::Impl::mutate(int operation)
{
    if ( !current() )
    {
        return;
    }
    try
    {
        int row = list_->currentRow();
        if ( operation != 0 && (row < 0 || static_cast<std::size_t>(row) >= context_.cameras.size()) )
        {
            report("Select a named viewpoint first.");
            return;
        }
        auto candidate = context_.cameras;
        QString error;
        if ( operation == 0 || operation == 2 )
        {
            const QString ignored = operation == 2 ? candidate[static_cast<std::size_t>(row)].name : QString{};
            if ( !app::validateSavedCameraName(name_->text(), candidate, ignored, &error) )
            {
                report(error);
                return;
            }
        }
        if ( operation == 0 )
        {
            if ( candidate.size() >= app::kMaximumSavedCameras )
            {
                report("A workspace can store at most 256 named viewpoints.");
                return;
            }
            candidate.push_back({ app::normalizedSavedCameraName(name_->text()), formPose() });
            row = static_cast<int>(candidate.size()) - 1;
        }
        else if ( operation == 1 )
        {
            candidate[static_cast<std::size_t>(row)].pose = formPose();
        }
        else if ( operation == 2 )
        {
            candidate[static_cast<std::size_t>(row)].name = app::normalizedSavedCameraName(name_->text());
        }
        else
        {
            candidate.erase(candidate.begin() + row);
            row = std::min(row, static_cast<int>(candidate.size()) - 1);
        }
        (void)app::savedCamerasToCbor(candidate);
        if ( !context_.store(candidate, &error) )
        {
            report(error.isEmpty() ? "Workspace update was refused." : error);
            return;
        }
        context_.cameras = std::move(candidate);
        refresh(row);
        report("Named viewpoints updated in the editor workspace; native Save or recovery retains them.");
    }
    catch ( const std::exception& error )
    {
        report(QString::fromUtf8(error.what()));
    }
}
SavedCamerasDialog::SavedCamerasDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}
SavedCamerasDialog::~SavedCamerasDialog() = default;
} // namespace sed::ui::viewport
