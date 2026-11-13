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
 * captured at; restoration preserves horizontal world units per logical pixel
 * instead of raw scale. Rounded physical dimensions may change vertical pixel
 * scale slightly at fractional DPR; one scale cannot preserve both axes exactly.
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

/** Horizontal world units per logical pixel, using the rounded physical height. */
[[nodiscard]] double orthoWorldUnitsPerLogicalPixel(const CameraPose& pose) noexcept;

/**
 * Scale that preserves horizontal logical-pixel offsets from the view target.
 * Returns zero for invalid dimensions or an unrepresentable destination scale.
 * Fractional framebuffer rounding can change vertical offsets slightly.
 */
[[nodiscard]] float orthoScaleForViewport(const CameraPose& pose, QSize logicalSize, double devicePixelRatio) noexcept;

/** Visible orthographic world extent (width, height) in the pose's own viewport. */
[[nodiscard]] QSizeF orthoLogicalWorldExtent(const CameraPose& pose) noexcept;

[[nodiscard]] bool cameraPoseIsValid(const CameraPose& pose, QString* error = nullptr);

/** Failure-atomic restore into a value camera, including destination scale admission. */
[[nodiscard]] bool applyCameraPoseToCamera(ViewportCamera& camera, const CameraPose& pose, QSize logicalSize,
    double devicePixelRatio, QString* error = nullptr);

/** Trimmed, whitespace-collapsed display name. */
[[nodiscard]] QString normalizedSavedCameraName(const QString& name);

/**
 * Admits a new or renamed camera name. `ignoredName` is the camera being
 * renamed (it may keep its own name with different case).
 */
[[nodiscard]] bool validateSavedCameraName(
    const QString& name, const std::vector<SavedCamera>& existing, const QString& ignoredName, QString* error);

[[nodiscard]] int findSavedCamera(const std::vector<SavedCamera>& cameras, const QString& name) noexcept;

/** Throws std::invalid_argument before encoding an invalid or over-budget list. */
[[nodiscard]] QCborValue savedCamerasToCbor(const std::vector<SavedCamera>& cameras);
[[nodiscard]] std::optional<std::vector<SavedCamera>> savedCamerasFromCbor(const QCborValue& value, QString* error);

[[nodiscard]] QJsonObject cameraPoseToJson(const CameraPose& pose);
[[nodiscard]] std::optional<CameraPose> cameraPoseFromJson(const QJsonObject& object, QString* error);

} // namespace sed::app
