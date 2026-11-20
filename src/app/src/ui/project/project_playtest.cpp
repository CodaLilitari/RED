#include "ui/project/project_playtest.hpp"

#include "ui/components/preview_worker_pool.hpp"
#include "ui/project/engine_capture_dialog.hpp"
#include "ui/project/project_playtest_request.hpp"
#include "project_profile.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <memory>
#include <span>
#include <stdexcept>

namespace sed::ui
{
namespace
{
namespace fs = std::filesystem;
namespace campaign = ::sed::project;
constexpr qsizetype kMetadataBudget = 4 * 1024 * 1024;

QByteArray readFile(const QString& name, qsizetype maximum)
{
    QFile file(name);
    const QFileInfo info(file);
    if ( !info.isFile() || info.isSymLink() || file.size() > maximum || !file.open(QIODevice::ReadOnly) )
        throw std::runtime_error("Cannot read a regular project test input within its size limit.");
    auto bytes = file.read(maximum + 1);
    if ( bytes.size() > maximum || file.error() != QFileDevice::NoError || !file.atEnd() )
        throw std::runtime_error("Project test input changed or exceeded its size limit.");
    return bytes;
}
QJsonObject object(const QByteArray& bytes)
{
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if ( error.error != QJsonParseError::NoError || !json.isObject() )
        throw std::runtime_error("Expected a JSON object for the project test.");
    return json.object();
}
QString digest(const QByteArrayView bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }

void writeNew(const QString& name, QByteArrayView data)
{
    const fs::path path(name.toStdU16String());
    assets::detail::StagedAssetFile file(path);
    file.writeAndClose(std::as_bytes(std::span(data.data(), static_cast<std::size_t>(data.size()))));
    file.publishNew(path);
}

struct Run
{
    fs::path profile, stock;
    campaign::LiveExport live;
    QByteArray stamp;
    QString scenario, requestFile, archiveSha, error;
    std::string revision;
    std::shared_ptr<QTemporaryDir> inputs;
    QJsonObject report;
    QStringList review;
};

void prepareRun(Run& run, const QString& machine)
{
    auto plan = campaign::prepare(run.profile, run.stock, &run.live);
    const auto chosen = std::find_if(plan.scenarios.begin(), plan.scenarios.end(),
        [&](const auto& entry) { return QString::fromStdString(entry.name) == run.scenario; });
    if ( chosen == plan.scenarios.end() || chosen->level != plan.liveLevel )
        throw std::runtime_error("The scenario must use the level currently open in the editor.");
    if ( std::count_if(plan.scenarios.begin(), plan.scenarios.end(), [&](const auto& item) { return item.name == chosen->name; }) != 1 )
        throw std::runtime_error("Give each scenario a unique name before testing.");
    if ( plan.target.engine != "OpenJKDF2" || plan.target.renderer != "OpenGL" )
        throw std::runtime_error("This gameplay adapter supports an OpenJKDF2 project with an OpenGL renderer.");
    const auto recipe = QByteArray::fromStdString(chosen->definitionBytes);
    const auto archive = run.inputs->filePath(plan.target.game == "jk" ? "campaign.gob" : "campaign.goo");
    const auto package = QByteArrayView(plan.package.data(), static_cast<qsizetype>(plan.package.size()));
    run.archiveSha = digest(package);
    writeNew(archive, package);
    const auto request = project::makeProjectPlaytestRequest(object(readFile(machine, kMetadataBudget)), object(recipe),
        QString::fromStdString(plan.target.game), QString::fromStdString(plan.liveLevel), archive, run.archiveSha);
    run.requestFile = run.inputs->filePath("request.json");
    writeNew(run.requestFile, QJsonDocument(request).toJson());
    QJsonArray resources, warnings;
    for ( const auto& resource : plan.resources )
    {
        const auto name = QString::fromStdString(resource.name);
        const auto winner = QString::fromStdString(resource.winner);
        resources.append(QJsonObject{{"name", name}, {"source", winner},
            {"sha256", QString::fromStdString(resource.sha256)}, {"suppliedByGame", resource.suppliedByGame}});
        run.review << name + " — " + winner;
    }
    for ( const auto& warning : plan.warnings )
    {
        const auto message = QString::fromStdString(warning);
        run.review << message;
        warnings.append(message);
    }
    run.revision = plan.revision;
    run.report = {{"schemaVersion", 1}, {"scenario", run.scenario}, {"level", QString::fromStdString(plan.liveLevel)},
        {"editorRevision", QString::fromLatin1(run.stamp)}, {"projectRevision", QString::fromStdString(plan.revision)},
        {"exportSha256", digest(QByteArrayView(run.live.bytes.data(), static_cast<qsizetype>(run.live.bytes.size())))},
        {"scenarioFileSha256", digest(recipe)}, {"packageSha256", run.archiveSha},
        {"target", QJsonObject{{"game", QString::fromStdString(plan.target.game)}, {"engine", QString::fromStdString(plan.target.engine)},
            {"renderer", QString::fromStdString(plan.target.renderer)}}}, {"plannedResources", resources},
        {"packageWarnings", warnings}, {"runtimeResourceResolutionObserved", false}};
}

void observe(QWidget* owner, QLabel* status, const std::shared_ptr<Run>& run,
    const PlaytestEditor& editor, const QString& receiptPath)
{
    // Compare all project inputs in the worker again. The editor stamp is sampled when the worker returns.
    auto* watcher = new QFutureWatcher<bool>(owner);
    QObject::connect(watcher, &QFutureWatcherBase::finished, owner, [=]
    {
        const bool projectCurrent = watcher->result();
        watcher->deleteLater();
        try
        {
            const auto bytes = readFile(receiptPath, kMetadataBudget);
            const auto receipt = object(bytes);
            const bool editorCurrent = editor.revision() == run->stamp;
            const auto result = project::projectPlaytestStatus(receipt, run->archiveSha, projectCurrent && editorCurrent);
            auto report = run->report;
            report["status"] = result;
            report["editorCurrentAtReview"] = editorCurrent;
            report["projectCurrentAtReview"] = projectCurrent;
            report["launcherReceipt"] = "launcher-receipt.json";
            report["launcherReceiptSha256"] = digest(bytes);
            const auto directory = QFileInfo(receiptPath).absoluteDir();
            report["stagedManifestSha256"] = digest(readFile(directory.filePath("manifest-before.json"), kMetadataBudget));
            for ( const auto* key : {"engineProfile","captureMode","processExitCode","timedOut","runnerTimedOut",
                "requestedGraphicsBackend","graphicsBackendObserved","effectiveCameraObserved",
                "runtimeResourceResolutionObserved","gameplay","cleanupComplete"} )
                if ( receipt.contains(key) ) report[key] = receipt[key];
            const auto out = directory.filePath("project-test.json");
            writeNew(out, QJsonDocument(report).toJson());
            status->setText(QObject::tr("Scenario %1 · %2 · %3").arg(run->scenario, result, out));
        }
        catch ( const std::exception& error ) { status->setText(QString::fromUtf8(error.what())); }
    });
    watcher->setFuture(detail::runPreviewWorkerTask([run]
    {
        try { return campaign::prepare(run->profile, run->stock, &run->live).revision == run->revision; }
        catch ( const std::exception& ) { return false; }
    }));
}

void reviewRun(QWidget* owner, QLabel* status, const std::shared_ptr<Run>& run, const PlaytestEditor& editor)
{
    auto* review = new QMessageBox(QMessageBox::Question, QObject::tr("Review test inputs"),
        QObject::tr("Run %1 on the current export?\nPackage SHA-256 %2\n%3 resolved resources. "
            "Runtime resource reads are unobserved.").arg(run->scenario, run->archiveSha)
            .arg(run->report["plannedResources"].toArray().size()),
        QMessageBox::Ok | QMessageBox::Cancel, owner);
    review->setDetailedText(run->review.join('\n'));
    review->setDefaultButton(QMessageBox::Cancel);
    QObject::connect(review, &QDialog::finished, owner, [=](int decision)
    {
        review->deleteLater();
        if ( decision != QMessageBox::Ok ) { run->inputs.reset(); return; }
        if ( editor.revision() != run->stamp ) { status->setText(QObject::tr("The reviewed editor revision has changed.")); return; }
        const auto parent = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/playtests";
        if ( !QDir().mkpath(parent) ) { status->setText(QObject::tr("Cannot create the playtest results folder.")); return; }
        project::EngineCaptureDialog::Context context;
        context.projectRequest = run->requestFile;
        context.projectOutput = QDir(parent).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        context.projectInputs = run->inputs;
        context.projectStillCurrent = [editor, run] { return editor.revision() == run->stamp; };
        context.projectReceipt = [=](const QString& path)
        {
            observe(owner, status, run, editor, path);
            return QObject::tr("Observed scenario %1. Project Cockpit is checking its source revision.").arg(run->scenario);
        };
        auto* capture = new project::EngineCaptureDialog(std::move(context), owner);
        capture->setAttribute(Qt::WA_DeleteOnClose);
        capture->show();
        status->setText(QObject::tr("Testing %1. Close the capture window to return to this editor.").arg(run->scenario));
    });
    review->open();
}
} // namespace

void beginProjectPlaytest(QWidget* owner, QLabel* status, const fs::path& profile, const fs::path& stockRoot,
    const QString& scenarioName, const QString& machineRequest, const PlaytestEditor& editor)
{
    if ( !editor.source || !editor.revision || !editor.exportMap ) return;
    try
    {
        auto run = std::make_shared<Run>();
        run->inputs = std::make_shared<QTemporaryDir>();
        if ( !run->inputs->isValid() ) throw std::runtime_error("Cannot create an owned staging directory.");
        run->profile = profile; run->stock = stockRoot; run->scenario = scenarioName;
        run->live.source = editor.source();
        if ( run->live.source.empty() ) throw std::runtime_error("Open a level from this project before testing.");
        run->stamp = editor.revision();
        const QString exportName = run->inputs->filePath("live.jkl");
        const QPointer<QWidget> guard(owner);
        if ( !editor.exportMap(fs::path(exportName.toStdU16String())) || !guard || editor.revision() != run->stamp )
        {
            if ( guard ) status->setText(QObject::tr("The live export was cancelled or its document changed."));
            return;
        }
        run->live.bytes = readFile(exportName, 64 * 1024 * 1024).toStdString();
        status->setText(QObject::tr("Checking the current level, project resources and scenario…"));
        auto* watcher = new QFutureWatcher<void>(owner);
        QObject::connect(watcher, &QFutureWatcherBase::finished, owner, [=]
        {
            watcher->deleteLater();
            if ( !run->error.isEmpty() ) { run->inputs.reset(); status->setText(run->error); return; }
            if ( editor.revision() != run->stamp )
            {
                run->inputs.reset();
                status->setText(QObject::tr("The editor changed during test preparation."));
                return;
            }
            reviewRun(owner, status, run, editor);
        });
        watcher->setFuture(detail::runPreviewWorkerTask([run, machineRequest]
        {
            try { prepareRun(*run, machineRequest); }
            catch ( const std::exception& error ) { run->error = QString::fromUtf8(error.what()); }
        }));
    }
    catch ( const std::exception& error ) { status->setText(QString::fromUtf8(error.what())); }
}
} // namespace sed::ui
