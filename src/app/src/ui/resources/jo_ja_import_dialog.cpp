#include "jo_ja_import_dialog.hpp"

#include "../../../../core/src/assets/atomic_file_write.hpp"
#include "sed/assets/jo_ja_import.hpp"
#include "sed/assets/raven_bsp.hpp"
#include "ui/components/preview_worker_pool.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <array>

namespace sed::ui::resources
{
namespace
{
using Guard = detail::PreviewWorkerPublicationGuard;
using Stage = assets::detail::StagedAssetFile;
enum class Work
{
    inspect,
    prepare,
    publish
};

struct Mapping
{
    std::string shader;
    QString file, name;
};
struct Request
{
    QString source, palette, output;
    std::vector<Mapping> mappings;
    assets::JoJaImportOptions options;
    QByteArray inspectedHash;
};
struct Input
{
    QString path, canonical;
    QByteArray hash;
    qint64 size{};
};
struct Prepared
{
    std::vector<Input> inputs;
    std::string archive;
    QString report, extension;
};
struct Result
{
    std::shared_ptr<Prepared> prepared;
    std::unique_ptr<Stage> stage;
    QString destination, report, error;
    QList<QByteArray> shaders;
    QByteArray sourceHash;
};

// The owner may have changed a file while the completed stage waited for its GUI callback.
// Verify the exact bytes again after ownership admission. Read bounded slices so the event
// loop can accept edits, Cancel and Close even when captures approach the total input cap.
struct FinalVerification
{
    std::unique_ptr<Stage> stage;
    QString destination;
    QFile file;
    QCryptographicHash digest{QCryptographicHash::Sha256};
    std::vector<QDateTime> checkedTimes;
    QDateTime openedTime;
    qint64 read{};

    bool advance(const std::vector<Input>& inputs)
    {
        const Input& input = inputs.at(checkedTimes.size());
        const QFileInfo info(input.path);
        if ( !info.isFile() || info.size() != input.size || info.canonicalFilePath() != input.canonical )
        {
            throw std::runtime_error("A captured input changed before publication. Prepare again.");
        }
        if ( !file.isOpen() )
        {
            file.setFileName(input.path);
            if ( !file.open(QIODevice::ReadOnly) )
            {
                throw std::runtime_error(file.errorString().toStdString());
            }
            openedTime = info.lastModified();
            read = 0;
            digest.reset();
        }
        if ( read < input.size )
        {
            const auto block = file.read(std::min<qint64>(256 * 1024, input.size - read));
            if ( block.isEmpty() || file.error() != QFileDevice::NoError )
            {
                throw std::runtime_error("Cannot re-read a captured input.");
            }
            read += block.size();
            digest.addData(block);
        }
        if ( read < input.size )
        {
            return false;
        }
        if ( !file.atEnd() || digest.result() != input.hash )
        {
            throw std::runtime_error("A captured input changed before publication. Prepare again.");
        }
        file.close();
        checkedTimes.push_back(openedTime);
        if ( checkedTimes.size() != inputs.size() )
        {
            return false;
        }
        for ( std::size_t i = 0; i < inputs.size(); ++i )
        {
            const QFileInfo finalInfo(inputs[i].path);
            if ( !finalInfo.isFile() || finalInfo.size() != inputs[i].size ||
                 finalInfo.canonicalFilePath() != inputs[i].canonical || finalInfo.lastModified() != checkedTimes[i] )
            {
                throw std::runtime_error("A captured input changed during final verification. Prepare again.");
            }
        }
        return true;
    }
};

void active(const Guard& guard)
{
    if ( !detail::previewWorkerMayPublish(guard, guard.generation) )
    {
        throw std::runtime_error("Import cancelled.");
    }
}

std::string capture(const QString& path, qint64 limit, const Guard& guard, Input& identity)
{
    active(guard);
    const QFileInfo before(path);
    if ( path.isEmpty() || path.contains(QChar::Null) || !before.isFile() || before.size() < 0 ||
         before.size() > limit )
    {
        throw std::runtime_error(("Missing input or capture size limit exceeded: " + path).toStdString());
    }
    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error(file.errorString().toStdString());
    }
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(before.size()));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while ( !file.atEnd() )
    {
        active(guard);
        const auto chunk = file.read(1024 * 1024);
        if ( chunk.isEmpty() || chunk.size() > before.size() - static_cast<qint64>(bytes.size()) )
        {
            throw std::runtime_error("Input changed or a read failed during capture.");
        }
        bytes.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
        hash.addData(chunk);
    }
    const QFileInfo after(path);
    if ( file.error() != QFileDevice::NoError || before.size() != static_cast<qint64>(bytes.size()) ||
         !after.isFile() || after.size() != before.size() || after.lastModified() != before.lastModified() ||
         after.canonicalFilePath() != before.canonicalFilePath() )
    {
        throw std::runtime_error("Input changed during capture. Inspect and prepare again.");
    }
    identity = {before.absoluteFilePath(), before.canonicalFilePath(), hash.result(), before.size()};
    return bytes;
}

