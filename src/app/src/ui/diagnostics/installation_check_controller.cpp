#include "installation_check_controller.hpp"
#include "installation_check_contract.hpp"
#include "installation_child_session.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

#include <array>
#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace sed::ui::diagnostics
{
namespace
{
constexpr qint64 kOutputLimit = 256 * 1024;
constexpr qint64 kReceiptLimit = 64 * 1024;
constexpr qint64 kImageLimit = 8 * 1024 * 1024;


class InstallationChildProcess final : public QProcess
{
public:
    InstallationChildProcess()
    {
#if defined(Q_OS_UNIX) && QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession
            | QProcess::UnixProcessFlag::ResetSignalHandlers);
#elif defined(Q_OS_UNIX) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        setChildProcessModifier([]()
        {
            if ( !detail::prepareInstallationChildSession() )
            {
                ::_exit(126);
            }
        });
#endif
    }

#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
protected:
    void setupChildProcess() override
    {
        if ( !detail::prepareInstallationChildSession() )
        {
            ::_exit(126);
        }
    }
#endif
};

bool writeJson(const QString& path, const QJsonObject& object, QString& error, const bool exclusive = false)
{
    const QByteArray bytes = QJsonDocument(object).toJson();
    if ( exclusive )
    {
        QFile file(path);
        if ( !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || file.write(bytes) != bytes.size() || !file.flush() )
        {
            error = file.errorString();
            return false;
        }
        return true;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if ( !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit() )
    {
        error = file.errorString();
        return false;
    }
    return true;
}

QByteArray readBounded(const QString& path, const qint64 limit, QString& error)
{
    const QFileInfo info(path);
    if ( !info.isFile() || info.isSymLink() || info.size() > limit )
    {
        error = QStringLiteral("A required diagnostic artifact is missing, linked, or oversized.");
        return {};
    }
    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        error = file.errorString();
        return {};
    }
    const QByteArray bytes = file.read(limit + 1);
    if ( bytes.size() > limit || file.error() != QFileDevice::NoError )
    {
        error = QStringLiteral("A diagnostic artifact exceeded its read budget.");
        return {};
    }
    return bytes;
}

QProcessEnvironment diagnosticEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for ( const QString& key : environment.keys() )
    {
        const QString upper = key.toUpper();
        if ( upper.startsWith("QT_") || upper.startsWith("QML") || upper.startsWith("LD_")
            || upper.startsWith("DYLD_") || upper.startsWith("PYTHON") || upper.startsWith("SED_") )
        {
            environment.remove(key);
        }
    }
#ifdef Q_OS_WIN
    environment.insert("PATH", QDir(environment.value("SystemRoot", "C:/Windows")).filePath("System32"));
    environment.insert("QT_QPA_PLATFORM", "windows");
#elif defined(Q_OS_MACOS)
    environment.insert("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
    environment.insert("QT_QPA_PLATFORM", "cocoa");
#else
    environment.insert("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
    environment.insert("QT_QPA_PLATFORM", environment.contains("DISPLAY") ? "xcb"
        : environment.contains("WAYLAND_DISPLAY") ? "wayland" : "xcb");
#endif
    return environment;
}

bool nativePlatform(const QString& name)
{
#ifdef Q_OS_WIN
    return name == QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return name == QStringLiteral("cocoa");
#else
    return name == QStringLiteral("xcb") || name == QStringLiteral("wayland");
#endif
}

} // namespace

