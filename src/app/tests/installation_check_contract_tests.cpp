#include "ui/diagnostics/installation_check_contract.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <iostream>
#include <limits>

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
}

int main(int argc, char** argv)
{
    const auto processArguments = sed::ui::diagnostics::installationProcessArguments(argc, argv);
    QCoreApplication application(argc, argv);
    using namespace sed::ui::diagnostics;
    if ( application.arguments().contains("--argument-roundtrip") )
    {
        if ( !processArguments )
        {
            return 1;
        }
        const QByteArray bytes = QJsonDocument(QJsonArray::fromStringList(*processArguments)).toJson(QJsonDocument::Compact);
        std::cout.write(bytes.constData(), bytes.size());
        return 0;
    }
    {
        const QString unicodePath = QStringLiteral("C:/report 漢字 🧪/with space");
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--argument-roundtrip", unicodePath});
        check(child.waitForFinished(3000) && child.exitCode() == 0, "real process argument fixture completes");
        const auto decoded = QJsonDocument::fromJson(child.readAllStandardOutput()).array();
        check(decoded.size() == 2 && decoded[1].toString() == unicodePath,
            "early dispatch preserves an actual Unicode report path before QApplication exists");
    }
    check(parseInstallationArguments({}).mode == InstallationStartupMode::normal,
        "ordinary startup remains ordinary");
    check(parseInstallationArguments({"-platform", "windows"}).mode == InstallationStartupMode::normal,
        "unrelated existing Qt arguments remain untouched");
    const auto parsed = parseInstallationArguments(
        {"--check-installation", "--report-directory", "C:/owned report"});
    check(parsed.mode == InstallationStartupMode::controller && parsed.reportDirectory == "C:/owned report",
        "explicit public diagnostic preserves the exact report argument");
    check(parseInstallationArguments({"--installation-check-child", "--report-directory", "/owned"}).mode
            == InstallationStartupMode::child,
        "internal owned child role is admitted separately");
    for ( const QStringList args : {QStringList{"--check-installation"},
              QStringList{"--check-installation", "--report-directory", ""},
              QStringList{"--check-installation", "--report-directory", "/owned", "extra"},
              QStringList{"--check-installation", "--installation-check-child", "/owned"}} )
    {
        const auto refused = parseInstallationArguments(args);
        check(refused.mode == InstallationStartupMode::refused && !refused.error.isEmpty(),
            "malformed diagnostic invocation refuses before normal startup");
    }

    QTemporaryDir temporary;
    check(temporary.isValid(), "owned test scratch exists");
    const QString report = temporary.filePath("report");
    QString error;
    check(createInstallationReportDirectory(report, &error) && QDir(report).exists(),
        "report directory is exclusively created beneath an existing parent");
    QFile sentinel(report + "/sentinel");
    if ( sentinel.open(QIODevice::WriteOnly) )
    {
        sentinel.write("unchanged");
        sentinel.close();
    }
    check(!createInstallationReportDirectory(report, &error) && !error.isEmpty(),
        "existing report destination is refused");
    check(!createInstallationReportDirectory(temporary.filePath("missing/report"), &error),
        "missing report parent is not recursively created");
    check(!createInstallationReportDirectory("relative-report", &error),
        "relative report target refuses");
    check(!createInstallationReportDirectory(temporary.filePath("escape/../report2"), &error),
        "report traversal is not silently normalized");
    check(sentinel.open(QIODevice::ReadOnly) && sentinel.readAll() == "unchanged",
        "refusal preserves existing destination bytes");

    QImage before(160, 120, QImage::Format_ARGB32);
    QImage after(160, 120, QImage::Format_ARGB32);
    before.fill(Qt::black);
    after.fill(Qt::black);
    {
        QPainter first(&before);
        first.fillRect(QRect(25, 45, 31, 31), Qt::white);
        QPainter second(&after);
        second.fillRect(QRect(105, 45, 31, 31), Qt::white);
    }
    const InstallationFrameProbes probes{{0.25, 0.5}, {0.75, 0.5}, {0.5, 0.2}};
    check(assessInstallationFrames(before, after, probes, &error),
        "two hand-constructed bright geometry states pass their independent interior/background probes");
    check(!assessInstallationFrames(before, before, probes, &error),
        "a repeated stale frame cannot pass");
    QImage empty(160, 120, QImage::Format_ARGB32);
    empty.fill(Qt::black);
    check(!assessInstallationFrames(empty, empty, probes, &error), "blank frames cannot pass");
    empty.fill(Qt::white);
    check(!assessInstallationFrames(empty, empty, probes, &error), "uniform white frames cannot pass");
    check(!assessInstallationFrames(before, after.scaled(80, 60), probes, &error),
        "mismatched physical dimensions refuse");
    check(!assessInstallationFrames(before, after,
            {{std::numeric_limits<double>::quiet_NaN(), 0.5}, probes.secondFace, probes.background}, &error),
        "nonfinite projected probe refuses");
    check(!assessInstallationFrames(before, after, {{-0.1, 0.5}, probes.secondFace, probes.background}, &error),
        "off-frame projection refuses rather than clamping");
    check(!assessInstallationFrames(before, after, {probes.firstFace, probes.firstFace, probes.background}, &error),
        "overlapping probe regions cannot claim distinct states");
    std::cout << "installation contract failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