void verify(const std::vector<Input>& inputs, const Guard& guard)
{
    for ( const auto& original : inputs )
    {
        Input current;
        capture(original.path, original.size, guard, current);
        if ( current.hash != original.hash || current.canonical != original.canonical )
        {
            throw std::runtime_error("A captured input changed. Inspect and prepare again.");
        }
    }
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().toStdString());
#endif
}

std::span<const std::byte> asBytes(const std::string& value)
{
    return std::as_bytes(std::span(value.data(), value.size()));
}

std::shared_ptr<Result> run(Work mode, Request request, std::shared_ptr<const Prepared> reviewed, Guard guard)
{
    auto result = std::make_shared<Result>();
    try
    {
        if ( mode == Work::publish )
        {
            active(guard);
            const QFileInfo target(request.output);
            if ( request.output.contains(QChar::Null) ||
                 target.suffix().compare(reviewed->extension, Qt::CaseInsensitive) != 0 )
            {
                throw std::runtime_error(("Choose a new ." + reviewed->extension + " archive.").toStdString());
            }
            if ( target.exists() || target.isSymLink() )
            {
                throw std::runtime_error("Output already exists. Choose a new filename.");
            }
            for ( const auto& input : reviewed->inputs )
            {
                if ( target.absoluteFilePath() == input.path || target.absoluteFilePath() == input.canonical )
                {
                    throw std::runtime_error("The output aliases an input file.");
                }
            }
            verify(reviewed->inputs, guard);
            auto stage = std::make_unique<Stage>(nativePath(target.absoluteFilePath()));
            stage->writeAndClose(asBytes(reviewed->archive));
            active(guard);
            result->destination = target.absoluteFilePath();
            result->stage = std::move(stage);
            return result;
        }
        Input source;
        if ( QFileInfo(request.source).suffix().compare("bsp", Qt::CaseInsensitive) != 0 )
        {
            throw std::runtime_error("Select a native Raven .bsp map.");
        }
        if ( mode == Work::prepare )
        {
            // Refuse oversized selections before reading hundreds of MiB. Capture independently charges the
            // observed bytes again because the selected paths can change after this inexpensive size check.
            qint64 available = assets::kJoJaMaxCapturedInputBytes;
            const auto charge = [&](const QString& path)
            {
                const auto size = QFileInfo(path).size();
                if ( size < 0 || size > available )
                {
                    throw std::runtime_error("Selected files exceed the 256 MiB combined input capture limit.");
                }
                available -= size;
            };
            charge(request.source);
            for ( const auto& mapping : request.mappings )
            {
                charge(mapping.file);
            }
            if ( !request.palette.isEmpty() )
            {
                charge(request.palette);
            }
        }
        const auto bytes = capture(request.source, assets::kRavenBspMaxBytes, guard, source);
        if ( mode == Work::inspect )
        {
            const auto bsp = assets::parseRavenBsp(asBytes(bytes));
            QSet<QByteArray> names;
            for ( const auto& shader : bsp.shaders )
            {
                const auto name = QByteArray::fromStdString(shader.name);
                if ( !names.contains(name) )
                {
                    names.insert(name);
                    result->shaders.append(name);
                }
            }
            result->sourceHash = source.hash;
            result->report = QStringLiteral("%1 shaders, %2 source brushes and %3 submodels.\nSource SHA-256  %4")
                                 .arg(result->shaders.size())
                                 .arg(bsp.brushCount)
                                 .arg(bsp.models.empty() ? 0 : bsp.models.size() - 1)
                                 .arg(QString::fromLatin1(source.hash.toHex()));
            return result;
        }
        if ( !request.inspectedHash.isEmpty() && request.inspectedHash != source.hash )
        {
            throw std::runtime_error("The inspected BSP changed. Inspect its shaders again.");
        }
        auto prepared = std::make_shared<Prepared>();
        prepared->inputs.push_back(source);
        if ( request.mappings.size() > assets::kJoJaMaxMaterials )
        {
            throw std::runtime_error("The importer accepts up to 256 material mappings.");
        }
        qint64 capturedRemaining = assets::kJoJaMaxCapturedInputBytes - source.size;
        qint64 remaining = std::min<qint64>(assets::kJoJaMaxCapturedMaterialBytes, capturedRemaining);
        const auto captureAsset = [&](const QString& path, qint64 limit)
        {
            Input input;
            auto data = capture(path, std::min(limit, remaining), guard, input);
            remaining -= input.size;
            capturedRemaining -= input.size;
            prepared->inputs.push_back(std::move(input));
            return data;
        };
        for ( const auto& mapping : request.mappings )
        {
            request.options.materials.push_back({mapping.shader, mapping.name.toStdString(),
                                                 captureAsset(mapping.file, assets::kJoJaMaxMaterialBytes)});
        }
        if ( !request.palette.isEmpty() )
        {
            Input palette;
            request.options.palette = capture(
                request.palette, std::min<qint64>(assets::kJoJaMaxPaletteBytes, capturedRemaining), guard, palette);
            prepared->inputs.push_back(std::move(palette));
        }
        active(guard);
        auto converted = assets::prepareJoJaImport(asBytes(bytes), request.options);
        verify(prepared->inputs, guard);
        prepared->archive = std::move(converted.archive.outputData);
        prepared->extension = request.options.target == ProjectType::MOTS ? "goo" : "gob";
        if ( request.options.geometryMode == assets::JoJaGeometryMode::HybridSectors )
        {
            prepared->report = QStringLiteral("Native sectors %1 · Portal pairs %2 · Native textured faces %3\n"
                                              "Residual model faces %4 · Equivalent triangles %5\nNative boundaries collide. "
                                              "Residual model collision is %6.\n\n")
                                   .arg(converted.sectors).arg(converted.portalPairs).arg(converted.nativeFaces)
                                   .arg(converted.residualFaces).arg(converted.residualTriangles)
                                   .arg(request.options.renderMeshCollision ? "on" : "off");
        }
        prepared->report += QString::fromStdString(converted.report) + "\n\nCaptured inputs (SHA-256)\n";
        for ( const auto& input : prepared->inputs )
        {
            prepared->report += input.path + "  " + QString::fromLatin1(input.hash.toHex()) + '\n';
        }
        prepared->report += "Output SHA-256  " + QString::fromStdString(converted.outputSha256) +
                            "\nManifest SHA-256  " + QString::fromStdString(converted.manifestSha256);
        result->prepared = std::move(prepared);
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    return result;
}

template <class T> T* named(const char* name)
{
    auto* widget = new T;
    widget->setObjectName(QString::fromLatin1(name));
    return widget;
}
} // namespace

