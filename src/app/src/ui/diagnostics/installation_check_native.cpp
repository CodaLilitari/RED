#include "installation_check_native.hpp"
#include "installation_check_contract.hpp"
#include "installation_check_controller.hpp"
#include "installation_owner_watch.hpp"
#include "installation_runtime_data.hpp"
#include "app_settings.hpp"
#include "app_version.hpp"
#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "render/viewport_aa_policy.hpp"
#include "../../../../core/src/assets/atomic_file_write.hpp"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QTimer>
#include <QUuid>
#include <QWindow>

#include <array>
#include <cstdio>
#include <stdexcept>

namespace sed::ui::diagnostics
{
namespace
{
void writeLine(const QJsonObject& object)
{
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    (void)std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
    (void)std::fflush(stdout);
}

void publishBytes(const QString& destination, const QByteArray& bytes)
{
    const std::filesystem::path path(destination.toStdWString());
    assets::detail::StagedAssetFile staged(path);
    staged.writeAndClose(std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
    staged.publishNew(path);
}

void publishFrame(const QString& destination, const QImage& frame)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if ( !buffer.open(QIODevice::WriteOnly) || !frame.save(&buffer, "PNG") || bytes.size() > 8 * 1024 * 1024 )
    {
        throw std::runtime_error("The native frame could not be encoded within its budget.");
    }
    publishBytes(destination, bytes);
}

bool nativePlatform(const QString& platform)
{
#ifdef Q_OS_WIN
    return platform == "windows";
#elif defined(Q_OS_MACOS)
    return platform == "cocoa";
#else
    return platform == "xcb" || platform == "wayland";
#endif
}
}

int runInstallationCheckController(int& argc, char** argv, const QString& reportDirectory)
{
    QCoreApplication application(argc, argv);
    InstallationCheckController controller;
    QObject::connect(&controller, &InstallationCheckController::finished, &application,
        [&application](const QJsonObject& receipt)
        {
            writeLine(receipt);
            application.exit(receipt.value("status") == "passed" ? 0 : 1);
        });
    if ( !controller.start(QCoreApplication::applicationFilePath(), reportDirectory) )
    {
        writeLine(controller.receipt());
        return 64;
    }
    return controller.running() ? application.exec() : controller.receipt().value("status") == "passed" ? 0 : 1;
}

int runInstallationCheckChild(int& argc, char** argv, const QString& reportDirectory)
{
    if ( !admitInstallationOwner(std::chrono::seconds(5)) )
    {
        return 64;
    }
    InstallationOwnerWatch ownerWatch;
    try
    {
        const QFileInfo directory(reportDirectory);
        QFile requestFile(QDir(reportDirectory).filePath("request.json"));
        const QFileInfo requestInfo(requestFile);
        if ( !directory.isAbsolute() || directory.isSymLink() || !directory.isDir()
            || requestInfo.isSymLink() || !requestInfo.isFile() || requestInfo.size() > 4096
            || !requestFile.open(QIODevice::ReadOnly) )
        {
            throw std::runtime_error("The owned installation request is unavailable.");
        }
        const QByteArray requestBytes = requestFile.read(4097);
        const auto request = QJsonDocument::fromJson(requestBytes).object();
        if ( requestBytes.size() > 4096 || requestFile.error() != QFileDevice::NoError
            || request.value("schemaVersion") != 1 || QUuid(request.value("runId").toString()).isNull() )
        {
            throw std::runtime_error("The owned installation request is malformed.");
        }
        QString error;
        if ( !initializeIsolatedAppStorage(std::filesystem::path(
                QDir(reportDirectory).filePath("storage").toStdWString()), &error) )
        {
            throw std::runtime_error(error.toStdString());
        }
        // Restrict this explicit deployment check to packaged plug-in roots.
        // The normal application launch retains its existing Qt discovery.
        const QString executableDirectory = QFileInfo(request.value("executable").toString()).canonicalPath();
        QStringList pluginRoots{executableDirectory, executableDirectory + "/plugins"};
#ifdef Q_OS_MACOS
        pluginRoots.append(QDir::cleanPath(executableDirectory + "/../PlugIns"));
#elif !defined(Q_OS_WIN)
        pluginRoots.append(QDir::cleanPath(executableDirectory + "/../plugins"));
#endif
        QCoreApplication::setLibraryPaths(pluginRoots);
        QSurfaceFormat::setDefaultFormat(render::viewportSurfaceFormat(render::kDefaultViewportMsaaSamples));
        QApplication application(argc, argv);
        QApplication::setApplicationName("RED Installation Check");
        QApplication::setOrganizationName("RED");
        QApplication::setApplicationVersion(QString::fromLatin1(app::kVersion));
        if ( !nativePlatform(QGuiApplication::platformName())
            || QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()
                != request.value("executable").toString() )
        {
            throw std::runtime_error("The check requires the requested executable and its native Qt platform.");
        }
        const auto runtimeData = inspectInstallationRuntimeData(
            QCoreApplication::applicationDirPath() + "/data", &error);
        if ( !runtimeData )
        {
            throw std::runtime_error(error.toStdString());
        }
        writeLine({{"phase", "Bundled runtime data parsed"}});
        MainWindow window(nullptr, true, MainWindow::StartupPurpose::installationCheck);
        auto* view = window.installationCheckViewport();
        if ( !view )
        {
            throw std::runtime_error("The diagnostic shell did not provide its isolated viewport.");
        }
        QString renderError;
        QObject::connect(view, &GlView::errorMessage, &window, [&renderError](const QString& message)
        {
            if ( renderError.isEmpty() )
            {
                renderError = message.left(2048);
            }
        });
        window.setWindowTitle(QStringLiteral("RED — Installation Check (synthetic scene)"));
        window.setMinimumSize(800, 600);
        window.resize(1280, 800);
        window.setEnabled(false);
        window.show();
        QTimer progress;
        progress.setInterval(16);
        unsigned stage{};
        QSize stableSize;
        std::array<QImage, 2> frames;
        InstallationFrameProbes probes;
        QJsonObject graphics;
        QJsonObject child{{"schemaVersion", 1}, {"runId", request.value("runId")},
            {"executable", request.value("executable")}, {"appVersion", QString::fromLatin1(app::kVersion)},
            {"qtVersion", QString::fromLatin1(qVersion())}, {"nativePlatform", QGuiApplication::platformName()},
            {"runtimeData", *runtimeData}, {"pluginRoots", QJsonArray::fromStringList(QCoreApplication::libraryPaths())}};
        QObject::connect(&progress, &QTimer::timeout, &window, [&]()
        {
            try
            {
                if ( !renderError.isEmpty() )
                {
                    throw std::runtime_error(renderError.toStdString());
                }
                if ( !window.windowHandle() || !window.windowHandle()->isExposed() || !view->isValid()
                    || !view->context() || !view->context()->isValid() )
                {
                    return;
                }
                if ( !view->sceneShaderLinked() )
                {
                    throw std::runtime_error("The native viewport scene shader is not linked.");
                }
                if ( view->size() != stableSize )
                {
                    stableSize = view->size();
                    return;
                }
                if ( stage == 0 )
                {
                    if ( !window.setInstallationCheckFrame(0, &error) )
                    {
                        throw std::runtime_error(error.toStdString());
                    }
                    writeLine({{"phase", "Native editor shell and OpenGL scene shader ready"}});
                    ++stage;
                    return;
                }
                const unsigned frameIndex = stage - 1;
                const qreal dpr = view->devicePixelRatioF();
                if ( view->width() * dpr < 64 || view->height() * dpr < 64
                    || view->width() * dpr > 4096 || view->height() * dpr > 4096 )
                {
                    throw std::runtime_error("The native viewport exceeds the frame size budget.");
                }
                frames[frameIndex] = view->grabFramebuffer();
                if ( frames[frameIndex].isNull() )
                {
                    throw std::runtime_error("The native framebuffer could not be captured.");
                }
                if ( stage == 1 )
                {
                    const auto normalized = [view](const QVector3D& point)
                    {
                        const auto projected = view->projectWorldToViewport(point);
                        if ( !projected )
                        {
                            throw std::runtime_error("Synthetic probe projection failed.");
                        }
                        return QPointF(projected->x() / view->width(), projected->y() / view->height());
                    };
                    probes = {normalized({-2.0f, 0.0f, 0.0f}), normalized({2.0f, 0.0f, 0.0f}),
                        normalized({0.0f, 2.0f, 0.0f})};
                    if ( !window.setInstallationCheckFrame(1, &error) )
                    {
                        throw std::runtime_error(error.toStdString());
                    }
                    ++stage;
                    return;
                }
                progress.stop();
                if ( !assessInstallationFrames(frames[0], frames[1], probes, &error) )
                {
                    throw std::runtime_error(error.toStdString());
                }
                view->makeCurrent();
                auto* functions = view->context()->functions();
                const auto glString = [functions](const GLenum name)
                {
                    const auto* bytes = functions->glGetString(name);
                    return bytes ? QString::fromLatin1(reinterpret_cast<const char*>(bytes)) : QString{};
                };
                graphics = {{"vendor", glString(GL_VENDOR)}, {"renderer", glString(GL_RENDERER)},
                    {"version", glString(GL_VERSION)}, {"samples", view->context()->format().samples()}};
                const GLenum glError = functions->glGetError();
                view->doneCurrent();
                if ( glError != GL_NO_ERROR )
                {
                    throw std::runtime_error("The native viewport reported an OpenGL error during the diagnostic.");
                }
                publishFrame(reportDirectory + "/frame-before.png", frames[0]);
                publishFrame(reportDirectory + "/frame-after.png", frames[1]);
                child.insert("status", "passed");
                child.insert("gl", graphics);
                child.insert("devicePixelRatio", view->devicePixelRatioF());
                child.insert("milestones", QJsonObject{{"shellExposed", true}, {"contextValid", true},
                    {"sceneShaderLinked", true}, {"runtimeDataParsed", true}});
                child.insert("probes", QJsonObject{{"firstFace", QJsonArray{probes.firstFace.x(), probes.firstFace.y()}},
                    {"secondFace", QJsonArray{probes.secondFace.x(), probes.secondFace.y()}},
                    {"background", QJsonArray{probes.background.x(), probes.background.y()}}});
                publishBytes(reportDirectory + "/child-result.json", QJsonDocument(child).toJson());
                writeLine({{"phase", "Two distinct native geometry frames inspected"}});
                application.exit(0);
            }
            catch ( const std::exception& exception )
            {
                progress.stop();
                qCritical("Installation check failed: %s", exception.what());
                application.exit(1);
            }
        });
        progress.start();
        return application.exec();
    }
    catch ( const std::exception& exception )
    {
        std::fprintf(stderr, "Installation check failed: %s\n", exception.what());
        return 1;
    }
}
}
