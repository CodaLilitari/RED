// GlView members for editor saved cameras, camera reports and the explicit
// viewport performance harness. Kept outside render/ so the renderer itself
// stays untouched; these members only read state and drive the existing
// paint path.

#include "gl_view.hpp"

#include "viewport/saved_camera.hpp"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QScopeGuard>
#include <QSurfaceFormat>

#include <algorithm>
#include <array>
#include <cmath>

namespace sed
{
namespace
{

QString profileName(const QSurfaceFormat::OpenGLContextProfile profile)
{
    switch (profile)
    {
    case QSurfaceFormat::CoreProfile:
        return QStringLiteral("core");
    case QSurfaceFormat::CompatibilityProfile:
        return QStringLiteral("compatibility");
    case QSurfaceFormat::NoProfile:
        break;
    }
    return QStringLiteral("none");
}

QString renderableTypeName(const QSurfaceFormat::RenderableType type)
{
    switch (type)
    {
    case QSurfaceFormat::OpenGL:
        return QStringLiteral("OpenGL");
    case QSurfaceFormat::OpenGLES:
        return QStringLiteral("OpenGLES");
    case QSurfaceFormat::OpenVG:
        return QStringLiteral("OpenVG");
    case QSurfaceFormat::DefaultRenderableType:
        break;
    }
    return QStringLiteral("default");
}

QJsonObject shaderProgramState(const QOpenGLShaderProgram* program)
{
    QJsonObject state{};
    state.insert(QStringLiteral("present"), program != nullptr);
    state.insert(QStringLiteral("linked"), program != nullptr && program->isLinked());
    const QString log = program ? program->log().trimmed() : QString{};
    state.insert(QStringLiteral("log"), log.left(4000));
    return state;
}

// Software rasterizers that can stand in for a GPU driver on Windows/Linux.
bool rendererLooksSoftware(const QString& renderer, const QString& vendor)
{
    const QString text = (renderer + QLatin1Char(' ') + vendor).toLower();
    for (const char* marker : { "llvmpipe", "softpipe", "swiftshader", "microsoft basic render",
             "gdi generic", "software rasterizer", "lavapipe" })
    {
        if (text.contains(QLatin1String(marker)))
        {
            return true;
        }
    }
    return false;
}

} // namespace

app::CameraPose GlView::captureCameraPose() const
{
    app::CameraPose pose{};
    pose.orbit = camera_.orbit;
    pose.orthoScale = camera_.orthoScale();
    if (viewAnim_.active)
    {
        // An animated view change is committed to its target; saving its
        // midpoint would make the stored viewpoint depend on timing.
        pose.orbit.yawDeg = viewAnim_.targetYawDeg;
        pose.orbit.pitchDeg = viewAnim_.targetPitchDeg;
        pose.orbit.rollDeg = viewAnim_.targetRollDeg;
        pose.orbit.pan = viewAnim_.targetPan;
        pose.orbit.distance = viewAnim_.targetDistance;
        pose.orthoScale = viewAnim_.targetOrthoScale;
    }
    pose.projection = camera_.projectionType();
    pose.fovXDeg = camera_.fovXDeg();
    pose.nearPlane = camera_.nearPlane();
    pose.farPlane = camera_.farPlane();
    pose.clipEnabled = camera_.clipEnabled();
    pose.logicalSize = QSize(std::max(1, width()), std::max(1, height()));
    pose.devicePixelRatio = devicePixelRatioF();
    return pose;
}

void GlView::applyCameraPose(const app::CameraPose& pose)
{
    if (!app::cameraPoseIsValid(pose))
    {
        return;
    }

    cancelViewAnimation();
    const QSize logicalSize(std::max(1, width()), std::max(1, height()));
    const double dpr = devicePixelRatioF();
    const QSize physical = app::physicalFramebufferSize(logicalSize, dpr);
    const float orthoScale = std::clamp(
        app::orthoScaleForViewport(pose, logicalSize, dpr), 0.01f, 1.0e6f);

    camera_.SetCanvas(physical.width(), physical.height());
    camera_.orbit = pose.orbit;
    camera_.SetProjectionType(pose.projection);
    camera_.SetFovXDeg(pose.fovXDeg);
    camera_.SetOrthoScale(orthoScale);
    camera_.SetNearFar(pose.nearPlane, pose.farPlane);
    camera_.SetClipEnabled(pose.clipEnabled);
    camera_.UpdateFromOrbit();
    markViewChanged();
    Q_EMIT viewportStateChanged();
    update();
}

QImage GlView::captureSceneFramebuffer()
{
    // Toasts are transient notifications, not scene content. Withhold them
    // for this grab only and restore their timers untouched afterwards.
    const std::array<ToastState, kToastGroupCount> savedToasts = toastStates_;
    for (ToastState& toast : toastStates_)
    {
        toast.text.clear();
    }
    const auto restoreToasts = qScopeGuard([this, &savedToasts]()
        {
            toastStates_ = savedToasts;
        });
    return grabFramebuffer();
}

QJsonObject GlView::cameraReportEnvironment()
{
    QJsonObject environment{};
    environment.insert(QStringLiteral("qtPlatform"), QGuiApplication::platformName());
    environment.insert(QStringLiteral("qtOpenGLModule"),
        QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES
            ? QStringLiteral("gles")
            : QStringLiteral("desktop-gl"));
    environment.insert(QStringLiteral("qtOpenGLRequest"), qEnvironmentVariable("QT_OPENGL"));
    environment.insert(QStringLiteral("glReady"), glReady_);
    environment.insert(QStringLiteral("contextLost"), glResourcesContextLost_);

    QJsonObject timeDependent{};
    timeDependent.insert(QStringLiteral("viewAnimationActive"), viewAnim_.active);
    timeDependent.insert(QStringLiteral("flyNavigationActive"), flyNav_.active);
    int activeToasts = 0;
    for (const ToastState& toast : toastStates_)
    {
        activeToasts += toast.text.isEmpty() ? 0 : 1;
    }
    timeDependent.insert(QStringLiteral("activeToastOverlays"), activeToasts);
    timeDependent.insert(QStringLiteral("toastOverlaysWithheldFromCapture"), true);
    timeDependent.insert(QStringLiteral("animatedMaterials"),
        QStringLiteral("not animated: the viewport draws material cel 0 with no animation clock"));
    environment.insert(QStringLiteral("timeDependentRendering"), timeDependent);

    QOpenGLContext* const glContext = context();
    environment.insert(QStringLiteral("contextValid"), isValid() && glContext != nullptr);
    if (!isValid() || !glContext)
    {
        environment.insert(QStringLiteral("hardwareAcceleration"), QStringLiteral("unknown"));
        return environment;
    }

    makeCurrent();
    const auto releaseContext = qScopeGuard([this]()
        {
            doneCurrent();
        });
    QOpenGLFunctions* const functions = glContext->functions();
    const auto glString = [functions](const GLenum name)
    {
        const GLubyte* const value = functions->glGetString(name);
        return value ? QString::fromLatin1(reinterpret_cast<const char*>(value)) : QString{};
    };
    const QString vendor = glString(GL_VENDOR);
    const QString renderer = glString(GL_RENDERER);
    environment.insert(QStringLiteral("glVendor"), vendor);
    environment.insert(QStringLiteral("glRenderer"), renderer);
    environment.insert(QStringLiteral("glVersion"), glString(GL_VERSION));
    environment.insert(QStringLiteral("glslVersion"), glString(0x8B8C /* GL_SHADING_LANGUAGE_VERSION */));
    environment.insert(QStringLiteral("isOpenGLES"), glContext->isOpenGLES());

    const QSurfaceFormat format = glContext->format();
    QJsonObject formatObject{};
    formatObject.insert(QStringLiteral("version"),
        QStringLiteral("%1.%2").arg(format.majorVersion()).arg(format.minorVersion()));
    formatObject.insert(QStringLiteral("profile"), profileName(format.profile()));
    formatObject.insert(QStringLiteral("renderableType"), renderableTypeName(format.renderableType()));
    formatObject.insert(QStringLiteral("samples"), format.samples());
    formatObject.insert(QStringLiteral("depthBufferSize"), format.depthBufferSize());
    formatObject.insert(QStringLiteral("stencilBufferSize"), format.stencilBufferSize());
    formatObject.insert(QStringLiteral("alphaBufferSize"), format.alphaBufferSize());
    formatObject.insert(QStringLiteral("swapInterval"), format.swapInterval());
    formatObject.insert(QStringLiteral("debugContext"), format.testOption(QSurfaceFormat::DebugContext));
    environment.insert(QStringLiteral("contextFormat"), formatObject);

    const bool software = qEnvironmentVariable("QT_OPENGL").compare(
                              QStringLiteral("software"), Qt::CaseInsensitive) == 0
        || rendererLooksSoftware(renderer, vendor);
    environment.insert(QStringLiteral("hardwareAcceleration"),
        software ? QStringLiteral("software") : QStringLiteral("hardware"));

    QJsonArray errors{};
    for (int index = 0; index < 32; ++index)
    {
        const GLenum error = functions->glGetError();
        if (error == GL_NO_ERROR)
        {
            break;
        }
        errors.append(QStringLiteral("0x%1").arg(static_cast<uint>(error), 4, 16, QLatin1Char('0')));
    }
    environment.insert(QStringLiteral("glErrorsDrained"), errors);

    QJsonObject shaders{};
    shaders.insert(QStringLiteral("main"), shaderProgramState(program_.get()));
    shaders.insert(QStringLiteral("thickLine"), shaderProgramState(thickLineProgram_.get()));
    shaders.insert(QStringLiteral("grid"), shaderProgramState(gridProgram_.get()));
    shaders.insert(QStringLiteral("frameGhost"), shaderProgramState(frameGhostProgram_.get()));
    shaders.insert(QStringLiteral("pick"), shaderProgramState(pickProgram_.get()));
    environment.insert(QStringLiteral("shaderPrograms"), shaders);
    environment.insert(QStringLiteral("dynamicLightUniformBufferReady"), dynamicLightUniformBufferReady_);
    environment.insert(QStringLiteral("dynamicLightFallbackWarningEmitted"), dynamicLightUniformBufferWarningEmitted_);
    return environment;
}

double GlView::renderFrameForMeasurementMs(double* submitMs)
{
    if (submitMs)
    {
        *submitMs = -1.0;
    }
    QOpenGLContext* const glContext = context();
    if (!isValid() || !glContext)
    {
        return -1.0;
    }

    makeCurrent();
    QOpenGLFunctions* const functions = glContext->functions();
    // Drain earlier queued GPU work so the sample covers exactly one frame.
    functions->glFinish();
    QElapsedTimer timer{};
    timer.start();
    paintGL();
    const qint64 submittedNs = timer.nsecsElapsed();
    functions->glFinish();
    const qint64 finishedNs = timer.nsecsElapsed();
    doneCurrent();

    if (submitMs)
    {
        *submitMs = static_cast<double>(submittedNs) / 1.0e6;
    }
    return static_cast<double>(finishedNs) / 1.0e6;
}

} // namespace sed