class JoJaImportDialog::Impl
{
  public:
    Impl(JoJaImportDialog& dialog, Context context) : dialog_(dialog), context_(std::move(context))
    {
        dialog.setWindowTitle("Import JO/JA Map");
        dialog.resize(760, 740);
        auto* layout = new QVBoxLayout(&dialog);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* form = new QFormLayout(body);
        scroll->setWidget(body);
        layout->addWidget(scroll);
        const auto addPath = [&](const QString& label, const char* name, const QString& filter, bool save = false)
        {
            auto* line = named<QLineEdit>(name);
            auto* row = new QHBoxLayout;
            auto* browse = new QPushButton("Browse…");
            row->addWidget(line);
            row->addWidget(browse);
            form->addRow(label, row);
            QObject::connect(
                browse, &QPushButton::clicked, &dialog,
                [this, line, filter, save]
                {
                    const QPointer<JoJaImportDialog> owner(&dialog_);
                    const auto before = generation_;
                    const QString beforeText = line->text();
                    auto useFilter = filter;
                    if ( save )
                    {
                        useFilter = target_->currentIndex() ? "GOO archive (*.goo)" : "GOB archive (*.gob)";
                    }
                    const auto start = line->text().isEmpty() ? context_.initialDirectory : line->text();
                    const auto value =
                        save ? QFileDialog::getSaveFileName(&dialog_, "Create new archive", start, useFilter)
                             : QFileDialog::getOpenFileName(&dialog_, "Choose import input", start, useFilter);
                    if ( owner && !closed_ && before == generation_ && line->text() == beforeText && !value.isEmpty() )
                    {
                        line->setText(value);
                    }
                });
            return line;
        };
        source_ = addPath("Raven BSP", "joJaSource", "JO/JA maps (*.bsp)");
        target_ = named<QComboBox>("joJaTarget");
        target_->addItems({"Jedi Knight (GOB)", "Mysteries of the Sith (GOO)"});
        form->addRow("Target game", target_);
        level_ = named<QLineEdit>("joJaLevelName");
        level_->setText("joja_scene");
        form->addRow("Level basename", level_);
        scale_ = named<QDoubleSpinBox>("joJaScale");
        scale_->setDecimals(10);
        scale_->setRange(0.0000000001, 1.0);
        scale_->setValue(1.0 / 320.0);
        form->addRow("Scale (JK units per source unit)", scale_);
        patch_ = named<QSpinBox>("joJaPatch");
        patch_->setRange(1, 16);
        patch_->setValue(4);
        form->addRow("Patch subdivisions", patch_);
        geometry_ = named<QComboBox>("joJaGeometryMode");
        geometry_->addItems({"Model scene (one enclosing sector)", "Hybrid sectors (native rooms, portals and model detail)"});
        form->addRow("Geometry", geometry_);
        chunk_ = named<QSpinBox>("joJaChunk");
        chunk_->setRange(1, 170);
        chunk_->setValue(140);
        form->addRow("Faces per 3DO", chunk_);
        triangles_ = named<QSpinBox>("joJaMaxTriangles");
        triangles_->setRange(1, 100000);
        triangles_->setValue(100000);
        form->addRow("Source triangle limit", triangles_);
        models_ = named<QSpinBox>("joJaMaxModels");
        models_->setRange(1, 512);
        models_->setValue(512);
        form->addRow("Model limit", models_);
        sectors_ = named<QSpinBox>("joJaMaxSectors");
        sectors_->setRange(1, 96);
        sectors_->setValue(64);
        form->addRow("Sector limit", sectors_);
        fragments_ = named<QSpinBox>("joJaMaxFragments");
        fragments_->setRange(1, 1000000);
        fragments_->setValue(500000);
        form->addRow("Geometry fragment limit", fragments_);
        form->setRowVisible(sectors_, false);
        form->setRowVisible(fragments_, false);
        placeholders_ = named<QCheckBox>("joJaPlaceholders");
        placeholders_->setText("Generate checker MATs for unmapped shaders");
        collision_ = named<QCheckBox>("joJaCollision");
        collision_->setText("Make rendered model triangles collidable");
        form->addRow("Materials", placeholders_);
        form->addRow("Model collision", collision_);
        audio_ = named<QComboBox>("joJaAudio");
        audio_->addItems({"JK/MotS equivalents (preferred)", "Original JO/JA sounds (unavailable)"});
        auto* audioModel = qobject_cast<QStandardItemModel*>(audio_->model());
        audioModel->item(1)->setEnabled(false);
        audio_->setToolTip(
            "Stock JK/MotS footsteps are available for supported surfaces. Original JO/JA sounds require "
            "verified source-audio mappings. No JO/JA audio files are copied.");
        form->addRow("Audio policy", audio_);
        palette_ = addPath("Palette for indexed MATs", "joJaPalette", "Colormaps (*.cmp)");
        inspect_ = named<QPushButton>("joJaInspect");
        inspect_->setText("Inspect shaders");
        form->addRow(inspect_);
        shaders_ = named<QTableWidget>("joJaShaders");
        shaders_->setColumnCount(4);
        shaders_->setHorizontalHeaderLabels({"Source shader", "Target MAT file", "MAT basename", "Skip"});
        shaders_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        shaders_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        form->addRow(shaders_);
        auto* browseMat = new QPushButton("Choose MAT for selected shader…");
        form->addRow(browseMat);
        output_ = addPath("New archive", "joJaOutput", {}, true);
        report_ = named<QPlainTextEdit>("joJaReport");
        report_->setReadOnly(true);
        form->addRow("Review", report_);
        status_ = named<QLabel>("joJaStatus");
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto* actions = new QDialogButtonBox(QDialogButtonBox::Close);
        const auto action = [&](const QString& text, const char* name)
        {
            auto* button = actions->addButton(text, QDialogButtonBox::ActionRole);
            button->setObjectName(QString::fromLatin1(name));
            return button;
        };
        prepare_ = action("Prepare", "joJaPrepare");
        create_ = action("Create new archive", "joJaCreate");
        cancel_ = action("Cancel work", "joJaCancel");
        layout->addWidget(actions);
        QObject::connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(inspect_, &QPushButton::clicked, &dialog, [this] { start(Work::inspect); });
        QObject::connect(prepare_, &QPushButton::clicked, &dialog, [this] { start(Work::prepare); });
        QObject::connect(create_, &QPushButton::clicked, &dialog, [this] { start(Work::publish); });
        QObject::connect(cancel_, &QPushButton::clicked, &dialog,
                         [this]
                         {
                             invalidate();
                             status_->setText("Cancelled.");
                         });
        QObject::connect(browseMat, &QPushButton::clicked, &dialog, [this] { chooseMat(); });
        QObject::connect(source_, &QLineEdit::textChanged, &dialog,
                         [this]
                         {
                             inspectedHash_.clear();
                             shaders_->setRowCount(0);
                             invalidate();
                         });
        const auto change = [this] { invalidate(); };
        QObject::connect(geometry_, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [this, form](int index)
        {
            const bool hybrid = index == 1;
            const std::size_t selected = hybrid ? 1 : 0;
            const auto previous = 1 - selected;
            chunkByMode_[previous] = chunk_->value();
            trianglesByMode_[previous] = triangles_->value();
            const QSignalBlocker chunkSignals(chunk_);
            const QSignalBlocker triangleSignals(triangles_);
            chunk_->setMaximum(hybrid ? 479 : 170);
            triangles_->setMaximum(hybrid ? 250000 : 100000);
            chunk_->setValue(chunkByMode_[selected]);
            triangles_->setValue(trianglesByMode_[selected]);
            collision_->setText(hybrid ? "Collide with residual model detail (native boundaries always collide)"
                                       : "Make rendered model triangles collidable");
            form->setRowVisible(sectors_, hybrid);
            form->setRowVisible(fragments_, hybrid);
            invalidate();
        });
        QObject::connect(level_, &QLineEdit::textChanged, &dialog, change);
        QObject::connect(palette_, &QLineEdit::textChanged, &dialog, change);
        QObject::connect(target_, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, change);
        QObject::connect(audio_, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, change);
        QObject::connect(scale_, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, change);
        QObject::connect(patch_, qOverload<int>(&QSpinBox::valueChanged), &dialog, change);
        for ( auto* limit : { chunk_, triangles_, models_, sectors_, fragments_ } )
        {
            QObject::connect(limit, qOverload<int>(&QSpinBox::valueChanged), &dialog, change);
        }
        QObject::connect(placeholders_, &QCheckBox::toggled, &dialog, change);
        QObject::connect(collision_, &QCheckBox::toggled, &dialog, change);
        QObject::connect(shaders_, &QTableWidget::itemChanged, &dialog, change);
        QObject::connect(output_, &QLineEdit::textChanged, &dialog,
                         [this]
                         {
                             if ( busy_ && work_ == Work::publish )
                             {
                                 invalidate();
                             }
                             update();
                         });
        QObject::connect(&watcher_, &QFutureWatcherBase::finished, &dialog, [this] { complete(); });
        update();
    }

