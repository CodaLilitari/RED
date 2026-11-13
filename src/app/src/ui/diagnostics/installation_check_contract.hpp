#pragma once

#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <optional>

namespace sed::ui::diagnostics
{

enum class InstallationStartupMode
{
    normal,
    controller,
    child,
    refused,
};

struct InstallationArguments
{
    InstallationStartupMode mode{ InstallationStartupMode::normal };
    QString reportDirectory{};
    QString error{};
};

struct InstallationFrameProbes
{
    QPointF firstFace{};
    QPointF secondFace{};
    QPointF background{};
};

InstallationArguments parseInstallationArguments(const QStringList& arguments);
std::optional<QStringList> installationProcessArguments(int argc, char** argv);
bool createInstallationReportDirectory(const QString& directory, QString* error = nullptr);
bool assessInstallationFrames(
    const QImage& before, const QImage& after,
    const InstallationFrameProbes& probes, QString* error = nullptr);

} // namespace sed::ui::diagnostics