class InstallationCheckController::Impl
{
public:
    explicit Impl(InstallationCheckController* owner) : owner_(owner)
    {
        timer_.setSingleShot(true);
        QObject::connect(&timer_, &QTimer::timeout, owner_, [this]()
        {
            complete("failed", QStringLiteral("The native installation-check deadline expired."));
        });
        QObject::connect(&process_, &QProcess::readyReadStandardOutput, owner_, [this]() { readOutput(); });
        QObject::connect(&process_, &QProcess::readyReadStandardError, owner_, [this]() { readOutput(); });
        QObject::connect(&process_, &QProcess::started, owner_, [this]()
        {
            if ( !busy_ )
            {
                return;
            }
            childPid_ = process_.processId();
#ifdef Q_OS_WIN
            HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                static_cast<DWORD>(childPid_));
            const bool assigned = child && AssignProcessToJobObject(job_, child);
            if ( child )
            {
                CloseHandle(child);
            }
            if ( !assigned )
            {
                complete("failed", QStringLiteral("Could not assign the diagnostic child to its owned Windows Job."));
                return;
            }
#else
            if ( childPid_ <= 0 || childPid_ > std::numeric_limits<pid_t>::max()
                || ::getsid(static_cast<pid_t>(childPid_)) != childPid_
                || ::getpgid(static_cast<pid_t>(childPid_)) != childPid_ )
            {
                // Without verified ownership, cleanup may kill the direct child
                // only. Never signal an unverified process group.
                childPid_ = 0;
                complete("failed", QStringLiteral("Could not establish the diagnostic child's owned Unix session."));
                return;
            }
#endif
            phase_ = QStringLiteral("Starting isolated native editor");
            process_.write("start\n");
            Q_EMIT owner_->progress(phase_);
        });
        QObject::connect(&process_, &QProcess::errorOccurred, owner_, [this](const QProcess::ProcessError error)
        {
            if ( busy_ && error == QProcess::FailedToStart )
            {
                complete("failed", QStringLiteral("The diagnostic executable could not start: %1").arg(process_.errorString()));
            }
        });
        QObject::connect(&process_, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            owner_, [this](const int code, const QProcess::ExitStatus status)
        {
            const QPointer<InstallationCheckController> guard(owner_);
            readOutput();
            if ( !guard || !busy_ )
            {
                return;
            }
            if ( code != 0 || status != QProcess::NormalExit )
            {
                complete("failed", QStringLiteral("The diagnostic child exited unsuccessfully (%1).").arg(code));
                return;
            }
            if ( elapsed_.elapsed() >= timer_.interval() )
            {
                complete("failed", QStringLiteral("The native installation-check deadline expired."));
                return;
            }
            // Retire descendants before reading evidence: no owned background
            // process may rewrite an image while it is being admitted.
            stopOwnedTree();
            QString error;
            if ( !admitChildResult(error) )
            {
                complete("failed", error);
                return;
            }
            if ( elapsed_.elapsed() >= timer_.interval() )
            {
                complete("failed", QStringLiteral("The native installation-check deadline expired during evidence admission."));
                return;
            }
            complete("passed", {});
        });
    }

    ~Impl()
    {
        if ( busy_ )
        {
            complete("cancelled", QStringLiteral("The installation-check owner closed."), false);
        }
        stopOwnedTree();
    }

    bool start(const QString& executable, const QString& directory, const int timeoutMs)
    {
        if ( busy_ || timeoutMs < 100 || timeoutMs > 120000 )
        {
            return false;
        }
        QString error;
        if ( !createInstallationReportDirectory(directory, &error) )
        {
            result_ = {{"status", "refused"}, {"error", error}};
            return false;
        }
        directory_ = QFileInfo(directory).canonicalFilePath();
        executable_ = QFileInfo(executable).canonicalFilePath();
        runId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        stdout_.clear();
        stderr_.clear();
        pendingProgress_.clear();
        cleanupError_.clear();
        result_ = {};
        childResult_ = {};
        childPid_ = 0;
        phase_ = QStringLiteral("Admitting diagnostic child");
        busy_ = true;
        elapsed_.start();
        timer_.start(timeoutMs);
        if ( executable_.isEmpty() || !QFileInfo(executable_).isFile() )
        {
            complete("failed", QStringLiteral("The selected diagnostic executable is missing."));
            return true;
        }
        const QJsonObject request{{"schemaVersion", 1}, {"runId", runId_},
            {"executable", executable_}, {"timeoutMs", timeoutMs}};
        if ( !writeJson(directory_ + "/request.json", request, error, true) )
        {
            complete("failed", QStringLiteral("Could not write diagnostic request: %1").arg(error));
            return true;
        }
#ifdef Q_OS_WIN
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if ( !job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) )
        {
            complete("failed", QStringLiteral("Could not create the diagnostic ownership Job."));
            return true;
        }
        process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments)
        {
            arguments->flags |= CREATE_NO_WINDOW;
        });