    void close()
    {
        closed_ = true;
        invalidate();
    }
    ~Impl()
    {
        detail::cancelPreviewWorkerPublication(guard_);
    }

  private:
    void chooseMat()
    {
        const auto row = shaders_->currentRow();
        if ( row < 0 )
        {
            status_->setText("Select a source shader first.");
            return;
        }
        const QPointer<JoJaImportDialog> owner(&dialog_);
        const auto before = generation_;
        const auto path =
            QFileDialog::getOpenFileName(&dialog_, "Choose target MAT", context_.initialDirectory, "Materials (*.mat)");
        if ( !owner || closed_ || before != generation_ || path.isEmpty() )
        {
            return;
        }
        shaders_->item(row, 1)->setText(path);
        if ( shaders_->item(row, 2)->text().isEmpty() )
        {
            shaders_->item(row, 2)->setText(QFileInfo(path).fileName());
        }
    }

    void invalidate()
    {
        ++generation_;
        detail::cancelPreviewWorkerPublication(guard_);
        if ( final_ )
        {
            final_.reset();
            busy_ = false;
        }
        prepared_.reset();
        report_->clear();
        status_->clear();
        update();
    }

    void update()
    {
        inspect_->setEnabled(!busy_ && !source_->text().isEmpty());
        prepare_->setEnabled(!busy_ && !source_->text().isEmpty());
        create_->setEnabled(!busy_ && prepared_ && !output_->text().isEmpty());
        cancel_->setEnabled(busy_);
    }

