#include "engine_capture_dialog.hpp"
#include "../../../../core/src/assets/atomic_file_write.hpp"
#include "ui/components/preview_worker_pool.hpp"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <filesystem>
#include <span>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sed::ui::project
{
namespace
{
constexpr qint64 kJsonLimit = 2 * 1024 * 1024;
constexpr qint64 kReceiptLimit = 8 * 1024 * 1024;
constexpr int kFileLimit = 512;

std::filesystem::path filePath(const QString& value)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::u8path(value.toUtf8().toStdString());
#endif
}
QByteArray readBounded(const QString& path, qint64 limit)
{
    QFile file(path);
    if ( !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error(("Cannot read file: " + path).toStdString());
    }
    const auto bytes = file.read(limit + 1);
    if ( file.error() != QFileDevice::NoError || bytes.size() > limit || !file.atEnd() )
    {
        throw std::runtime_error(("File is unreadable or exceeds the read limit: " + path).toStdString());
    }
    return bytes;
}
QJsonObject readObject(const QByteArray& bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if ( error.error != QJsonParseError::NoError || !document.isObject() )
    {
        throw std::runtime_error(("Invalid JSON object: " + error.errorString()).toStdString());
    }
    return document.object();
}
struct Prepared
{
    QJsonObject request;
    QByteArray bytes;
    QString python, runner, requestPath;
    std::shared_ptr<QTemporaryDir> directory;
};
struct Preparation
{
    std::shared_ptr<Prepared> prepared;
    QString error;
};
std::shared_ptr<Preparation> prepareRequest(QJsonObject request, QString importedPath, QString python, QString runner,
    detail::PreviewWorkerPublicationGuard guard)
{
    auto result = std::make_shared<Preparation>();
    try
    {
        QElapsedTimer elapsed;
        elapsed.start();
        auto active = [&]
        {
            if ( guard.canceled->load() || elapsed.elapsed() > 30000 )
            {
                throw std::runtime_error("Request preparation cancelled or exceeded its 30-second work budget.");
            }
        };
        if ( !importedPath.isEmpty() )
        {
            request = readObject(readBounded(importedPath, kJsonLimit));
            const auto files = request["files"].toArray();
            if ( files.size() > kFileLimit )
            {
                throw std::runtime_error("GUI request exceeds 512 explicit files.");
            }
            for ( const auto& value : files )
            {
                if ( !QFileInfo(value.toObject()["source"].toString()).isAbsolute() )
                {
                    throw std::runtime_error("Imported source paths must be absolute; relative paths depend on the "
                                             "original caller's working directory.");
                }
            }
        }
        else
        {
            QJsonArray files;
            for ( const auto& value : request["files"].toArray() )
            {
                active();
                auto item = value.toObject();
                const QString source = item["source"].toString();
                QFile file(source);
                if ( !QFileInfo(source).isFile() || QFileInfo(source).isSymLink() || !file.open(QIODevice::ReadOnly) )
                {
                    throw std::runtime_error(("Source is missing, linked, or unreadable: " + source).toStdString());
                }
                QCryptographicHash hash(QCryptographicHash::Sha256);
                while ( !file.atEnd() )
                {
                    active();
                    const auto chunk = file.read(1024 * 1024);
                    if ( file.error() != QFileDevice::NoError || (chunk.isEmpty() && !file.atEnd()) )
                    {
                        throw std::runtime_error(("Cannot capture source hash: " + source).toStdString());
                    }
                    hash.addData(chunk);
                }
                item["sha256"] = QString::fromLatin1(hash.result().toHex());
                files.append(item);
            }
            request["files"] = files;
        }
        active();
        auto prepared = std::make_shared<Prepared>();
        prepared->request = request;
        prepared->bytes = QJsonDocument(request).toJson();
        if ( prepared->bytes.size() > kJsonLimit )
        {
            throw std::runtime_error("GUI request exceeds 2 MiB.");
        }
        prepared->python = std::move(python);
        prepared->runner = std::move(runner);
        prepared->directory = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/red-capture-gui-XXXXXX");
        if ( !prepared->directory->isValid() )
        {
            throw std::runtime_error("Cannot create an external temporary request directory.");
        }
        prepared->requestPath = prepared->directory->filePath("request.json");
        QFile file(prepared->requestPath);
        if ( !file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
             file.write(prepared->bytes) != prepared->bytes.size() )
        {
            throw std::runtime_error("Cannot write the complete temporary capture request.");
        }
        file.close();
        active();
        result->prepared = std::move(prepared);
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    return result;
}
struct ProcessState
{
    std::shared_ptr<Prepared> prepared;
    QByteArray output, error;
    QString destination;
    std::uint64_t generation{};
    bool run{}, overflow{}, timeout{};
};
void drainProcess(QProcess& process, ProcessState& state)
{
    for ( auto channel : { QProcess::StandardOutput, QProcess::StandardError } )
    {
        process.setReadChannel(channel);
        auto& buffer = channel == QProcess::StandardOutput ? state.output : state.error;
        while ( process.bytesAvailable() > 0 )
        {
            const auto chunk = process.read(65536);
            const auto remaining = std::max<qint64>(0, kJsonLimit * 2 - buffer.size());
            buffer.append(chunk.left(remaining));
            state.overflow |= chunk.size() > remaining;
        }
    }
}
} // namespace

class EngineCaptureDialog::Impl
{
public:
    Impl(EngineCaptureDialog& dialog, Context context);
    ~Impl()
    {
        close();
        QObject::disconnect(watcher_, nullptr, &dialog_, nullptr);
    }
    void close()
    {
        closed_ = true;
        cancel();
    }

private:
    QLineEdit* line(const char* name)
    {
        auto* result = new QLineEdit(&dialog_);
        result->setObjectName(name);
        return result;
    }
    QPushButton* button(const char* label, const char* name)
    {
        auto* result = new QPushButton(QString::fromUtf8(label), &dialog_);
        result->setObjectName(name);
        return result;
    }
    QSpinBox* spin(const char* name, int low, int high, int value)
    {
        auto* result = new QSpinBox(&dialog_);
        result->setObjectName(name);
        result->setRange(low, high);
        result->setValue(value);
        return result;
    }
    QDoubleSpinBox* real(const char* name, double low, double high, double value)
    {
        auto* result = new QDoubleSpinBox(&dialog_);
        result->setObjectName(name);
        result->setDecimals(17);
        result->setRange(low, high);
        result->setValue(value);
        return result;
    }
    QHBoxLayout* pathRow(QLineEdit* edit, const char* label, bool directory);
    void update();
    void invalidate();
    void cancel();
    bool addRow(const QString& source, const QString& destination, const QString& role, const QString& hash);
    void requiredRows();
    QJsonObject request() const;
    void prepare(bool importing);
    void preparedResult();
    void startProcess(std::shared_ptr<Prepared> prepared, bool run);
    void finished(const std::shared_ptr<ProcessState>& state, int code, bool normal);
    QString diagnostic(const ProcessState& state) const;
    void populate(const QJsonObject& request);
    void exportRequest();
    void run();
    void inspectReceipt(const QString& path);

    EngineCaptureDialog& dialog_;
    Context context_;
    bool busy_{}, closed_{}, applying_{}, canRun_{};
    std::uint64_t generation_{};
    detail::PreviewWorkerPublicationGuard guard_{};
    std::shared_ptr<Prepared> prepared_;
    std::shared_ptr<ProcessState> state_;
    QPointer<QProcess> process_;
    QFutureWatcher<std::shared_ptr<Preparation>>* watcher_{};
    QLineEdit *python_{}, *runner_{}, *episode_{}, *map_{}, *requestPath_{}, *output_{}, *receiptPath_{};
    QComboBox *profile_{}, *backend_{};
    QSpinBox *width_{}, *height_{}, *sector_{}, *frame_{}, *engineTime_{}, *runnerTime_{}, *logBytes_{};
    std::array<QDoubleSpinBox*, 6> coordinates_{};
    QDoubleSpinBox* fov_{};
    QCheckBox* mots_{};
    QTableWidget* files_{};
    QTabWidget* tabs_{};
    QPlainTextEdit* receipt_{};
    QLabel *status_{}, *image_{};
    QPushButton *prepare_{}, *run_{}, *cancel_{}, *import_{}, *export_{}, *required_{};
};

EngineCaptureDialog::Impl::Impl(EngineCaptureDialog& dialog, Context context)
    : dialog_(dialog), context_(std::move(context))
{
    dialog_.setWindowTitle("Engine capture request and results");
    const auto available = dialog_.screen()->availableGeometry().size() - QSize(40, 80);
    dialog_.resize(QSize(1000, 640).boundedTo(available));
    auto* layout = new QVBoxLayout(&dialog_);
    auto* warning =
        new QLabel("Capture-v1 requires a compatible capture engine and explicit owned inputs; an ordinary retail "
                   "executable is not a substitute. Every capture's game acceptance remains unverified.",
            &dialog_);
    warning->setObjectName("captureWarning");
    warning->setWordWrap(true);
    layout->addWidget(warning);
#ifdef Q_OS_MACOS
    auto* unsupported = new QLabel(
        "macOS: prepare requests and inspect receipts only. The runner has no macOS engine profile.", &dialog_);
    unsupported->setWordWrap(true);
    layout->addWidget(unsupported);
#endif
    auto* tools = new QFormLayout;
    python_ = line("capturePython");
    runner_ = line("captureRunner");
    python_->setText(
        context_.pythonExecutable.isEmpty() ? QStandardPaths::findExecutable("python3") : context_.pythonExecutable);
    if ( python_->text().isEmpty() )
    {
        python_->setText(QStandardPaths::findExecutable("python"));
    }
    runner_->setText(context_.runnerScript.isEmpty()
                         ? QCoreApplication::applicationDirPath() + "/tools/engine-capture/invoke_engine_capture.py"
                         : context_.runnerScript);
    tools->addRow("Python executable", pathRow(python_, "Choose Python…", false));
    tools->addRow("Capture runner script", pathRow(runner_, "Choose runner…", false));
    layout->addLayout(tools);
    tabs_ = new QTabWidget(&dialog_);
    tabs_->setObjectName("captureTabs");
    layout->addWidget(tabs_, 1);
    auto* requestPage = new QWidget(tabs_);
    auto* form = new QFormLayout(requestPage);
    profile_ = new QComboBox(requestPage);
    profile_->setObjectName("captureProfile");
    profile_->addItem("Windows capture engine", "windows");
    profile_->addItem("Linux capture engine", "linux");
#ifndef Q_OS_WIN
    profile_->setCurrentIndex(1);
#endif
    backend_ = new QComboBox(requestPage);
    backend_->setObjectName("captureBackend");
    backend_->addItem("Private Xvfb (default)", "xvfb");
    backend_->addItem("SDL offscreen", "sdl-offscreen");
    form->addRow("Engine profile", profile_);
    form->addRow("Linux graphics backend", backend_);
    episode_ = line("captureEpisode");
    episode_->setText("capture");
    map_ = line("captureMap");
    map_->setText("level.jkl");
    form->addRow("Episode name", episode_);
    form->addRow("Map filename", map_);
    width_ = spin("captureWidth", 64, 2048, 512);
    height_ = spin("captureHeight", 64, 2048, 512);
    form->addRow("Width (pixels)", width_);
    form->addRow("Height (pixels)", height_);
    sector_ = spin("captureSector", 0, 1000000, 0);
    form->addRow("Camera sector", sector_);
    const char* names[] = { "captureX", "captureY", "captureZ", "capturePitch", "captureYaw", "captureRoll" };
    const char* labels[] = { "Position X", "Position Y", "Position Z", "Pitch (degrees)", "Yaw (degrees)",
        "Roll (degrees)" };
    for ( int i = 0; i < 6; ++i )
    {
        coordinates_[i] = real(names[i], -1000000, 1000000, 0);
        form->addRow(labels[i], coordinates_[i]);
    }
    fov_ = real("captureFov", 20, 140, 90);
    form->addRow("Horizontal field of view", fov_);
    frame_ = spin("captureFrame", 1, 120, 3);
    form->addRow("Capture frame", frame_);
    engineTime_ = spin("captureEngineTimeout", 1000, 60000, 30000);
    form->addRow("Engine timeout (ms)", engineTime_);
    runnerTime_ = spin("captureRunnerTimeout", 1000, 90000, 45000);
    form->addRow("Total runner work budget (ms)", runnerTime_);
    logBytes_ = spin("captureLogBytes", 1024, 16 * 1024 * 1024, 1024 * 1024);
    form->addRow("Aggregate retained log bytes", logBytes_);
    mots_ = new QCheckBox("MotS compatibility", requestPage);
    mots_->setObjectName("captureMots");
    mots_->setChecked(true);
    form->addRow(mots_);
    auto* requestScroll = new QScrollArea(tabs_);
    requestScroll->setObjectName("captureRequestScroll");
    requestScroll->setWidgetResizable(true);
    requestScroll->setFrameShape(QFrame::NoFrame);
    requestScroll->setWidget(requestPage);
    tabs_->addTab(requestScroll, "Request");
    auto* filePage = new QWidget(tabs_);
    auto* filesLayout = new QVBoxLayout(filePage);
    auto* hint = new QLabel("Declare every engine/runtime/config/scenario/resource input. Source paths are explicit; "
                            "SHA-256 is captured by Prepare. No game directories are scanned or copied implicitly.",
        filePage);
    hint->setWordWrap(true);
    filesLayout->addWidget(hint);
    files_ = new QTableWidget(0, 4, filePage);
    files_->setObjectName("captureFiles");
    files_->setHorizontalHeaderLabels({ "Source file", "Staged destination", "Role", "SHA-256" });
    files_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    files_->setColumnWidth(0, 310);
    files_->setColumnWidth(1, 250);
    files_->setColumnWidth(2, 90);
    files_->setColumnWidth(3, 300);
    filesLayout->addWidget(files_);
    auto* fileActions = new QHBoxLayout;
    required_ = button("Add required rows", "captureAddRequired");
    auto* add = button("Add file row", "captureAddFile");
    auto* remove = button("Remove row", "captureRemoveFile");
    auto* choose = button("Choose source for row…", "captureChooseSource");
    for ( auto* action : { required_, add, remove, choose } )
    {
        fileActions->addWidget(action);
    }
    fileActions->addStretch();
    filesLayout->addLayout(fileActions);
    tabs_->addTab(filePage, "Explicit files");
    auto* resultPage = new QWidget(tabs_);
    auto* resultLayout = new QVBoxLayout(resultPage);
    receiptPath_ = line("captureReceiptPath");
    auto* receiptRow = new QHBoxLayout;
    receiptRow->addWidget(receiptPath_);
    auto* inspect = button("Inspect receipt", "captureInspect");
    auto* browseReceipt = button("Choose receipt…", "captureChooseReceipt");
    receiptRow->addWidget(inspect);
    receiptRow->addWidget(browseReceipt);
    resultLayout->addLayout(receiptRow);
    image_ = new QLabel(resultPage);
    image_->setObjectName("captureImage");
    image_->setAlignment(Qt::AlignCenter);
    image_->setMinimumHeight(120);
    resultLayout->addWidget(image_);
    receipt_ = new QPlainTextEdit(resultPage);
    receipt_->setObjectName("captureReceipt");
    receipt_->setReadOnly(true);
    resultLayout->addWidget(receipt_, 1);
    tabs_->addTab(resultPage, "Receipt and image");
    auto* paths = new QFormLayout;
    requestPath_ = line("captureRequestPath");
    auto* requestRow = new QHBoxLayout;
    requestRow->addWidget(requestPath_);
    import_ = button("Import request", "captureImport");
    export_ = button("Export new request", "captureExport");
    requestRow->addWidget(import_);
    requestRow->addWidget(export_);
    auto* chooseRequest = button("Choose…", "captureChooseRequest");
    requestRow->addWidget(chooseRequest);
    paths->addRow("Request JSON path", requestRow);
    output_ = line("captureOutput");
    output_->setPlaceholderText("Fresh output directory; existing output is never replaced");
    paths->addRow("Capture output directory", pathRow(output_, "Choose parent…", true));
    layout->addLayout(paths);
    status_ = new QLabel("Declare inputs, prepare the request, then run or export it.", &dialog_);
    status_->setObjectName("captureStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    auto* actions = new QHBoxLayout;
    prepare_ = button("Prepare / validate inputs", "capturePrepare");
    run_ = button("Run capture", "captureRun");
    cancel_ = button("Cancel work", "captureCancel");
    for ( auto* action : { prepare_, run_, cancel_ } )
    {
        actions->addWidget(action);
    }
    actions->addStretch();
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog_);
    actions->addWidget(close);
    layout->addLayout(actions);
    watcher_ = new QFutureWatcher<std::shared_ptr<Preparation>>(&dialog_);
    QObject::connect(
        watcher_, &QFutureWatcher<std::shared_ptr<Preparation>>::finished, &dialog_, [this] { preparedResult(); });
    QObject::connect(prepare_, &QPushButton::clicked, &dialog_, [this] { prepare(false); });
    QObject::connect(import_, &QPushButton::clicked, &dialog_, [this] { prepare(true); });
    QObject::connect(export_, &QPushButton::clicked, &dialog_, [this] { exportRequest(); });
    QObject::connect(run_, &QPushButton::clicked, &dialog_, [this] { run(); });
    QObject::connect(cancel_, &QPushButton::clicked, &dialog_, [this] { cancel(); });
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog_, &QDialog::reject);
    QObject::connect(required_, &QPushButton::clicked, &dialog_, [this] { requiredRows(); });
    QObject::connect(add, &QPushButton::clicked, &dialog_,
        [this]
        {
            if ( addRow({}, "resource/", "resource", {}) )
            {
                invalidate();
            }
        });
    QObject::connect(remove, &QPushButton::clicked, &dialog_,
        [this]
        {
            if ( files_->currentRow() >= 0 )
            {
                files_->removeRow(files_->currentRow());
                invalidate();
            }
        });
    QObject::connect(choose, &QPushButton::clicked, &dialog_,
        [this]
        {
            const int row = files_->currentRow();
            if ( row < 0 )
            {
                status_->setText("Select a file row first.");
                return;
            }
            const QString selected =
                QFileDialog::getOpenFileName(&dialog_, "Choose declared source", context_.initialDirectory);
            if ( !selected.isEmpty() )
            {
                files_->item(row, 0)->setText(selected);
            }
        });
    QObject::connect(inspect, &QPushButton::clicked, &dialog_, [this] { inspectReceipt(receiptPath_->text()); });
    QObject::connect(browseReceipt, &QPushButton::clicked, &dialog_,
        [this]
        {
            const QString selected = QFileDialog::getOpenFileName(
                &dialog_, "Choose launcher receipt", context_.initialDirectory, "JSON (*.json)");
            if ( !selected.isEmpty() )
            {
                receiptPath_->setText(selected);
                inspectReceipt(selected);
            }
        });
    QObject::connect(chooseRequest, &QPushButton::clicked, &dialog_,
        [this]
        {
            const QString selected =
                QFileDialog::getSaveFileName(&dialog_, "Choose request path for import or new export",
                    context_.initialDirectory, "JSON (*.json)", nullptr, QFileDialog::DontConfirmOverwrite);
            if ( !selected.isEmpty() )
            {
                requestPath_->setText(selected);
            }
        });
    for ( auto* edit : { python_, runner_, episode_, map_ } )
    {
        QObject::connect(edit, &QLineEdit::textChanged, &dialog_, [this] { invalidate(); });
    }
    for ( auto* value : { width_, height_, sector_, frame_, engineTime_, runnerTime_, logBytes_ } )
    {
        QObject::connect(value, &QSpinBox::valueChanged, &dialog_, [this] { invalidate(); });
    }
    for ( auto* value : coordinates_ )
    {
        QObject::connect(value, &QDoubleSpinBox::valueChanged, &dialog_, [this] { invalidate(); });
    }
    QObject::connect(fov_, &QDoubleSpinBox::valueChanged, &dialog_, [this] { invalidate(); });
    QObject::connect(mots_, &QCheckBox::toggled, &dialog_, [this] { invalidate(); });
    QObject::connect(profile_, &QComboBox::currentIndexChanged, &dialog_, [this] { invalidate(); });
    QObject::connect(backend_, &QComboBox::currentIndexChanged, &dialog_, [this] { invalidate(); });
    QObject::connect(files_, &QTableWidget::cellChanged, &dialog_, [this] { invalidate(); });
    update();
}

