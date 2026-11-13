#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <optional>

namespace sed::ui::diagnostics
{
QStringList installationRuntimeDataNames();
std::optional<QJsonArray> inspectInstallationRuntimeData(
    const QString& dataDirectory, QString* error = nullptr);
}
