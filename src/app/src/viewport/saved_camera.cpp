#include "viewport/saved_camera.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <QCborArray>
#include <QCborMap>
#include <QJsonArray>
#include <QRegularExpression>

namespace sed::app
{
namespace
{

constexpr int kPoseFieldCount = 20;
constexpr double kMaximumDevicePixelRatio = 16.0;
constexpr int kMaximumLogicalExtent = 65535;
constexpr double kMaximumOrthoScale = 1.0e9;
constexpr double kMaximumAbsoluteCoordinate = 1.0e9;

bool finite(const double value) noexcept
{
    return std::isfinite(value);
}

bool finiteCoordinate(const double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= kMaximumAbsoluteCoordinate;
}

bool finiteVector(const QVector3D& value) noexcept
{
    return finiteCoordinate(value.x()) && finiteCoordinate(value.y()) && finiteCoordinate(value.z());
}

bool setError(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

QJsonArray vectorToJson(const QVector3D& value)
{
    return QJsonArray{ value.x(), value.y(), value.z() };
}

std::optional<QVector3D> vectorFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 3)
    {
        return std::nullopt;
    }
    for (const QJsonValue& component : array)
    {
        if (!component.isDouble() || !finiteCoordinate(component.toDouble()))
        {
            return std::nullopt;
        }
    }
    return QVector3D(
        static_cast<float>(array.at(0).toDouble()),
        static_cast<float>(array.at(1).toDouble()),
        static_cast<float>(array.at(2).toDouble()));
}

QString projectionName(const ViewportCamera::ProjectionType projection)
{
    return projection == ViewportCamera::ProjectionType::Orthographic
        ? QStringLiteral("orthographic")
        : QStringLiteral("perspective");
}

} // namespace

QSize physicalFramebufferSize(const QSize logicalSize, const double devicePixelRatio) noexcept
{
    const double dpr = (finite(devicePixelRatio) && devicePixelRatio > 0.0) ? devicePixelRatio : 1.0;
    const auto physical = [dpr](const int logical)
    {
        return std::max(1, static_cast<int>(std::lround(static_cast<double>(std::max(0, logical)) * dpr)));
    };
    return { physical(logicalSize.width()), physical(logicalSize.height()) };
}

double orthoWorldUnitsPerLogicalPixel(const CameraPose& pose) noexcept
{
    // ViewportCamera: half width = (W/2 / scale) / aspect with aspect = W/H in
    // physical pixels, so one physical pixel spans 1 / (scale * aspect) world
    // units and one logical pixel spans DPR times that.
    const QSize physical = physicalFramebufferSize(pose.logicalSize, pose.devicePixelRatio);
    const double aspect = static_cast<double>(physical.width()) / static_cast<double>(physical.height());
    const double scale = static_cast<double>(pose.orthoScale);
    if (!(scale > 0.0) || !(aspect > 0.0))
    {
        return 0.0;
    }
    const double dpr = (finite(pose.devicePixelRatio) && pose.devicePixelRatio > 0.0) ? pose.devicePixelRatio : 1.0;
    return dpr / (scale * aspect);
}

float orthoScaleForViewport(const CameraPose& pose, const QSize logicalSize, const double devicePixelRatio) noexcept
{
    // Red stage: the raw physical-framebuffer scale is restored unchanged.
    (void)logicalSize;
    (void)devicePixelRatio;
    return pose.orthoScale;
}

QSizeF orthoLogicalWorldExtent(const CameraPose& pose) noexcept
{
    const double unitsPerPixel = orthoWorldUnitsPerLogicalPixel(pose);
    return { static_cast<double>(std::max(0, pose.logicalSize.width())) * unitsPerPixel,
        static_cast<double>(std::max(0, pose.logicalSize.height())) * unitsPerPixel };
}