    bool current()
    {
        const QPointer<JoJaImportDialog> owner(&dialog_);
        const auto callback = context_.isCurrent;
        bool ok = true;
        try
        {
            if ( callback )
            {
                ok = callback();
            }
        }
        catch ( ... )
        {
            ok = false;
        }
        if ( !owner )
        {
            return false;
        }
        if ( !ok || closed_ )
        {
            invalidate();
            status_->setText("The owning document changed. Reopen the importer.");
            return false;
        }
        return true;
    }

    void start(Work mode)
    {
        if ( busy_ || !current() || (mode == Work::publish && !prepared_) )
        {
            return;
        }
        if ( mode != Work::publish )
        {
            invalidate();
        }
        Request request;
        request.source = source_->text();
        request.palette = palette_->text();
        request.output = output_->text();
        request.inspectedHash = inspectedHash_;
        auto& options = request.options;
        options.target = target_->currentIndex() ? ProjectType::MOTS : ProjectType::JKDF2;
        options.audioPolicy =
            audio_->currentIndex() ? assets::JoJaAudioPolicy::SourceImport : assets::JoJaAudioPolicy::StockEquivalents;
        options.levelName = level_->text().toStdString();
        options.scale = scale_->value();
        options.patchSubdivisions = patch_->value();
        options.geometryMode = geometry_->currentIndex() == 1 ? assets::JoJaGeometryMode::HybridSectors
                                                              : assets::JoJaGeometryMode::ModelScene;
        options.trianglesPerModel = chunk_->value();
        options.maxTriangles = triangles_->value();
        options.maxModels = models_->value();
        if ( options.geometryMode == assets::JoJaGeometryMode::HybridSectors )
        {
            options.maxSectors = sectors_->value();
            options.maxFragments = fragments_->value();
        }
        options.allowPlaceholders = placeholders_->isChecked();
        options.renderMeshCollision = collision_->isChecked();
        for ( int i = 0; i < shaders_->rowCount(); ++i )
        {
            const auto shader = shaders_->item(i, 0)->data(Qt::UserRole).toByteArray().toStdString();
            if ( shaders_->item(i, 3)->checkState() == Qt::Checked )
            {
                options.skipShaders.push_back(shader);
            }
            const auto file = shaders_->item(i, 1)->text();
            if ( !file.isEmpty() )
            {
                request.mappings.push_back({shader, file, shaders_->item(i, 2)->text()});
            }
        }
        guard_ = detail::makePreviewWorkerPublicationGuard(++generation_);
        busy_ = true;
        work_ = mode;
        update();
        status_->setText(mode == Work::publish ? "Rechecking inputs and staging archive…" : "Reading source map…");
        watcher_.setFuture(detail::runPreviewWorkerTask(run, mode, std::move(request),
                                                        std::shared_ptr<const Prepared>(prepared_), guard_));
    }

