#include "ui/project/project_playtest_request.hpp"

#include <QJsonArray>
#include <QRegularExpression>

#include <stdexcept>

namespace sed::ui::project
{
QJsonObject makeProjectPlaytestRequest(const QJsonObject& machine, const QJsonObject& recipe,
    const QString& game, const QString& level, const QString& archive, const QString& archiveSha256)
{
    const auto launch = recipe.value("launch").toObject();
    const QString episode = launch.value("episode").toString();
    static const QRegularExpression identifier("^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$");
    if ( (game != "jk" && game != "mots") || launch.value("game") != game
        || launch.value("map") != level || !identifier.match(episode).hasMatch() )
        throw std::runtime_error("The selected scenario must launch the reviewed game, episode and level.");
    if ( !machine.value("files").isArray() )
        throw std::runtime_error("Choose a local Engine Capture request with explicit engine, configuration and stock inputs.");
    QJsonArray files;
    for ( const auto& value : machine.value("files").toArray() )
    {
        const auto row = value.toObject();
        const auto role = row.value("role").toString();
        if ( role == "scenario" ) continue;
        if ( role != "engine" && role != "config" && role != "resource" )
            throw std::runtime_error("Unknown input role in the local Engine Capture request.");
        files.append(row);
    }
    const auto destination = "episode/" + episode + (game == "jk" ? ".gob" : ".goo");
    files.append(QJsonObject{{"source", archive}, {"destination", destination},
        {"role", "scenario"}, {"sha256", archiveSha256}});
    QJsonObject request{{"schemaVersion", 1}, {"files", files},
        {"episode", episode}, {"map", level}, {"episodeArchive", destination}, {"motsCompat", game == "mots"},
        {"graphicsBackend", machine.value("graphicsBackend").toString("xvfb")}, {"gameplay", recipe}};
    for ( const auto* field : {"engineTimeoutMs", "runnerTimeoutMs", "logLimitBytes"} )
        if ( machine.contains(field) ) request.insert(field, machine.value(field));
    return request;
}

QString projectPlaytestStatus(const QJsonObject& receipt, const QString& archiveSha256, bool current)
{
    const auto archive = receipt.value("episodeArchive").toObject();
    if ( archive.value("archiveSha256") != archiveSha256 || archive.value("looseEpisodeFilesStaged") != false )
        return "unverified";
    if ( receipt.value("status") == "failed" ) return "failed";
    if ( receipt.value("status") != "completed" || receipt.value("captureMode") != "gameplay"
        || receipt.value("processExitCode").toInt(-1) != 0 || receipt.value("immutableInputsUnchanged") != true
        || receipt.value("gameplay").toObject().value("status") != "passed" ) return "unverified";
    return current ? "passed" : "historical";
}
}
