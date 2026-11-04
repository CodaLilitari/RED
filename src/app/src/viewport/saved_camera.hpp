#pragma once

#include <optional>
#include <vector>

#include <QCborValue>
#include <QJsonObject>
#include <QSize>
#include <QSizeF>
#include <QString>

#include "viewport_camera.hpp"

namespace sed::app
{

inline constexpr int kMaximumSavedCameras = 256;
inline constexpr int kMaximumSavedCameraNameLength = 128;
inline constexpr int kSavedCamerasCborVersion = 1;

/**
 * Editor viewpoint. This is workspace state, never Level content.
 *
 * ViewportCamera's orthographic scale is relative to the physical
 * framebuffer (visible world width = physical height / scale), so the same
 * raw scale shows a different world extent at a different device-pixel
 * ratio. A pose therefore records the logical viewport size and DPR it was
 * captured at; restoration preserves world units per logical pixel instead
 * of the raw scale.
 */
struct CameraPose
{
    ViewportCamera::OrbitState orbit{};
    ViewportCamera::ProjectionType projection{ ViewportCamera::ProjectionType::Perspective };
    float fovXDeg{ 90.0f };
    float orthoScale{ 64.0f };
    float nearPlane{ ViewportCamera::kPerspectiveNearPlaneDefault };
    float farPlane{ ViewportCamera::kPerspectiveFarPlaneDefault };
    bool clipEnabled{ true };
    QSize logicalSize{};
    double devicePixelRatio{ 1.0 };
};

struct SavedCamera
{
    QString name{};
    CameraPose pose{};
};

/** Framebuffer size GlView renders for a logical widget size and DPR. */
[[nodiscard]] QSize physicalFramebufferSize(QSize logicalSize, double devicePixelRatio) noexcept;

/** World units covered by one logical (device-independent) pixel of an orthographic pose. */
[[nodiscard]] double orthoWorldUnitsPerLogicalPixel(const CameraPose& pose) noexcept;

/**
 * Orthographic scale that reproduces the pose's world units per logical
 * pixel in a viewport of `logicalSize` at `devicePixelRatio`. The view
 * target stays at the viewport centre, so a world point keeps its logical
 * offset from the centre at any size or DPR.
 */
[[nodiscard]] float orthoScaleForViewport(
    const CameraPose& pose, QSize logicalSize, double devicePixelRatio) noexcept;

/** Visible orthographic world extent (width, height) in the pose's own viewport. */
[[nodiscard]] QSizeF orthoLogicalWorldExtent(const CameraPose& pose) noexcept;

[[nodiscard]] bool cameraPoseIsValid(const CameraPose& pose, QString* error = nullptr);

/** Trimmed, whitespace-collapsed display name. */
[[nodiscard]] QString normalizedSavedCameraName(const QString& name);

/**
 * Admits a new or renamed camera name. `ignoredName` is the camera being
 * renamed (it may keep its own name with different case).
 */
[[nodiscard]] bool validateSavedCameraName(
    const QString& name,
    const std::vector<SavedCamera>& existing,
    const QString& ignoredName,
    QString* error);

[[nodiscard]] int findSavedCamera(const std::vector<SavedCamera>& cameras, const QString& name) noexcept;

[[nodiscard]] QCborValue savedCamerasToCbor(const std::vector<SavedCamera>& cameras);
[[nodiscard]] std::optional<std::vector<SavedCamera>> savedCamerasFromCbor(
    const QCborValue& value, QString* error);

[[nodiscard]] QJsonObject cameraPoseToJson(const CameraPose& pose);
[[nodiscard]] std::optional<CameraPose> cameraPoseFromJson(const QJsonObject& object, QString* error);

} // namespace sed::app