#endif
        process_.setProcessEnvironment(diagnosticEnvironment());
        process_.setWorkingDirectory(directory_);
        process_.start(executable_, {"--installation-check-child", "--report-directory", directory_});
        return true;
    }

    void stopOwnedTree()
    {
#ifdef Q_OS_WIN
        if ( job_ )
        {
            CloseHandle(job_);
            job_ = nullptr;
        }
#else
        if ( childPid_ > 0 && childPid_ <= std::numeric_limits<pid_t>::max() )
        {
            (void)::kill(-static_cast<pid_t>(childPid_), SIGKILL);
        }
#endif
        childPid_ = 0;
        if ( process_.state() != QProcess::NotRunning )
        {
            process_.kill();
            if ( !process_.waitForFinished(2000) )
            {
                cleanupError_ = QStringLiteral("Owned diagnostic child did not finish within its cleanup grace.");
            }
        }
    }

    void readOutput()
    {
        if ( !busy_ )
        {
            return;
        }
        for ( const auto channel : {QProcess::StandardOutput, QProcess::StandardError} )
        {
            process_.setReadChannel(channel);
            const qint64 remaining = kOutputLimit - stdout_.size() - stderr_.size();
            const QByteArray incoming = process_.read(remaining + 1);
            if ( incoming.size() > remaining )
            {
                complete("failed", QStringLiteral("The diagnostic child exceeded its combined output limit."));
                return;
            }
            (channel == QProcess::StandardOutput ? stdout_ : stderr_).append(incoming);
            if ( channel == QProcess::StandardOutput )
            {
                pendingProgress_.append(incoming);
                qsizetype newline{};
                while ( (newline = pendingProgress_.indexOf('\n')) >= 0 )
                {
                    const QByteArray line = pendingProgress_.left(newline);
                    pendingProgress_.remove(0, newline + 1);
                    const auto update = QJsonDocument::fromJson(line);
                    const QString phase = update.object().value("phase").toString();
                    if ( line.size() > 1024 || !update.isObject() || phase.isEmpty() || phase.size() > 160 )
                    {
                        complete("failed", QStringLiteral("The diagnostic child returned malformed progress."));
                        return;
                    }
                    phase_ = phase;
                    const QPointer<InstallationCheckController> guard(owner_);
                    Q_EMIT owner_->progress(phase_);
                    if ( !guard || !busy_ )
                    {
                        return;
                    }
                }
                if ( pendingProgress_.size() > 1024 )
                {
                    complete("failed", QStringLiteral("The diagnostic progress record exceeded its limit."));
                    return;
                }
            }
        }
    }

    bool admitChildResult(QString& error)
    {
        const auto fail = [&error](const QString& detail)
        {
            error = detail;
            return false;
        };
        if ( !pendingProgress_.isEmpty() )
        {
            return fail(QStringLiteral("The diagnostic child ended with an incomplete progress record."));
        }
        const QByteArray bytes = readBounded(directory_ + "/child-result.json", kReceiptLimit, error);
        if ( !error.isEmpty() )
        {
            return false;
        }
        const QJsonDocument document = QJsonDocument::fromJson(bytes);
        const auto child = document.object();
        if ( !document.isObject() || child.value("schemaVersion") != 1
            || child.value("runId") != runId_ || child.value("executable") != executable_
            || child.value("status") != "passed" )
        {
            return fail(QStringLiteral("The diagnostic child did not return a matching successful receipt."));
        }
        if ( !nativePlatform(child.value("nativePlatform").toString()) )
        {
            return fail(QStringLiteral("The installation check requires a native Qt platform; offscreen is not accepted."));
        }
        const auto milestones = child.value("milestones").toObject();
        for ( const char* key : {"shellExposed", "contextValid", "sceneShaderLinked", "runtimeDataParsed"} )
        {
            if ( milestones.value(QLatin1String(key)) != true )
            {
                return fail(QStringLiteral("A required native installation milestone did not pass."));
            }
        }
        for ( const char* key : {"appVersion", "qtVersion"} )
        {
            if ( child.value(QLatin1String(key)).toString().isEmpty() )
            {
                return fail(QStringLiteral("The diagnostic runtime identity is incomplete."));
            }
        }
        const auto gl = child.value("gl").toObject();
        for ( const char* key : {"vendor", "renderer", "version"} )
        {
            if ( gl.value(QLatin1String(key)).toString().isEmpty() )
            {
                return fail(QStringLiteral("The OpenGL identity is incomplete."));
            }
        }
        std::array<QImage, 2> images;
        QJsonArray imageRecords;
        const std::array<QString, 2> names{"frame-before.png", "frame-after.png"};
        for ( std::size_t index = 0; index < images.size(); ++index )
        {
            const QString path = directory_ + QLatin1Char('/') + names[index];
            const QByteArray imageBytes = readBounded(path, kImageLimit, error);
            if ( !error.isEmpty() )
            {
                return false;
            }
            // Decode the exact bounded captured bytes whose hash is recorded.
            QBuffer captured;
            captured.setData(imageBytes);
            captured.open(QIODevice::ReadOnly);
            QImageReader reader(&captured, "png");
            const QSize dimensions = reader.size();
            if ( dimensions.width() < 64 || dimensions.height() < 64
                || dimensions.width() > 4096 || dimensions.height() > 4096 )
            {
                return fail(QStringLiteral("A native frame exceeds its pixel budget."));
            }
            images[index] = reader.read();
            if ( images[index].isNull() || images[index].size() != dimensions )
            {
                return fail(QStringLiteral("A native frame could not be decoded exactly."));
            }
            imageRecords.append(QJsonObject{{"file", names[index]}, {"bytes", imageBytes.size()},
                {"sha256", QString::fromLatin1(QCryptographicHash::hash(imageBytes, QCryptographicHash::Sha256).toHex())}});
        }
        const auto probes = child.value("probes").toObject();
        std::array<QPointF, 3> points;
        const std::array<QString, 3> pointNames{"firstFace", "secondFace", "background"};
        for ( std::size_t index = 0; index < points.size(); ++index )
        {
            const auto values = probes.value(pointNames[index]).toArray();
            if ( values.size() != 2 || !values[0].isDouble() || !values[1].isDouble() )
            {
                return fail(QStringLiteral("The native frame probes are malformed."));
            }
            points[index] = {values[0].toDouble(), values[1].toDouble()};
        }
        if ( !assessInstallationFrames(images[0], images[1], {points[0], points[1], points[2]}, &error) )
        {
            return false;
        }
        childResult_ = child;
        childResult_.insert("frames", imageRecords);
        return true;
    }

    void complete(const QString& status, QString error, const bool notify = true)
    {
        if ( !busy_ )
        {
            return;
        }
        busy_ = false;
        timer_.stop();
        stopOwnedTree();
        if ( !cleanupError_.isEmpty() )
        {
            error += QLatin1Char(' ') + cleanupError_;
        }
        result_ = {{"schemaVersion", 1}, {"acceptanceKind", "native-installation"},
            {"status", cleanupError_.isEmpty() ? status : QStringLiteral("failed")},
            {"error", error}, {"runId", runId_}, {"executable", executable_},
            {"reportDirectory", directory_}, {"lastPhase", phase_}, {"elapsedMs", elapsed_.elapsed()},
            {"nativeFramesAccepted", status == "passed" && cleanupError_.isEmpty()},
            {"child", childResult_}, {"stdoutBytes", stdout_.size()}, {"stderrBytes", stderr_.size()},
            {"stderrPreview", QString::fromUtf8(stderr_.left(4096))}};
        QString writeError;
        if ( !writeJson(directory_ + "/installation-report.json", result_, writeError) )
        {
            result_.insert("status", "failed");
            result_.insert("nativeFramesAccepted", false);
            result_.insert("error", QStringLiteral("Could not persist the installation receipt: %1").arg(writeError));
        }
        if ( notify )
        {
            Q_EMIT owner_->finished(result_);
        }
    }

    InstallationCheckController* owner_{};
    InstallationChildProcess process_{};
    QTimer timer_{};
    QElapsedTimer elapsed_{};
    bool busy_{};
    qint64 childPid_{};
#ifdef Q_OS_WIN
    HANDLE job_{};
#endif
    QString directory_{};
    QString executable_{};
    QString runId_{};
    QString phase_{};
    QString cleanupError_{};
    QByteArray stdout_{};
    QByteArray stderr_{};
    QByteArray pendingProgress_{};
    QJsonObject result_{};
    QJsonObject childResult_{};
};

InstallationCheckController::InstallationCheckController(QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this))
{
}

InstallationCheckController::~InstallationCheckController() = default;

bool InstallationCheckController::start(const QString& executable, const QString& reportDirectory, const int timeoutMs)
{
    return impl_->start(executable, reportDirectory, timeoutMs);
}

void InstallationCheckController::cancel()
{
    impl_->complete("cancelled", QStringLiteral("The installation check was cancelled."));
}

bool InstallationCheckController::running() const
{
    return impl_->busy_;
}

QJsonObject InstallationCheckController::receipt() const
{
    return impl_->result_;
}

} // namespace sed::ui::diagnostics
