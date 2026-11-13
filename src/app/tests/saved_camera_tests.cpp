#include "viewport/saved_camera.hpp"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace sed::app;
int failures{};
void check(bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
CameraPose example()
{
    CameraPose pose;
    pose.logicalSize = QSize(800, 600);
    pose.projection = ViewportCamera::ProjectionType::Orthographic;
    pose.nearPlane = -10000.0f;
    pose.farPlane = 10000.0f;
    return pose;
}
double horizontalUnits(const CameraPose& pose)
{
    ViewportCamera camera;
    const auto physical = physicalFramebufferSize(pose.logicalSize, pose.devicePixelRatio);
    camera.SetCanvas(physical.width(), physical.height());
    camera.SetProjectionType(ViewportCamera::ProjectionType::Orthographic);
    camera.SetOrthoScale(pose.orthoScale);
    camera.Update();
    return static_cast<double>(camera.orthoRight() - camera.orthoLeft()) / pose.logicalSize.width();
}
void scaling()
{
    auto pose = example();
    pose.orthoScale = 0.01f;
    check(orthoScaleForViewport(pose, pose.logicalSize, pose.devicePixelRatio) == pose.orthoScale,
        "minimum stored float scale restores without a decimal-boundary refusal");
    pose = example();
    check(std::abs(orthoScaleForViewport(pose, pose.logicalSize, 2.0) - 128.0f) < 1.0e-5f,
        "DPR restoration preserves logical-pixel scale");
    check(std::abs(orthoScaleForViewport(pose, QSize(1600, 600), 1.0) - 32.0f) < 1.0e-5f,
        "aspect restoration preserves logical-pixel scale");
    pose.logicalSize = QSize(801, 601);
    pose.devicePixelRatio = 1.25;
    const double originalUnits = horizontalUnits(pose);
    check(std::abs(orthoWorldUnitsPerLogicalPixel(pose) - originalUnits) < 1.0e-9,
        "fractional DPR uses actual rounded framebuffer and logical width");
    auto restored = pose;
    restored.logicalSize = QSize(1003, 579);
    restored.devicePixelRatio = 1.5;
    restored.orthoScale = orthoScaleForViewport(pose, restored.logicalSize, restored.devicePixelRatio);
    check(std::abs(horizontalUnits(restored) - originalUnits) < 1.0e-8,
        "fractional DPR restoration agrees with real camera projection");
    const auto extent = orthoLogicalWorldExtent(pose);
    ViewportCamera actual;
    const auto framebuffer = physicalFramebufferSize(pose.logicalSize, pose.devicePixelRatio);
    actual.SetCanvas(framebuffer.width(), framebuffer.height());
    actual.SetOrthoScale(pose.orthoScale);
    actual.Update();
    check(std::abs(extent.width() - (actual.orthoRight() - actual.orthoLeft())) < 1.0e-6 &&
              std::abs(extent.height() - (actual.orthoTop() - actual.orthoBottom())) < 1.0e-6,
        "reported world extents agree with real camera projection");
    check(physicalFramebufferSize(QSize(0, 600), 1.0).isEmpty(), "invalid logical extent is rejected");
    check(physicalFramebufferSize(QSize(800, 600), 17.0).isEmpty(), "excessive DPR is rejected before multiplication");
}
void admission()
{
    QString error;
    auto pose = example();
    check(cameraPoseIsValid(pose, &error), "ordinary pose is accepted");
    pose.orthoScale = 2000000.0f;
    check(!cameraPoseIsValid(pose, &error), "scale outside viewport restore range is refused");
    pose = example();
    pose.projection = ViewportCamera::ProjectionType::Perspective;
    pose.nearPlane = -2.0f;
    pose.farPlane = -1.0f;
    check(!cameraPoseIsValid(pose, &error), "perspective planes cannot be silently clamped on apply");
    pose = example();
    pose.nearPlane = 1.0f;
    check(!cameraPoseIsValid(pose, &error), "orthographic near plane cannot be silently clamped on apply");
    pose = example();
    pose.orbit.radius = 100.0f;
    pose.orbit.distance = 1.0f;
    check(!cameraPoseIsValid(pose, &error), "renderer distance floor is an admission precondition");
    pose = example();
    pose.nearPlane = 0.0f;
    pose.farPlane = 0.001f;
    check(!cameraPoseIsValid(pose, &error), "minimum clip separation is preserved rather than clamped");
    pose = example();
    pose.orbit.pitchDeg = 360.0f;
    check(!cameraPoseIsValid(pose, &error), "noncanonical pitch is refused rather than silently wrapped");
    pose = example();
    pose.orbit.center = QVector3D(1.0e9f, 1.0e9f, 1.0e9f);
    pose.orbit.distance = 0.01f;
    pose.orbit.radius = 0.0f;
    check(!cameraPoseIsValid(pose, &error), "float-rounded coincident eye and target are refused");
}
void codecs()
{
    const std::vector<SavedCamera> cameras{ { QStringLiteral("North room"), example() } };
    QString error;
    const auto encoded = savedCamerasToCbor(cameras);
    const auto decoded = savedCamerasFromCbor(encoded, &error);
    check(decoded && decoded->size() == 1 && savedCamerasToCbor(*decoded) == encoded, "CBOR pose roundtrip is exact");
    check(cameraPoseFromJson(cameraPoseToJson(example()), &error).has_value(), "JSON pose roundtrip is valid");
    check(!validateSavedCameraName(QStringLiteral("north ROOM"), cameras, {}, &error),
        "camera names are unique ignoring case");
    auto malformed = encoded.toMap();
    auto entries = malformed.value(QStringLiteral("cameras")).toArray();
    entries.append(entries[0]);
    malformed.insert(QStringLiteral("cameras"), entries);
    check(!savedCamerasFromCbor(malformed, &error), "duplicate named cameras reject entire decoded list");
    for ( const bool tooMany : { false, true } )
    {
        auto invalid = cameras;
        if ( tooMany )
        {
            invalid.clear();
            for ( int i = 0; i <= kMaximumSavedCameras; ++i )
            {
                invalid.push_back({ QString::number(i), example() });
            }
        }
        else
        {
            invalid.front().pose.fovXDeg = std::numeric_limits<float>::infinity();
        }
        bool refused = false;
        try
        {
            (void)savedCamerasToCbor(invalid);
        }
        catch ( const std::invalid_argument& )
        {
            refused = true;
        }
        check(refused, "writer refuses an invalid or over-budget camera list");
    }
}

void atomicApply()
{
    ViewportCamera camera;
    camera.orbit.center = QVector3D(2, 4, 6);
    camera.UpdateFromOrbit();
    QString error;
    const auto pose = example();
    check(applyCameraPoseToCamera(camera, pose, QSize(800, 600), 2.0, &error), "valid pose restores into camera");
    check(camera.orthoScale() == 128.0f && camera.canvasWidth() == 1600 && camera.canvasHeight() == 1200,
        "actual camera receives destination canvas and corrected scale");
    const auto beforeView = camera.viewMatrix();
    const auto beforeProjection = camera.projectionMatrix();
    auto impossible = pose;
    impossible.orthoScale = 1000000.0f;
    check(!applyCameraPoseToCamera(camera, impossible, QSize(800, 600), 16.0, &error) && !error.isEmpty(),
        "destination scale overflow refuses explicitly");
    check(camera.viewMatrix() == beforeView && camera.projectionMatrix() == beforeProjection &&
              camera.orthoScale() == 128.0f && camera.canvasWidth() == 1600,
        "failed pose application preserves all prior camera values");
    camera.SetProjectionType(ViewportCamera::ProjectionType::Perspective);
    camera.SetNearFar(2.0f, 70.0f);
    camera.Update();
    check(applyCameraPoseToCamera(camera, pose, QSize(800, 600), 1.0, &error),
        "orthographic pose applies after perspective settings");
    camera.SetProjectionType(ViewportCamera::ProjectionType::Perspective);
    check(camera.nearPlane() == 2.0f && camera.farPlane() == 70.0f,
        "restore preserves inactive projection clip settings");
}

void codecBoundaries()
{
    QString error;
    check(!validateSavedCameraName(QString(QChar(0xd800)), {}, {}, &error),
        "ill-formed UTF-16 name is refused before CBOR replaces its identity");
    std::vector<SavedCamera> cameras;
    for ( int i = 0; i < kMaximumSavedCameras; ++i )
    {
        cameras.push_back({ QString::number(i), example() });
    }
    check(savedCamerasFromCbor(savedCamerasToCbor(cameras), &error)->size() == 256, "maximum list roundtrips");
    auto original = savedCamerasToCbor(cameras).toMap();
    auto entries = original.value(QStringLiteral("cameras")).toArray();
    auto last = entries.last().toMap();
    auto fields = last.value(QStringLiteral("pose")).toArray();
    fields[16] = 1;
    last.insert(QStringLiteral("pose"), fields);
    entries[entries.size() - 1] = last;
    original.insert(QStringLiteral("cameras"), entries);
    check(!savedCamerasFromCbor(original, &error),
        "malformed final pose refuses entire list without partial publication");
    cameras.resize(1);
    cameras.front().name = QStringLiteral("unnormalized  name");
    bool refused = false;
    try
    {
        (void)savedCamerasToCbor(cameras);
    }
    catch ( const std::invalid_argument& )
    {
        refused = true;
    }
    check(refused, "writer refuses noncanonical name spelling");
    auto bad = example();
    bad.nearPlane = 1.0f;
    refused = false;
    try
    {
        (void)cameraPoseToJson(bad);
    }
    catch ( const std::invalid_argument& )
    {
        refused = true;
    }
    check(refused, "JSON writer refuses invalid pose rather than publishing invalid representation");

    auto encoded = savedCamerasToCbor({ { QStringLiteral("Bounded"), example() } }).toMap();
    auto encodedEntries = encoded.value(QStringLiteral("cameras")).toArray();
    auto encodedEntry = encodedEntries[0].toMap();
    auto encodedFields = encodedEntry.value(QStringLiteral("pose")).toArray();
    encodedFields[0] = 1.0e9 + 1.0;
    encodedEntry.insert(QStringLiteral("pose"), encodedFields);
    encodedEntries[0] = encodedEntry;
    encoded.insert(QStringLiteral("cameras"), encodedEntries);
    check(!savedCamerasFromCbor(encoded, &error),
        "CBOR bounds apply before float rounding can admit an oversized coordinate");

    auto jsonPose = cameraPoseToJson(example());
    auto jsonOrbit = jsonPose.value(QStringLiteral("orbit")).toObject();
    jsonOrbit.insert(QStringLiteral("radius"), 1.0e9 + 1.0);
    jsonOrbit.insert(QStringLiteral("distance"), 5.0e7);
    jsonPose.insert(QStringLiteral("orbit"), jsonOrbit);
    check(
        !cameraPoseFromJson(jsonPose, &error), "JSON bounds apply before float rounding can admit an oversized radius");
    jsonOrbit.insert(QStringLiteral("radius"), std::numeric_limits<double>::max());
    jsonPose.insert(QStringLiteral("orbit"), jsonOrbit);
    check(!cameraPoseFromJson(jsonPose, &error), "enormous finite JSON scalars are rejected");
}
} // namespace
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    scaling();
    admission();
    codecs();
    atomicApply();
    codecBoundaries();
    std::cout << "Saved camera failures: " << failures << '\n';
    return failures ? 1 : 0;
}
