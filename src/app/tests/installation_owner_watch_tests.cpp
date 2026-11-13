#include "ui/diagnostics/installation_owner_watch.hpp"

#include <QCoreApplication>
#include <QProcess>

#include <cstdio>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    using namespace sed::ui::diagnostics;
    QCoreApplication application(argc, argv);
    if ( application.arguments().contains("--owned-child") )
    {
        if ( !admitInstallationOwner(std::chrono::milliseconds(300)) )
        {
            return 64;
        }
        InstallationOwnerWatch watch;
        std::cout << "admitted\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    int failures{};
    const auto check = [&failures](const bool condition, const char* message)
    {
        if ( !condition )
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };
    for ( const QByteArray input : {QByteArray("start\n"), QByteArray("invalid\n"), QByteArray{}} )
    {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--owned-child"});
        check(child.waitForStarted(1000), "owner-watch fixture started");
        if ( !input.isEmpty() )
        {
            child.write(input);
            child.waitForBytesWritten(1000);
        }
        if ( input == "start\n" )
        {
            const bool ready = child.waitForReadyRead(1000);
            check(ready && child.readAllStandardOutput().replace("\r\n", "\n") == "admitted\n",
                "exact owned admission reaches native child work");
            child.closeWriteChannel();
            check(child.waitForFinished(1500) && child.exitCode() == 125,
                "owner EOF terminates the blocked diagnostic child independently of its GUI thread");
        }
        else
        {
            check(child.waitForFinished(1000) && child.exitCode() == 64,
                "malformed or missing ownership admission is bounded and refused");
        }
        if ( child.state() != QProcess::NotRunning )
        {
            child.kill();
            child.waitForFinished(1000);
        }
    }
    std::cout << "installation owner-watch failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
