#pragma once

#include <optional>
#include <vector>

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "viewport/saved_camera.hpp"

namespace sed
{
struct Level;
class GlView;
namespace assets
{
class ResourceLocator;
}
} // namespace sed

namespace sed::app
{

inline constexpr char kCameraReportSchema[] = "red-camera-report/1";
inline constexpr char kCameraReportSetSchema[] = "red-camera-report-set/1";

/**
 * Level and asset identity shared by every report of one capture session.
 * Public reports carry file names and content hashes only; the full level
 * path is kept separately and written only to an explicitly requested
 * private record.
 */
struct CameraReportDocumentInfo
{
    QString levelFileName{};
    bool levelSaved{};
    QString levelContentSha256{};
    QString projectKind{};
    QJsonObject counts{};
    QJsonObject assets{};
    QString privateLevelPath{};
};

struct CameraReportOptions
{
    QString cameraName{};
    bool includePrivatePaths{};
};

struct CameraReportCapture
{
    QImage image{};
    QJsonObject report{};
};

struct CameraReportFiles
{
    QString pngFile{};
    QString pngSha256{};
    QString reportFile{};
    QString reportSha256{};
    QString privateFile{};
};

struct CameraReportSetEntry
{
    QString cameraName{};
    CameraReportFiles files{};
};

struct CameraReportSetComparison
{
    int compared{};
    int identicalImages{};
    int missing{};
    int sizeMismatches{};
    qint64 differingPixels{};
    int maximumChannelDelta{};
    bool sameRenderer{};
    QStringList notes{};

    [[nodiscard]] bool imagesIdentical() const noexcept
    {
        return compared > 0 && missing == 0 && sizeMismatches == 0 && identicalImages == compared;
    }
};

[[nodiscard]] QString sha256Hex(const QByteArray& bytes);

/** Builds level/asset identity. `levelPath` may be empty for an unsaved document. */
[[nodiscard]] CameraReportDocumentInfo cameraReportDocumentInfo(
    const Level* level, const QString& levelPath, const assets::ResourceLocator* resources);

/**
 * Renders the GlView through its real OpenGL path (grabFramebuffer) and
 * builds a "red-camera-report/1" object. The image is the native scene
 * framebuffer, not the software world snapshot.
 */
[[nodiscard]] std::optional<CameraReportCapture> captureCameraReport(
    GlView& view,
    const CameraReportDocumentInfo& document,
    const CameraReportOptions& options,
    QString* error);

/** Writes <stem>.png and <stem>.json (and <stem>.private.json when requested). */
[[nodiscard]] std::optional<CameraReportFiles> writeCameraReport(
    const QString& directory,
    const QString& stem,
    CameraReportCapture& capture,
    const CameraReportDocumentInfo& document,
    const CameraReportOptions& options,
    QString* error);

/** Stable, file-system safe stem: "<index>-<sanitized name>". */
[[nodiscard]] QString cameraReportFileStem(const QString& cameraName, int index);

/**
 * Captures every camera into `directory` with a "red-camera-report-set/1"
 * manifest.json. The view's original pose is restored afterwards.
 */
[[nodiscard]] std::optional<QJsonObject> captureCameraReportSet(
    GlView& view,
    const std::vector<SavedCamera>& cameras,
    const CameraReportDocumentInfo& document,
    const CameraReportOptions& options,
    const QString& directory,
    QString* error);

/** Compares two set directories by PNG hash, then by per-pixel channel delta. */
[[nodiscard]] std::optional<CameraReportSetComparison> compareCameraReportSets(
    const QString& firstDirectory, const QString& secondDirectory, QString* error);

[[nodiscard]] QJsonObject cameraReportSetComparisonToJson(const CameraReportSetComparison& comparison);

} // namespace sed::app
