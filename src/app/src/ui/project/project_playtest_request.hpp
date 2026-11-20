#pragma once

#include <QJsonObject>
#include <QString>

namespace sed::ui::project
{
QJsonObject makeProjectPlaytestRequest(const QJsonObject& machine, const QJsonObject& recipe,
    const QString& game, const QString& level, const QString& archive, const QString& archiveSha256);
QString projectPlaytestStatus(const QJsonObject& receipt, const QString& archiveSha256, bool current);
}