bool cameraPoseIsValid(const CameraPose& pose, QString* error)
{
    const ViewportCamera::OrbitState& orbit = pose.orbit;
    if (!finiteVector(orbit.center) || !finiteVector(orbit.pan)
        || !finiteCoordinate(orbit.radius) || !finiteCoordinate(orbit.distance)
        || orbit.radius < 0.0f || !(orbit.distance > 0.0f)
        || !finite(orbit.yawDeg) || !finite(orbit.pitchDeg) || !finite(orbit.rollDeg)
        || std::abs(orbit.yawDeg) > 1.0e6f || std::abs(orbit.pitchDeg) > 1.0e6f
        || std::abs(orbit.rollDeg) > 1.0e6f)
    {
        return setError(error, QStringLiteral("The camera orbit contains a non-finite or out-of-range value."));
    }
    if (pose.projection != ViewportCamera::ProjectionType::Perspective
        && pose.projection != ViewportCamera::ProjectionType::Orthographic)
    {
        return setError(error, QStringLiteral("The camera projection is unknown."));
    }
    if (!finite(pose.fovXDeg) || pose.fovXDeg < 1.0f || pose.fovXDeg > 179.0f)
    {
        return setError(error, QStringLiteral("The camera field of view must be between 1 and 179 degrees."));
    }
    if (!finite(pose.orthoScale) || !(pose.orthoScale > 0.0f) || pose.orthoScale > kMaximumOrthoScale)
    {
        return setError(error, QStringLiteral("The orthographic scale must be positive and finite."));
    }
    if (!finite(pose.nearPlane) || !finite(pose.farPlane) || !(pose.farPlane > pose.nearPlane))
    {
        return setError(error, QStringLiteral("The camera clipping planes must be finite with far beyond near."));
    }
    if (pose.logicalSize.width() < 1 || pose.logicalSize.height() < 1
        || pose.logicalSize.width() > kMaximumLogicalExtent || pose.logicalSize.height() > kMaximumLogicalExtent)
    {
        return setError(error, QStringLiteral("The captured viewport size is outside 1..65535 logical pixels."));
    }
    if (!finite(pose.devicePixelRatio) || !(pose.devicePixelRatio > 0.0)
        || pose.devicePixelRatio > kMaximumDevicePixelRatio)
    {
        return setError(error, QStringLiteral("The captured device-pixel ratio is outside (0, 16]."));
    }
    return true;
}

QString normalizedSavedCameraName(const QString& name)
{
    return name.simplified();
}

bool validateSavedCameraName(
    const QString& name,
    const std::vector<SavedCamera>& existing,
    const QString& ignoredName,
    QString* error)
{
    const QString normalized = normalizedSavedCameraName(name);
    if (normalized.isEmpty())
    {
        return setError(error, QStringLiteral("A saved camera needs a name."));
    }
    if (normalized.size() > kMaximumSavedCameraNameLength)
    {
        return setError(error, QStringLiteral("Saved camera names are limited to %1 characters.")
            .arg(kMaximumSavedCameraNameLength));
    }
    for (const QChar character : normalized)
    {
        if (character.category() == QChar::Other_Control)
        {
            return setError(error, QStringLiteral("Saved camera names cannot contain control characters."));
        }
    }
    for (const SavedCamera& camera : existing)
    {
        if (!ignoredName.isEmpty() && camera.name.compare(ignoredName, Qt::CaseInsensitive) == 0)
        {
            continue;
        }
        if (camera.name.compare(normalized, Qt::CaseInsensitive) == 0)
        {
            return setError(error, QStringLiteral("A saved camera named \"%1\" already exists.").arg(camera.name));
        }
    }
    return true;
}