    void complete()
    {
        busy_ = false;
        update();
        const QPointer<JoJaImportDialog> owner(&dialog_);
        try
        {
            const auto result = watcher_.result();
            if ( closed_ || !detail::previewWorkerMayPublish(guard_, generation_) || !current() || !owner ||
                 !detail::previewWorkerMayPublish(guard_, generation_) )
            {
                result->stage.reset();
                result->prepared.reset();
                return;
            }
            if ( !result->error.isEmpty() )
            {
                invalidate();
                status_->setText(result->error);
                return;
            }
            if ( work_ == Work::inspect )
            {
                const QSignalBlocker blocker(shaders_);
                const bool same = inspectedHash_ == result->sourceHash;
                if ( !same )
                {
                    shaders_->setRowCount(static_cast<int>(result->shaders.size()));
                    for ( int i = 0; i < shaders_->rowCount(); ++i )
                    {
                        const auto& name = result->shaders.at(i);
                        auto* shader = new QTableWidgetItem(QString::fromUtf8(name));
                        shader->setData(Qt::UserRole, name);
                        shader->setFlags(shader->flags() & ~Qt::ItemIsEditable);
                        shaders_->setItem(i, 0, shader);
                        shaders_->setItem(i, 1, new QTableWidgetItem);
                        shaders_->setItem(i, 2, new QTableWidgetItem);
                        auto* skip = new QTableWidgetItem;
                        skip->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                        skip->setCheckState(Qt::Unchecked);
                        skip->setToolTip("Omit this shader's surfaces from the converted scene.");
                        shaders_->setItem(i, 3, skip);
                    }
                }
                inspectedHash_ = result->sourceHash;
                report_->setPlainText(result->report);
                status_->setText("Map or skip shaders, or enable placeholders, then prepare.");
            }
            else if ( work_ == Work::prepare )
            {
                prepared_ = std::move(result->prepared);
                report_->setPlainText(prepared_->report);
                status_->setText("Prepared. Review the archive before creating it.");
            }
            else
            {
                final_ = std::make_unique<FinalVerification>();
                final_->stage = std::move(result->stage);
                final_->destination = result->destination;
                busy_ = true;
                status_->setText("Verifying captured input bytes before publication…");
                scheduleVerification();
            }
        }
        catch ( const std::exception& error )
        {
            invalidate();
            status_->setText(QString::fromUtf8(error.what()));
        }
        catch ( ... )
        {
            invalidate();
            status_->setText("Import failed unexpectedly.");
        }
        update();
    }

