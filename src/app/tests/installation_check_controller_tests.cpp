#include "ui/diagnostics/installation_check_controller.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

namespace
{
int failures{};
void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
template<typename Predicate>
bool waitUntil(Predicate predicate, const int timeout = 4000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while ( !predicate() && elapsed.elapsed() < timeout )
    {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return predicate();
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    using sed::ui::diagnostics::InstallationCheckController;
    const QString fixture = QCoreApplication::applicationDirPath() + "/installation_process_fixture"
#ifdef Q_OS_WIN
        ".exe"
#endif
        ;
    QTemporaryDir scratch(QDir::tempPath()
        + QStringLiteral("/red-installation-\u00e9-\u0394-XXXXXX"));
    check(scratch.isValid() && QFileInfo::exists(fixture), "owned scratch and actual child executable exist");
    QImage before(160, 120, QImage::Format_ARGB32);
    QImage after(160, 120, QImage::Format_ARGB32);
    before.fill(Qt::black);
    after.fill(Qt::black);
    check(after.save(scratch.filePath("blank.png")), "blank fixture written");
    {
        QPainter painter(&before);
        painter.fillRect(QRect(25, 45, 31, 31), Qt::white);
        QPainter other(&after);
        other.fillRect(QRect(105, 45, 31, 31), Qt::white);
    }
    check(before.save(scratch.filePath("before.png")) && after.save(scratch.filePath("after.png")),
        "positive frame fixtures written");

    for ( const QString mode : {"good", "crash", "blank", "wrong-run", "offscreen", "excess", "stall",
              "partial-progress", "missing-shader"} )
    {
        InstallationCheckController controller;
        bool completed = false;
        QObject::connect(&controller, &InstallationCheckController::finished, &application,
            [&completed](const QJsonObject&) { completed = true; });
        QElapsedTimer elapsed;
        elapsed.start();
        const QString output = scratch.filePath(mode);
        const bool started = controller.start(fixture, output, mode == "stall" ? 150 : 3000);
        check(started, "actual synthetic child starts within owned report scope");
        if ( !started )
        {
            continue;
        }
        check(!controller.start(fixture, scratch.filePath("concurrent")), "a concurrent request cannot replace the active run");
        check(waitUntil([&completed]() { return completed; }), "owned child reaches a terminal receipt");
        const auto receipt = controller.receipt();
        check(receipt.value("status").toString() == (mode == "good" ? "passed" : "failed"),
            "only matching native, rendered, complete child evidence can pass");
        check(!controller.running(), "terminal controller releases busy state");
        QFile saved(output + "/installation-report.json");
        check(saved.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(saved.readAll()).object() == receipt,
            "terminal receipt is persisted in the owned report directory");
        if ( mode == "stall" )
        {
            check(elapsed.elapsed() < 3000 && receipt.value("error").toString().contains("deadline"),
                "actual stalled child is stopped by the controller deadline");
        }
        if ( mode == "good" )
        {
            const QByteArray original = QJsonDocument(receipt).toJson();
            saved.close();
            check(!controller.start(fixture, output), "finished report cannot be reused for another run");
            check(saved.open(QIODevice::ReadOnly) && saved.readAll() == original,
                "refused rerun preserves prior receipt bytes");
        }
    }
    {
        InstallationCheckController controller;
        const QString output = scratch.filePath("cancel");
        const bool started = controller.start(fixture, output);
        check(started, "cancellation child starts");
        if ( started )
        {
            check(waitUntil([&output]() { return QFileInfo::exists(output + "/admitted"); }),
                "child waited until ownership admission");
            controller.cancel();
            check(!controller.running() && controller.receipt().value("status") == "cancelled",
                "Cancel stops the exact owned child and produces cancellation evidence");
        }
    }
    {
        InstallationCheckController controller;
        const QString output = scratch.filePath("descendants");
        QProcess peer;
        peer.start(fixture, {"--descendant", scratch.filePath("unrelated-marker")});
        check(peer.waitForStarted(2000), "independent test-owned peer started");
        const bool started = controller.start(fixture, output, 400);
        check(started, "owned descendant fixture starts");
        if ( started )
        {
            check(waitUntil([&output]() { return QFileInfo::exists(output + "/admitted"); }, 1000),
                "fixture actually spawned its owned descendant before the deadline");
            check(waitUntil([&controller]() { return !controller.running(); }), "owned tree deadline completes");
            check(peer.state() == QProcess::Running, "unrelated peer remains alive after controller cleanup");
            QElapsedTimer hold;
            hold.start();
            waitUntil([&hold]() { return hold.elapsed() >= 3200; });
            check(!QFileInfo::exists(output + "/escaped-child"), "owned descendant cannot outlive timeout and publish later");
            QFile unrelatedMarker(scratch.filePath("unrelated-marker"));
            check(unrelatedMarker.open(QIODevice::ReadOnly) && unrelatedMarker.readAll() == "escaped",
                "unrelated peer publishes exact marker bytes through the Unicode path");
        }
        if ( peer.state() != QProcess::NotRunning )
        {
            peer.kill();
            check(peer.waitForFinished(2000), "only the test-owned peer is cleaned up");
        }
    }
    {
        InstallationCheckController controller;
        const QString output = scratch.filePath("missing-executable");
        check(controller.start(scratch.filePath("does-not-exist"), output),
            "missing executable receives an owned failure-report destination");
        check(!controller.running() && controller.receipt().value("status") == "failed"
                && controller.receipt().value("error").toString().contains("missing"),
            "missing executable is diagnosed directly without launching/installing anything");
    }
    {
        const QString output = scratch.filePath("progress-owner-destroy");
        QPointer<InstallationCheckController> owner = new InstallationCheckController;
        QObject::connect(owner, &InstallationCheckController::progress, &application,
            [&owner](const QString& phase)
            {
                if ( phase == "fixture-final-progress" )
                {
                    delete owner.data();
                }
            });
        check(owner->start(fixture, output), "progress lifetime fixture starts");
        check(waitUntil([&owner]() { return !owner; }), "progress callback may destroy its owning controller");
        if ( owner )
        {
            delete owner.data();
        }
        QFile result(output + "/installation-report.json");
        check(result.open(QIODevice::ReadOnly)
            && QJsonDocument::fromJson(result.readAll()).object().value("status") == "cancelled",
            "progress owner destruction persists an honest cancellation receipt");
    }
    std::cout << "installation controller failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