int findSavedCamera(const std::vector<SavedCamera>& cameras, const QString& name) noexcept
{
    const QString normalized = normalizedSavedCameraName(name);
    for (std::size_t index = 0; index < cameras.size(); ++index)
    {
        if (cameras[index].name.compare(normalized, Qt::CaseInsensitive) == 0)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

QCborValue savedCamerasToCbor(const std::vector<SavedCamera>& cameras)
{
    QCborArray encoded{};
    for (const SavedCamera& camera : cameras)
    {
        const CameraPose& pose = camera.pose;
        const ViewportCamera::OrbitState& orbit = pose.orbit;
        QCborArray fields{
            static_cast<double>(orbit.center.x()), static_cast<double>(orbit.center.y()),
            static_cast<double>(orbit.center.z()),
            static_cast<double>(orbit.pan.x()), static_cast<double>(orbit.pan.y()),
            static_cast<double>(orbit.pan.z()),
            static_cast<double>(orbit.radius), static_cast<double>(orbit.distance),
            static_cast<double>(orbit.yawDeg), static_cast<double>(orbit.pitchDeg),
            static_cast<double>(orbit.rollDeg),
            static_cast<qint64>(pose.projection == ViewportCamera::ProjectionType::Orthographic ? 1 : 0),
            static_cast<double>(pose.fovXDeg), static_cast<double>(pose.orthoScale),
            static_cast<double>(pose.nearPlane), static_cast<double>(pose.farPlane),
            pose.clipEnabled,
            static_cast<qint64>(pose.logicalSize.width()), static_cast<qint64>(pose.logicalSize.height()),
            pose.devicePixelRatio,
        };
        QCborMap entry{};
        entry.insert(QStringLiteral("name"), camera.name);
        entry.insert(QStringLiteral("pose"), fields);
        encoded.append(entry);
    }
    QCborMap root{};
    root.insert(QStringLiteral("version"), static_cast<qint64>(kSavedCamerasCborVersion));
    root.insert(QStringLiteral("cameras"), encoded);
    return root;
}

std::optional<std::vector<SavedCamera>> savedCamerasFromCbor(const QCborValue& value, QString* error)
{
    const auto fail = [error](const QString& message) -> std::optional<std::vector<SavedCamera>>
    {
        setError(error, message);
        return std::nullopt;
    };
    if (!value.isMap())
    {
        return fail(QStringLiteral("The saved camera list is not a map."));
    }
    const QCborMap root = value.toMap();
    const QCborValue version = root.value(QStringLiteral("version"));
    if (!version.isInteger() || version.toInteger() != kSavedCamerasCborVersion)
    {
        return fail(QStringLiteral("The saved camera list has an unsupported version."));
    }
    const QCborValue camerasValue = root.value(QStringLiteral("cameras"));
    if (!camerasValue.isArray())
    {
        return fail(QStringLiteral("The saved camera list has no camera array."));
    }
    const QCborArray array = camerasValue.toArray();
    if (array.size() > kMaximumSavedCameras)
    {
        return fail(QStringLiteral("The saved camera list exceeds %1 cameras.").arg(kMaximumSavedCameras));
    }
    std::vector<SavedCamera> cameras{};
    cameras.reserve(static_cast<std::size_t>(array.size()));
    for (const QCborValue& entryValue : array)
    {
        if (!entryValue.isMap())
        {
            return fail(QStringLiteral("A saved camera entry is not a map."));
        }
        const QCborMap entry = entryValue.toMap();
        const QCborValue nameValue = entry.value(QStringLiteral("name"));
        const QCborValue poseValue = entry.value(QStringLiteral("pose"));
        if (!nameValue.isString() || !poseValue.isArray())
        {
            return fail(QStringLiteral("A saved camera entry is missing its name or pose."));
        }
        const QString name = nameValue.toString();
        QString nameError{};
        if (name != normalizedSavedCameraName(name)
            || !validateSavedCameraName(name, cameras, {}, &nameError))
        {
            return fail(nameError.isEmpty()
                ? QStringLiteral("A saved camera name is not normalized.")
                : nameError);
        }
        const QCborArray fields = poseValue.toArray();
        if (fields.size() != kPoseFieldCount)
        {
            return fail(QStringLiteral("Saved camera \"%1\" has a malformed pose.").arg(name));
        }
        const auto number = [&fields](const int index, double* out)
        {
            const QCborValue field = fields.at(index);
            if (!field.isDouble() && !field.isInteger())
            {
                return false;
            }
            *out = field.toDouble();
            return std::isfinite(*out);
        };
        std::array<double, kPoseFieldCount> numbers{};
        for (const int index : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 19 })
        {
            if (!number(index, &numbers[static_cast<std::size_t>(index)]))
            {
                return fail(QStringLiteral("Saved camera \"%1\" has a non-numeric pose value.").arg(name));
            }
        }
        const QCborValue projection = fields.at(11);
        const QCborValue clip = fields.at(16);
        const QCborValue width = fields.at(17);
        const QCborValue height = fields.at(18);
        if (!projection.isInteger() || projection.toInteger() < 0 || projection.toInteger() > 1
            || !clip.isBool() || !width.isInteger() || !height.isInteger()
            || width.toInteger() < 1 || width.toInteger() > kMaximumLogicalExtent
            || height.toInteger() < 1 || height.toInteger() > kMaximumLogicalExtent)
        {
            return fail(QStringLiteral("Saved camera \"%1\" has a malformed projection or viewport.").arg(name));
        }
        SavedCamera camera{};
        camera.name = name;
        CameraPose& pose = camera.pose;
        pose.orbit.center = QVector3D(static_cast<float>(numbers[0]), static_cast<float>(numbers[1]),
            static_cast<float>(numbers[2]));
        pose.orbit.pan = QVector3D(static_cast<float>(numbers[3]), static_cast<float>(numbers[4]),
            static_cast<float>(numbers[5]));
        pose.orbit.radius = static_cast<float>(numbers[6]);
        pose.orbit.distance = static_cast<float>(numbers[7]);
        pose.orbit.yawDeg = static_cast<float>(numbers[8]);
        pose.orbit.pitchDeg = static_cast<float>(numbers[9]);
        pose.orbit.rollDeg = static_cast<float>(numbers[10]);
        pose.projection = projection.toInteger() == 1
            ? ViewportCamera::ProjectionType::Orthographic
            : ViewportCamera::ProjectionType::Perspective;
        pose.fovXDeg = static_cast<float>(numbers[12]);
        pose.orthoScale = static_cast<float>(numbers[13]);
        pose.nearPlane = static_cast<float>(numbers[14]);
        pose.farPlane = static_cast<float>(numbers[15]);
        pose.clipEnabled = clip.toBool();
        pose.logicalSize = QSize(static_cast<int>(width.toInteger()), static_cast<int>(height.toInteger()));
        pose.devicePixelRatio = numbers[19];
        QString poseError{};
        if (!cameraPoseIsValid(pose, &poseError))
        {
            return fail(QStringLiteral("Saved camera \"%1\": %2").arg(name, poseError));
        }
        cameras.push_back(std::move(camera));
    }
    return cameras;
}

QJsonObject cameraPoseToJson(const CameraPose& pose)
{
    QJsonObject orbit{};
    orbit.insert(QStringLiteral("center"), vectorToJson(pose.orbit.center));
    orbit.insert(QStringLiteral("pan"), vectorToJson(pose.orbit.pan));
    orbit.insert(QStringLiteral("radius"), pose.orbit.radius);
    orbit.insert(QStringLiteral("distance"), pose.orbit.distance);
    orbit.insert(QStringLiteral("yawDeg"), pose.orbit.yawDeg);
    orbit.insert(QStringLiteral("pitchDeg"), pose.orbit.pitchDeg);
    orbit.insert(QStringLiteral("rollDeg"), pose.orbit.rollDeg);

    QJsonObject object{};
    object.insert(QStringLiteral("orbit"), orbit);
    object.insert(QStringLiteral("projection"), projectionName(pose.projection));
    object.insert(QStringLiteral("fovXDeg"), pose.fovXDeg);
    object.insert(QStringLiteral("orthoScale"), pose.orthoScale);
    object.insert(QStringLiteral("orthoWorldUnitsPerLogicalPixel"), orthoWorldUnitsPerLogicalPixel(pose));
    const QSizeF extent = orthoLogicalWorldExtent(pose);
    object.insert(QStringLiteral("orthoLogicalWorldExtent"), QJsonArray{ extent.width(), extent.height() });
    object.insert(QStringLiteral("near"), pose.nearPlane);
    object.insert(QStringLiteral("far"), pose.farPlane);
    object.insert(QStringLiteral("clipEnabled"), pose.clipEnabled);
    object.insert(QStringLiteral("logicalSize"),
        QJsonArray{ pose.logicalSize.width(), pose.logicalSize.height() });
    object.insert(QStringLiteral("devicePixelRatio"), pose.devicePixelRatio);
    const QSize physical = physicalFramebufferSize(pose.logicalSize, pose.devicePixelRatio);
    object.insert(QStringLiteral("framebufferSize"), QJsonArray{ physical.width(), physical.height() });
    return object;
}

std::optional<CameraPose> cameraPoseFromJson(const QJsonObject& object, QString* error)
{
    const auto fail = [error](const QString& message) -> std::optional<CameraPose>
    {
        setError(error, message);
        return std::nullopt;
    };
    const QJsonObject orbit = object.value(QStringLiteral("orbit")).toObject();
    const std::optional<QVector3D> center = vectorFromJson(orbit.value(QStringLiteral("center")));
    const std::optional<QVector3D> pan = vectorFromJson(orbit.value(QStringLiteral("pan")));
    if (!center || !pan)
    {
        return fail(QStringLiteral("The camera orbit centre or pan is malformed."));
    }
    const auto number = [](const QJsonValue& value, double* out)
    {
        if (!value.isDouble())
        {
            return false;
        }
        *out = value.toDouble();
        return std::isfinite(*out);
    };
    double radius{}, distance{}, yaw{}, pitch{}, roll{}, fov{}, scale{}, nearPlane{}, farPlane{}, dpr{};
    if (!number(orbit.value(QStringLiteral("radius")), &radius)
        || !number(orbit.value(QStringLiteral("distance")), &distance)
        || !number(orbit.value(QStringLiteral("yawDeg")), &yaw)
        || !number(orbit.value(QStringLiteral("pitchDeg")), &pitch)
        || !number(orbit.value(QStringLiteral("rollDeg")), &roll)
        || !number(object.value(QStringLiteral("fovXDeg")), &fov)
        || !number(object.value(QStringLiteral("orthoScale")), &scale)
        || !number(object.value(QStringLiteral("near")), &nearPlane)
        || !number(object.value(QStringLiteral("far")), &farPlane)
        || !number(object.value(QStringLiteral("devicePixelRatio")), &dpr))
    {
        return fail(QStringLiteral("The camera pose has a missing or non-numeric value."));
    }
    const QString projection = object.value(QStringLiteral("projection")).toString();
    const QJsonArray size = object.value(QStringLiteral("logicalSize")).toArray();
    if ((projection != QStringLiteral("perspective") && projection != QStringLiteral("orthographic"))
        || !object.value(QStringLiteral("clipEnabled")).isBool() || size.size() != 2
        || !size.at(0).isDouble() || !size.at(1).isDouble())
    {
        return fail(QStringLiteral("The camera projection, clipping flag or viewport size is malformed."));
    }
    CameraPose pose{};
    pose.orbit.center = *center;
    pose.orbit.pan = *pan;
    pose.orbit.radius = static_cast<float>(radius);
    pose.orbit.distance = static_cast<float>(distance);
    pose.orbit.yawDeg = static_cast<float>(yaw);
    pose.orbit.pitchDeg = static_cast<float>(pitch);
    pose.orbit.rollDeg = static_cast<float>(roll);
    pose.projection = projection == QStringLiteral("orthographic")
        ? ViewportCamera::ProjectionType::Orthographic
        : ViewportCamera::ProjectionType::Perspective;
    pose.fovXDeg = static_cast<float>(fov);
    pose.orthoScale = static_cast<float>(scale);
    pose.nearPlane = static_cast<float>(nearPlane);
    pose.farPlane = static_cast<float>(farPlane);
    pose.clipEnabled = object.value(QStringLiteral("clipEnabled")).toBool();
    pose.logicalSize = QSize(size.at(0).toInt(), size.at(1).toInt());
    pose.devicePixelRatio = dpr;
    QString poseError{};
    if (!cameraPoseIsValid(pose, &poseError))
    {
        return fail(poseError);
    }
    return pose;
}

} // namespace sed::app