QHBoxLayout* EngineCaptureDialog::Impl::pathRow(QLineEdit* edit, const char* label, bool directory)
{
    auto* row = new QHBoxLayout;
    row->addWidget(edit);
    auto* choose = new QPushButton(QString::fromUtf8(label), &dialog_);
    row->addWidget(choose);
    QObject::connect(choose, &QPushButton::clicked, &dialog_,
        [this, edit, directory]
        {
            QString selected = directory
                                   ? QFileDialog::getExistingDirectory(
                                         &dialog_, "Choose parent of new capture directory", context_.initialDirectory)
                                   : QFileDialog::getOpenFileName(&dialog_, "Choose tool", context_.initialDirectory);
            if ( !selected.isEmpty() )
            {
                edit->setText(directory ? QDir(selected).filePath("new-capture") : selected);
            }
        });
    return row;
}
void EngineCaptureDialog::Impl::update()
{
    prepare_->setEnabled(!busy_ && !closed_);
    import_->setEnabled(!busy_ && !closed_);
    export_->setEnabled(!busy_ && !closed_ && prepared_ != nullptr);
    run_->setEnabled(!busy_ && !closed_ && prepared_ != nullptr && canRun_);
    cancel_->setEnabled(busy_ && !closed_);
    required_->setEnabled(!busy_ && files_->rowCount() == 0);
    backend_->setEnabled(profile_->currentData() == "linux");
    const bool running = process_ && state_ && state_->run;
    tabs_->widget(0)->setEnabled(!running);
    tabs_->widget(1)->setEnabled(!running);
    python_->setEnabled(!running);
    runner_->setEnabled(!running);
    output_->setEnabled(!running);
}
void EngineCaptureDialog::Impl::invalidate()
{
    if ( applying_ )
    {
        return;
    }
    ++generation_;
    detail::cancelPreviewWorkerPublication(guard_);
    prepared_.reset();
    canRun_ = false;
    if ( process_ && state_ && state_->run )
    {
        process_->closeWriteChannel();
    }
    status_->setText("Inputs changed; prepare or import the request again.");
    update();
}
void EngineCaptureDialog::Impl::cancel()
{
    ++generation_;
    detail::cancelPreviewWorkerPublication(guard_);
    if ( process_ )
    {
        if ( state_->run )
        {
            process_->write("cancel\n");
            process_->closeWriteChannel();
        }
        else
        {
            process_->kill(); // Validation-only mode cannot own an engine/worker tree.
        }
    }
    if ( status_ )
    {
        status_->setText("Cancellation requested; waiting for owned work to settle.");
    }
}
bool EngineCaptureDialog::Impl::addRow(
    const QString& source, const QString& destination, const QString& role, const QString& hash)
{
    if ( files_->rowCount() >= kFileLimit )
    {
        status_->setText("GUI requests support at most 512 explicit files.");
        return false;
    }
    const int row = files_->rowCount();
    files_->insertRow(row);
    const QString values[] = { source, destination, role, hash };
    for ( int column = 0; column < 4; ++column )
    {
        auto* item = new QTableWidgetItem(values[column]);
        if ( column == 3 )
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }
        files_->setItem(row, column, item);
    }
    return true;
}
void EngineCaptureDialog::Impl::requiredRows()
{
    if ( files_->rowCount() != 0 )
    {
        return;
    }
    applying_ = true;
    if ( profile_->currentData() == "windows" )
    {
        addRow({}, "openjkdf2-64.exe", "engine", {});
        addRow({}, "OpenAL32.dll", "runtime", {});
        addRow({}, "libGameNetworkingSockets.dll", "runtime", {});
    }
    else
    {
        addRow({}, "openjkdf2", "engine", {});
    }
    addRow({}, "registry.json", "config", {});
    addRow({}, "openjkdf2_cvars.json", "config", {});
    addRow({}, "episode/" + episode_->text() + "/episode.jk", "scenario", {});
    addRow({}, "episode/" + episode_->text() + "/jkl/" + map_->text(), "scenario", {});
    applying_ = false;
    invalidate();
    tabs_->setCurrentIndex(1);
}
QJsonObject EngineCaptureDialog::Impl::request() const
{
    QJsonArray position, rotation;
    for ( int i = 0; i < 3; ++i )
    {
        position.append(coordinates_[i]->value());
        rotation.append(coordinates_[i + 3]->value());
    }
    QJsonObject camera{ { "width", width_->value() }, { "height", height_->value() }, { "sector", sector_->value() },
        { "position", position }, { "pitchYawRoll", rotation }, { "horizontalFov", fov_->value() } };
    QJsonArray files;
    for ( int row = 0; row < files_->rowCount(); ++row )
    {
        auto text = [&](int column)
        { return files_->item(row, column) ? files_->item(row, column)->text() : QString{}; };
        files.append(QJsonObject{ { "source", text(0).isEmpty() ? QString{} : QFileInfo(text(0)).absoluteFilePath() },
            { "destination", text(1) }, { "role", text(2) }, { "sha256", text(3) } });
    }
    QJsonObject result{ { "schemaVersion", 1 }, { "episode", episode_->text() }, { "map", map_->text() },
        { "camera", camera }, { "files", files }, { "frame", frame_->value() },
        { "engineTimeoutMs", engineTime_->value() }, { "runnerTimeoutMs", runnerTime_->value() },
        { "logLimitBytes", logBytes_->value() }, { "motsCompat", mots_->isChecked() } };
    if ( profile_->currentData() == "linux" )
    {
        result["graphicsBackend"] = backend_->currentData().toString();
    }
    return result;
}
void EngineCaptureDialog::Impl::prepare(bool importing)
{
    if ( busy_ )
    {
        return;
    }
    invalidate();
    QString python = python_->text();
    if ( !QFileInfo(python).isAbsolute() )
    {
        python = QStandardPaths::findExecutable(python);
    }
    if ( python.isEmpty() || !QFileInfo(python).isFile() )
    {
        status_->setText(
            "Python executable is missing. Select an installed Python interpreter; RED does not install dependencies.");
        return;
    }
    const QString runner = QFileInfo(runner_->text()).absoluteFilePath();
    if ( !QFileInfo(runner).isFile() )
    {
        status_->setText("Capture runner script is missing. Select invoke_engine_capture.py from a RED distribution or "
                         "source checkout.");
        return;
    }
    if ( importing && requestPath_->text().isEmpty() )
    {
        status_->setText("Choose an existing request JSON path to import.");
        return;
    }
    guard_ = detail::makePreviewWorkerPublicationGuard(generation_);
    busy_ = true;
    status_->setText(importing ? "Reading request for authoritative validation…" : "Capturing explicit input hashes…");
    update();
    watcher_->setFuture(detail::runPreviewWorkerTask(
        prepareRequest, request(), importing ? requestPath_->text() : QString{}, python, runner, guard_));
}
void EngineCaptureDialog::Impl::preparedResult()
{
    const auto result = watcher_->result();
    busy_ = false;
    if ( closed_ || !detail::previewWorkerMayPublish(guard_, generation_) )
    {
        result->prepared.reset();
        update();
        return;
    }
    if ( !result->error.isEmpty() )
    {
        status_->setText(result->error);
        update();
        return;
    }
    auto prepared = std::move(result->prepared);
    startProcess(std::move(prepared), false);
}
void EngineCaptureDialog::Impl::startProcess(std::shared_ptr<Prepared> prepared, bool run)
{
    auto state = std::make_shared<ProcessState>();
    state->prepared = std::move(prepared);
    state->run = run;
    state->generation = generation_;
    state->destination = run ? QFileInfo(output_->text()).absoluteFilePath() : QString{};
    // Self-owned until completion: parenting this process to the dialog would
    // kill the supervisor before it can release its separate POSIX worker group.
    // Dialog/application close sends EOF; Python retains ownership of cleanup.
    auto* process = new QProcess;
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    process_ = process;
    state_ = state;
    busy_ = true;
    QObject::connect(
        process, &QProcess::readyReadStandardOutput, process, [process, state] { drainProcess(*process, *state); });
    QObject::connect(
        process, &QProcess::readyReadStandardError, process, [process, state] { drainProcess(*process, *state); });
    QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), &dialog_,
        [this, process, state](int code, QProcess::ExitStatus exit)
        {
            drainProcess(*process, *state);
            finished(state, code, exit == QProcess::NormalExit);
        });
    QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process, [process] { process->deleteLater(); });
    QObject::connect(process, &QProcess::errorOccurred, &dialog_,
        [this, process](QProcess::ProcessError error)
        {
            if ( error == QProcess::FailedToStart )
            {
                busy_ = false;
                process_ = nullptr;
                status_->setText("Could not start Python: " + process->errorString());
                update();
            }
        });
    QObject::connect(process, &QProcess::errorOccurred, process,
        [process](QProcess::ProcessError error)
        {
            if ( error == QProcess::FailedToStart )
            {
                process->deleteLater();
            }
        });
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, process,
        [process, run]
        {
            if ( run )
            {
                process->closeWriteChannel();
            }
            else
            {
                process->kill();
            }
        });
    auto* timeout = new QTimer(process);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, process,
        [process, state]
        {
            state->timeout = true;
            if ( state->run )
            {
                process->closeWriteChannel();
            }
            else
            {
                process->kill();
            }
        });
    QStringList arguments{ "-B", state->prepared->runner, state->prepared->requestPath };
    if ( run )
    {
        arguments << "--output" << state->destination << "--cancel-on-stdin";
    }
    else
    {
        arguments << "--validate-only";
    }
    process->setWorkingDirectory(state->prepared->directory->path());
    process->start(state->prepared->python, arguments);
    timeout->start(run ? state->prepared->request["runnerTimeoutMs"].toInt(45000) + 5000 : 30000);
    status_->setText(run ? "Capture running. Cancel closes its ownership channel; acceptance remains unverified."
                         : "Validating with the authoritative capture runner…");
    update();
}
void EngineCaptureDialog::Impl::finished(const std::shared_ptr<ProcessState>& state, int code, bool normal)
{
    busy_ = false;
    process_ = nullptr;
    if ( closed_ )
    {
        return;
    }
    if ( state->run )
    {
        receipt_->clear();
        image_->clear();
        if ( !normal || (code != 0 && code != 1) || state->timeout || state->overflow )
        {
            status_->setText(
                "Capture refused or interrupted; no new receipt is associated with this run. " + diagnostic(*state));
            update();
            return;
        }
        try
        {
            const auto reply = readObject(state->output);
            const QString path = QDir(state->destination).filePath("launcher-receipt.json");
            const QString reported = QFileInfo(reply["receipt"].toString()).absoluteFilePath();
            if ( reply["status"] != (code == 0 ? "completed" : "failed") ||
                 QDir::cleanPath(reported) != QDir::cleanPath(path) )
            {
                throw std::runtime_error("Runner response does not identify this run's receipt.");
            }
            receiptPath_->setText(path);
            inspectReceipt(path);
        }
        catch ( const std::exception& error )
        {
            status_->setText(QString::fromUtf8(error.what()));
        }
        update();
        return;
    }
    if ( state->generation != generation_ )
    {
        update();
        return;
    }
    if ( !normal || code != 0 || state->overflow || state->timeout )
    {
        status_->setText(diagnostic(*state));
        update();
        return;
    }
    try
    {
        const auto report = readObject(state->output);
        if ( report["status"] != "validated" || !report["request"].isObject() )
        {
            throw std::runtime_error("Runner did not return a validated request.");
        }
        populate(report["request"].toObject());
        prepared_ = state->prepared;
        canRun_ = report["canRunHere"].toBool();
#ifdef Q_OS_MACOS
        canRun_ = false;
#endif
        status_->setText(
            canRun_
                ? "Prepared request validated. Run rechecks these exact source hashes; capture acceptance remains "
                  "unverified."
                : "Request validated for preparation/inspection only; this engine profile cannot run on this host.");
    }
    catch ( const std::exception& error )
    {
        status_->setText(QString::fromUtf8(error.what()));
    }
    update();
}
QString EngineCaptureDialog::Impl::diagnostic(const ProcessState& state) const
{
    if ( state.timeout )
    {
        return "Runner operation exceeded its GUI wait budget.";
    }
    if ( state.overflow )
    {
        return "Runner response exceeded the bounded GUI diagnostic budget.";
    }
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(state.error, &error);
    if ( error.error == QJsonParseError::NoError && json.isObject() )
    {
        return json.object()["error"].toString();
    }
    return QString::fromUtf8(state.error.left(8192));
}
void EngineCaptureDialog::Impl::populate(const QJsonObject& request)
{
    applying_ = true;
    profile_->setCurrentIndex(profile_->findData(request["engineProfile"].toString()));
    backend_->setCurrentIndex(backend_->findData(request["graphicsBackend"].toString("xvfb")));
    episode_->setText(request["episode"].toString());
    map_->setText(request["map"].toString());
    const auto camera = request["camera"].toObject();
    width_->setValue(camera["width"].toInt());
    height_->setValue(camera["height"].toInt());
    sector_->setValue(camera["sector"].toInt());
    fov_->setValue(camera["horizontalFov"].toDouble());
    for ( int i = 0; i < 3; ++i )
    {
        coordinates_[i]->setValue(camera["position"].toArray()[i].toDouble());
        coordinates_[i + 3]->setValue(camera["pitchYawRoll"].toArray()[i].toDouble());
    }
    frame_->setValue(request["frame"].toInt());
    engineTime_->setValue(request["engineTimeoutMs"].toInt());
    runnerTime_->setValue(request["runnerTimeoutMs"].toInt());
    logBytes_->setValue(request["logLimitBytes"].toInt());
    mots_->setChecked(request["motsCompat"].toBool());
    files_->setRowCount(0);
    for ( const auto& value : request["files"].toArray() )
    {
        const auto item = value.toObject();
        addRow(item["source"].toString(), item["destination"].toString(), item["role"].toString(),
            item["sha256"].toString());
    }
    applying_ = false;
}
void EngineCaptureDialog::Impl::exportRequest()
{
    if ( busy_ || !prepared_ )
    {
        return;
    }
    try
    {
        if ( requestPath_->text().isEmpty() )
        {
            throw std::runtime_error("Choose a new request JSON output path.");
        }
        assets::detail::StagedAssetFile staged(filePath(requestPath_->text()));
        staged.writeAndClose(
            std::as_bytes(std::span(prepared_->bytes.constData(), static_cast<std::size_t>(prepared_->bytes.size()))));
        staged.publishNew(filePath(requestPath_->text()));
        status_->setText("Created request: " + requestPath_->text());
    }
    catch ( const std::exception& error )
    {
        status_->setText(QString::fromUtf8(error.what()));
    }
}
void EngineCaptureDialog::Impl::run()
{
    if ( busy_ || !prepared_ || !canRun_ )
    {
        return;
    }
    if ( output_->text().trimmed().isEmpty() )
    {
        status_->setText("Choose a fresh capture output directory.");
        return;
    }
    startProcess(prepared_, true);
}
void EngineCaptureDialog::Impl::inspectReceipt(const QString& path)
{
    image_->clear();
    receipt_->clear();
    try
    {
        const auto object = readObject(readBounded(path, kReceiptLimit));
        if ( !object["status"].isString() )
        {
            throw std::runtime_error("Receipt has no reported status.");
        }
        receipt_->setPlainText(QString::fromUtf8(QJsonDocument(object).toJson()));
        status_->setText("Reported status: " + object["status"].toString() + ". Game acceptance remains unverified. " +
                         object["error"].toString());
        tabs_->setCurrentIndex(2);
        if ( object["validPpm"].toBool() )
        {
            QByteArray bytes = readBounded(QFileInfo(path).dir().filePath("capture.ppm"), 2097152 * 3 + 128);
            if ( QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) !=
                 object["imageSha256"].toString() )
            {
                throw std::runtime_error("Receipt image hash does not match; pixels are not displayed.");
            }
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PPM");
            const auto size = reader.size();
            if ( size.width() < 64 || size.width() > 2048 || size.height() < 64 || size.height() > 2048 ||
                 static_cast<qint64>(size.width()) * size.height() > 2097152 )
            {
                throw std::runtime_error("Receipt image exceeds the capture dimension budget.");
            }
            const auto image = reader.read();
            if ( image.isNull() )
            {
                throw std::runtime_error("Receipt image could not be decoded.");
            }
            image_->setPixmap(
                QPixmap::fromImage(image.scaled(640, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }
    catch ( const std::exception& error )
    {
        status_->setText(QString::fromUtf8(error.what()));
    }
}

EngineCaptureDialog::EngineCaptureDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}
EngineCaptureDialog::~EngineCaptureDialog() = default;
void EngineCaptureDialog::done(int result)
{
    impl_->close();
    QDialog::done(result);
}
} // namespace sed::ui::project