    void scheduleVerification()
    {
        const auto expected = generation_;
        QTimer::singleShot(0, &dialog_,
                           [this, expected]
                           {
                               if ( !final_ || expected != generation_ )
                               {
                                   return;
                               }
                               const QPointer<JoJaImportDialog> owner(&dialog_);
                               if ( !current() || !owner || !final_ || expected != generation_ )
                               {
                                   return;
                               }
                               try
                               {
                                   if ( !final_->advance(prepared_->inputs) )
                                   {
                                       scheduleVerification();
                                       return;
                                   }
                                   // No event dispatch or external callback between final verification and create-only
                                   // publication.
                                   const auto destination = final_->destination;
                                   final_->stage->publishNew(nativePath(destination));
                                   final_.reset();
                                   busy_ = false;
                                   update();
                                   status_->setText("Created " + destination);
                                   const auto published = context_.published;
                                   if ( published )
                                   {
                                       try
                                       {
                                           published(destination);
                                       }
                                       catch ( ... )
                                       {
                                           if ( owner )
                                           {
                                               status_->setText("Created " + destination +
                                                                ". Host notification failed.");
                                           }
                                       }
                                   }
                               }
                               catch ( const std::exception& error )
                               {
                                   if ( owner )
                                   {
                                       invalidate();
                                       status_->setText(QString::fromUtf8(error.what()));
                                   }
                               }
                               catch ( ... )
                               {
                                   if ( owner )
                                   {
                                       invalidate();
                                       status_->setText("Final input verification failed.");
                                   }
                               }
                           });
    }

    JoJaImportDialog& dialog_;
    Context context_;
    QLineEdit *source_{}, *palette_{}, *level_{}, *output_{};
    QComboBox *target_{}, *audio_{}, *geometry_{};
    QDoubleSpinBox* scale_{};
    QSpinBox *patch_{}, *chunk_{}, *triangles_{}, *models_{}, *sectors_{}, *fragments_{};
    std::array<int, 2> chunkByMode_{140, 400};
    std::array<int, 2> trianglesByMode_{100000, 200000};
    QCheckBox *placeholders_{}, *collision_{};
    QTableWidget* shaders_{};
    QPlainTextEdit* report_{};
    QLabel* status_{};
    QPushButton *inspect_{}, *prepare_{}, *create_{}, *cancel_{};
    QFutureWatcher<std::shared_ptr<Result>> watcher_;
    Guard guard_;
    std::unique_ptr<FinalVerification> final_;
    std::shared_ptr<Prepared> prepared_;
    QByteArray inspectedHash_;
    std::uint64_t generation_{};
    Work work_{Work::inspect};
    bool busy_{}, closed_{};
};

JoJaImportDialog::JoJaImportDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}
JoJaImportDialog::~JoJaImportDialog() = default;
void JoJaImportDialog::done(int result)
{
    impl_->close();
    QDialog::done(result);
}
} // namespace sed::ui::resources
