#include "model_import_dialog.hpp"

#include "../../../../core/src/assets/atomic_file_write.hpp"
#include "sed/assets/mat.hpp"
#include "sed/assets/obj_import.hpp"
#include "ui/components/preview_worker_pool.hpp"
#include "ui/components/ui_decimal_format.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
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
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <span>

namespace sed::ui::resources
{
namespace
{
constexpr qint64 kMatByteLimit = 64 * 1024 * 1024;
constexpr qint64 kInputByteLimit = 128 * 1024 * 1024;
constexpr int kMaterialLimit = 256;

std::filesystem::path filePath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::u8path(value.toUtf8().toStdString());
#endif
}

std::span<const std::byte> bytesOf(const QByteArray& bytes)
{
    return { reinterpret_cast<const std::byte*>(bytes.constData()), static_cast<std::size_t>(bytes.size()) };
}

QByteArray hashOf(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QByteArray readBounded(const QString& path, const qint64 limit)
{
    QFile file(path);
    if ( !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error(("Cannot read input file: " + path).toStdString());
    }
    const QByteArray bytes = file.read(limit + 1);
    if ( file.error() != QFileDevice::NoError )
    {
        throw std::runtime_error(file.errorString().toStdString());
    }
    if ( bytes.size() > limit || !file.atEnd() )
    {
        throw std::runtime_error(QStringLiteral("Input exceeds the %1-byte limit: %2").arg(limit).arg(path).toStdString());
    }
    return bytes;
}

struct Source
{
    QString path;
    QByteArray hash;
    qint64 limit{};
};

struct Request
{
    QString path;
    assets::ObjImportOptions options;
    QStringList matPaths;
};

struct Prepared
{
    std::vector<Source> sources;
    QByteArray model;
    QByteArray image;
    QString imageSuffix;
    QString imageProblem;
    QString report;
};

struct WorkResult
{
    std::shared_ptr<Prepared> prepared;
    std::unique_ptr<assets::detail::StagedAssetFile> staged;
    QString destination;
    QString error;
};

void requireActive(const detail::PreviewWorkerPublicationGuard& guard)
{
    if ( guard.canceled->load(std::memory_order_acquire) )
    {
        throw std::runtime_error("Cancelled.");
    }
}

QByteArray capture(const QString& path, const qint64 limit, Prepared& prepared, qint64& total)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QByteArray bytes = readBounded(absolute, std::min(limit, kInputByteLimit - total));
    total += bytes.size();
    prepared.sources.push_back({ absolute, hashOf(bytes), limit });
    return bytes;
}

void verifySources(const Prepared& prepared, const detail::PreviewWorkerPublicationGuard& guard)
{
    for ( const auto& source : prepared.sources )
    {
        requireActive(guard);
        if ( hashOf(readBounded(source.path, source.limit)) != source.hash )
        {
            throw std::runtime_error(("Input changed; prepare again: " + source.path).toStdString());
        }
    }
    requireActive(guard);
}

QString canonicalPath(const QString& path)
{
    const QFileInfo info(path);
    const QString existing = info.canonicalFilePath();
    const QString result = existing.isEmpty()
        ? QDir::cleanPath(QFileInfo(info.absolutePath()).canonicalFilePath() + '/' + info.fileName()) : existing;
#ifdef _WIN32
    return result.toCaseFolded();
#else
    return result;
#endif
}

void requireNewDestination(const Prepared& prepared, const QString& destination)
{
    const QFileInfo target(destination);
    if ( target.exists() || target.isSymLink() )
    {
        throw std::runtime_error("Output already exists; choose a new filename.");
    }
    if ( !QFileInfo(target.absolutePath()).isDir() || target.fileName().isEmpty() )
    {
        throw std::runtime_error("Choose a new filename in an existing directory.");
    }
#ifdef _WIN32
    if ( target.fileName().contains(':') )
    {
        throw std::runtime_error("An output filename cannot name an alternate data stream.");
    }
#endif
    for ( const auto& source : prepared.sources )
    {
        if ( canonicalPath(source.path) == canonicalPath(destination) )
        {
            throw std::runtime_error("Output aliases a protected input file.");
        }
    }
}

void verifyModel(const assets::Model3do& model, const QByteArray& text)
{
    assets::Model3doLoadOptions options;
    options.applyHierarchyTransforms = false;
    const auto back = assets::loadFrom3DO(bytesOf(text), options);
    bool same = std::fabs(back.version - 2.1) < 1e-9 && back.materials == model.materials
        && back.meshes.size() == model.meshes.size() && back.nodes.size() == model.nodes.size();
    for ( std::size_t i = 0; same && i < model.nodes.size(); ++i )
    {
        same = back.nodes[i].parent == model.nodes[i].parent && back.nodes[i].mesh == model.nodes[i].mesh;
    }
    for ( std::size_t i = 0; same && i < model.meshes.size(); ++i )
    {
        const auto& a = model.meshes[i];
        const auto& b = back.meshes[i];
        same = a.name == b.name && a.vertices.size() == b.vertices.size()
            && a.texVertices.size() == b.texVertices.size() && a.faces.size() == b.faces.size()
            && b.vertexNormals.size() == a.vertices.size() && b.faceNormals.size() == a.faces.size();
        for ( std::size_t f = 0; same && f < a.faces.size(); ++f )
        {
            same = a.faces[f].material == b.faces[f].material && a.faces[f].vertices == b.faces[f].vertices
                && a.faces[f].texVertices == b.faces[f].texVertices;
        }
        for ( std::size_t t = 0; same && t < a.texVertices.size(); ++t )
        {
            same = a.texVertices[t].u == b.texVertices[t].u && a.texVertices[t].v == b.texVertices[t].v;
        }
    }
    if ( !same )
    {
        throw std::runtime_error("3DO readback differs from the prepared meshes, materials or texels.");
    }
}

QString reportFor(const assets::ObjImportResult& imported, const assets::ObjImportOptions& options)
{
    QString report = QStringLiteral("Format: %1 → 3DO 2.1\nScale: %2; source up axis: %3; origin: %4\n"
                                    "Normals: %5; normal split vertices: %6\n"
                                    "Meshes: %7; faces: %8; vertices: %9; texture vertices: %10\n"
                                    "Triangulated polygons: %11; untextured faces: %12; degenerate triangles: %13\n")
                         .arg(imported.format == assets::ModelSourceFormat::Glb ? "GLB" : "OBJ")
                         .arg(options.scale, 0, 'g', 17)
                         .arg(options.upAxis == assets::ObjUpAxis::Y ? "Y" : "Z")
                         .arg(options.origin == assets::ObjOrigin::Base ? "base"
                                 : options.origin == assets::ObjOrigin::Center ? "center" : "none")
                         .arg(options.normalPolicy == assets::ObjNormalPolicy::Preserve ? "preserve" : "recalculate")
                         .arg(imported.normalSplitVertices).arg(imported.meshes.size()).arg(imported.faces)
                         .arg(imported.vertices).arg(imported.textureVertices).arg(imported.triangulatedPolygons)
                         .arg(imported.untexturedFaces).arg(imported.degenerateTriangles);
    const auto vectorText = [](const TVector& value)
    {
        return QStringLiteral("(%1, %2, %3)").arg(value.x, 0, 'g', 9).arg(value.y, 0, 'g', 9).arg(value.z, 0, 'g', 9);
    };
    report += "Bounds: " + vectorText(imported.boundsMin) + " .. " + vectorText(imported.boundsMax) + '\n';
    for ( const auto& mesh : imported.meshes )
    {
        report += QStringLiteral("Mesh %1: source %2, part %3/%4; %5 vertices, %6 faces, %7 UVs\n")
                      .arg(QString::fromStdString(mesh.name), QString::fromStdString(mesh.source))
                      .arg(mesh.part).arg(mesh.parts).arg(mesh.vertices).arg(mesh.faces).arg(mesh.textureVertices);
    }
    for ( const auto& material : imported.materials )
    {
        report += QStringLiteral("Material '%1' → %2: %3 × %4 texels, %5 faces\n")
                      .arg(QString::fromStdString(material.name), QString::fromStdString(material.mat))
                      .arg(material.width).arg(material.height).arg(material.faces);
    }
    for ( const auto& name : imported.unusedMaterials )
    {
        report += "Unused mapping: " + QString::fromStdString(name) + '\n';
    }
    for ( const auto& [name, count] : imported.ignoredStatements )
    {
        report += QStringLiteral("Ignored OBJ %1: %2\n").arg(QString::fromStdString(name)).arg(count);
    }
    for ( const auto& [name, count] : imported.gltf.ignored )
    {
        report += QStringLiteral("Ignored GLB %1: %2\n").arg(QString::fromStdString(name)).arg(count);
    }
    return report;
}

std::shared_ptr<WorkResult> prepareModel(Request request, const detail::PreviewWorkerPublicationGuard guard)
{
    auto result = std::make_shared<WorkResult>();
    try
    {
        requireActive(guard);
        auto prepared = std::make_shared<Prepared>();
        qint64 total{};
        const QByteArray bytes = capture(request.path, assets::kGlbImportMaxSourceBytes, *prepared, total);
        for ( std::size_t i = 0; i < request.options.materials.size(); ++i )
        {
            requireActive(guard);
            const QString& path = request.matPaths[static_cast<qsizetype>(i)];
            if ( path.isEmpty() )
            {
                continue;
            }
            const QByteArray matBytes = capture(path, kMatByteLimit, *prepared, total);
            std::vector<std::byte> raw(static_cast<std::size_t>(matBytes.size()));
            std::memcpy(raw.data(), matBytes.constData(), raw.size());
            const auto mat = assets::MatFile::parse(std::move(raw));
            if ( mat.isColor() || mat.cels().empty() )
            {
                throw std::runtime_error("MAT has no texture dimensions; use explicit width and height instead.");
            }
            request.options.materials[i].width = mat.cels().front().width;
            request.options.materials[i].height = mat.cels().front().height;
        }
        requireActive(guard);
        const auto imported = assets::importModel(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), request.options);
        requireActive(guard);
        prepared->model = QByteArray::fromStdString(assets::saveTo3DO(imported.model));
        verifyModel(imported.model, prepared->model);
        prepared->report = reportFor(imported, request.options);
        for ( const auto& source : prepared->sources )
        {
            prepared->report += source.path + "\nSHA-256: " + QString::fromLatin1(source.hash.toHex()) + '\n';
        }
        prepared->report += "3DO SHA-256: " + QString::fromLatin1(hashOf(prepared->model).toHex()) + '\n';
        const auto& images = imported.gltf.baseColorImages;
        if ( images.size() == 1 && images[0].problem.empty() && !images[0].bytes.empty()
            && (images[0].mimeType == "image/png" || images[0].mimeType == "image/jpeg") )
        {
            prepared->image = QByteArray(reinterpret_cast<const char*>(images[0].bytes.data()),
                static_cast<qsizetype>(images[0].bytes.size()));
            prepared->imageSuffix = images[0].mimeType == "image/png" ? "png" : "jpg";
            prepared->report += "Embedded base-color image: " + QString::fromStdString(images[0].name)
                + " (" + QString::fromStdString(images[0].mimeType) + "), SHA-256: "
                + QString::fromLatin1(hashOf(prepared->image).toHex()) + '\n';
        }
        else
        {
            prepared->imageProblem = images.empty() ? "No used embedded base-color image."
                : images.size() > 1 ? "Used materials reference several base-color images; single-image export is unavailable."
                                   : "Embedded base-color image is unavailable: " + QString::fromStdString(images[0].problem);
            prepared->report += prepared->imageProblem + '\n';
        }
        verifySources(*prepared, guard);
        result->prepared = std::move(prepared);
    }
    catch ( const assets::ObjImportError& error )
    {
        result->error = QString::fromStdString(error.code()) + ": " + QString::fromUtf8(error.what());
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    return result;
}

std::shared_ptr<WorkResult> stageModel(std::shared_ptr<const Prepared> prepared, const QString destination,
    const bool image, const detail::PreviewWorkerPublicationGuard guard)
{
    auto result = std::make_shared<WorkResult>();
    try
    {
        verifySources(*prepared, guard);
        requireNewDestination(*prepared, destination);
        auto staged = std::make_unique<assets::detail::StagedAssetFile>(filePath(destination));
        staged->writeAndClose(bytesOf(image ? prepared->image : prepared->model));
        verifySources(*prepared, guard);
        result->destination = destination;
        result->staged = std::move(staged);
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    return result;
}
} // namespace

class ModelImportDialog::Impl
{
public:
    Impl(ModelImportDialog& dialog, Context context) : dialog_(dialog), context_(std::move(context))
    {
        dialog_.setWindowTitle("Import model to 3DO");
        dialog_.resize(860, 740);
        auto* outer = new QVBoxLayout(&dialog_);
        auto* scroll = new QScrollArea(&dialog_);
        scroll->setWidgetResizable(true);
        auto* body = new QWidget(scroll);
        auto* layout = new QVBoxLayout(body);
        auto* explanation = new QLabel("Convert OBJ or GLB to a new 3DO 2.1. Material mappings are explicit; "
            "external MTL and image files are not loaded. This dialog does not change the level.", body);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto* form = new QFormLayout;
        source_ = line("modelImportSource");
        form->addRow("Source OBJ / GLB", pathRow(source_, "modelImportBrowseSource", 0));
        scale_ = line("modelImportScale");
        scale_->setLocale(uiNumericLocale());
        scale_->setText(formatUiNumber(1));
        onUiNumericFormatChanged(scale_, [this]
        {
            if ( scale_->isModified() )
            {
                return;
            }
            bool valid{};
            const double value = parseUiNumber(scale_->text(), scale_->locale(), &valid);
            const QSignalBlocker blocker(scale_);
            scale_->setLocale(uiNumericLocale());
            if ( valid )
            {
                scale_->setText(formatUiNumber(value, 'g', 17));
            }
        });
        form->addRow("Positive scale (up to 1000000)", scale_);
        axis_ = combo("modelImportUpAxis", { "Y up → Z up", "Already Z up" });
        form->addRow("Source up axis", axis_);
        origin_ = combo("modelImportOrigin", { "Bottom center", "Center", "Keep transformed coordinates" });
        form->addRow("Model origin", origin_);
        normals_ = combo("modelImportNormals", { "Preserve authored normals / hard edges", "Recalculate smooth normals" });
        form->addRow("Normals", normals_);
        mesh_ = line("modelImportMeshName");
        form->addRow("Optional mesh name prefix", mesh_);
        layout->addLayout(form);
        split_ = checkBox("Split meshes to JK limits", "modelImportSplit", true);
        untextured_ = checkBox("Allow untextured faces (invisible, collision only)", "modelImportAllowUntextured", false);
        layout->addWidget(split_);
        layout->addWidget(untextured_);
        auto* mappingHelp = new QLabel("Map exact OBJ usemtl names or GLB material names (#index when unnamed). "
            "Give width/height in texels, or a MAT file whose first cel supplies both dimensions.", body);
        mappingHelp->setWordWrap(true);
        layout->addWidget(mappingHelp);
        materials_ = new QTableWidget(0, 5, body);
        materials_->setObjectName("modelImportMaterials");
        materials_->setHorizontalHeaderLabels({ "Source material", "MAT basename", "Width", "Height", "MAT file (optional)" });
        materials_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        materials_->horizontalHeader()->setStretchLastSection(true);
        materials_->setMinimumHeight(130);
        materials_->setSelectionBehavior(QAbstractItemView::SelectRows);
        layout->addWidget(materials_);
        auto* mappingActions = new QHBoxLayout;
        add_ = button("Add mapping", "modelImportAddMaterial");
        remove_ = button("Remove selected", "modelImportRemoveMaterial");
        auto* selectMat = button("Choose MAT for selected row…", "modelImportBrowseMat");
        mappingActions->addWidget(add_);
        mappingActions->addWidget(remove_);
        mappingActions->addWidget(selectMat);
        mappingActions->addStretch();
        layout->addLayout(mappingActions);
        QObject::connect(add_, &QPushButton::clicked, &dialog_, [this] { addMapping(); });
        QObject::connect(remove_, &QPushButton::clicked, &dialog_, [this]
        {
            const int row = materials_->currentRow();
            if ( row >= 0 )
            {
                materials_->removeRow(row);
                invalidate();
            }
        });
        QObject::connect(selectMat, &QPushButton::clicked, &dialog_, [this] { chooseMat(); });
        auto* outputs = new QFormLayout;
        output_ = line("modelImportOutput");
        imageOutput_ = line("modelImportImageOutput");
        outputs->addRow("New 3DO filename", pathRow(output_, "modelImportBrowseOutput", 1));
        outputs->addRow("Optional new image filename", pathRow(imageOutput_, "modelImportBrowseImage", 2));
        layout->addLayout(outputs);
        auto* imageHelp = new QLabel("The embedded base-color image is saved unchanged as a separate file. "
            "Creating the 3DO and saving the image are independent actions.", body);
        imageHelp->setWordWrap(true);
        layout->addWidget(imageHelp);
        report_ = new QPlainTextEdit(body);
        report_->setObjectName("modelImportReport");
        report_->setReadOnly(true);
        report_->setMinimumHeight(140);
        layout->addWidget(report_);
        scroll->setWidget(body);
        outer->addWidget(scroll);
        status_ = new QLabel("Choose a source and explicit material mappings, then prepare.", &dialog_);
        status_->setObjectName("modelImportStatus");
        status_->setWordWrap(true);
        outer->addWidget(status_);
        auto* actions = new QHBoxLayout;
        prepare_ = button("Prepare", "modelImportPrepare");
        create_ = button("Create new 3DO", "modelImportCreate");
        saveImage_ = button("Save base-color image", "modelImportSaveImage");
        cancel_ = button("Cancel work", "modelImportCancel");
        actions->addWidget(prepare_);
        actions->addWidget(create_);
        actions->addWidget(cancel_);
        outer->addLayout(actions);
        auto* imageActions = new QHBoxLayout;
        imageActions->addWidget(saveImage_);
        imageActions->addStretch();
        outer->addLayout(imageActions);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog_);
        outer->addWidget(close);
        QObject::connect(close, &QDialogButtonBox::rejected, &dialog_, &QDialog::reject);
        QObject::connect(prepare_, &QPushButton::clicked, &dialog_, [this] { prepare(); });
        QObject::connect(create_, &QPushButton::clicked, &dialog_, [this] { create(false); });
        QObject::connect(saveImage_, &QPushButton::clicked, &dialog_, [this] { create(true); });
        QObject::connect(cancel_, &QPushButton::clicked, &dialog_, [this]
        {
            invalidate(!publishing_);
            status_->setText("Cancelled. The bounded worker finishes without publishing.");
        });
        for ( auto* input : { source_, scale_, mesh_ } )
        {
            QObject::connect(input, &QLineEdit::textChanged, &dialog_, [this] { invalidate(); });
        }
        for ( auto* input : { axis_, origin_, normals_ } )
        {
            QObject::connect(input, &QComboBox::currentIndexChanged, &dialog_, [this] { invalidate(); });
        }
        for ( auto* input : { split_, untextured_ } )
        {
            QObject::connect(input, &QCheckBox::toggled, &dialog_, [this] { invalidate(); });
        }
        QObject::connect(materials_, &QTableWidget::cellChanged, &dialog_, [this] { invalidate(); });
        for ( auto* destination : { output_, imageOutput_ } )
        {
            QObject::connect(destination, &QLineEdit::textChanged, &dialog_, [this]
            {
                if ( publishing_ )
                {
                    invalidate(false);
                    status_->setText("Destination changed; staging cancelled. Retry with the new filename.");
                }
            });
        }
        watcher_ = new QFutureWatcher<std::shared_ptr<WorkResult>>(&dialog_);
        QObject::connect(watcher_, &QFutureWatcherBase::finished, &dialog_, [this] { finish(); });
        updateActions();
    }

    ~Impl()
    {
        detail::cancelPreviewWorkerPublication(guard_);
    }

    void close()
    {
        closed_ = true;
        invalidate();
    }

private:
    QLineEdit* line(const char* name)
    {
        auto* value = new QLineEdit(&dialog_);
        value->setObjectName(QString::fromLatin1(name));
        return value;
    }

    QPushButton* button(const char* label, const char* name)
    {
        auto* value = new QPushButton(QString::fromUtf8(label), &dialog_);
        value->setObjectName(QString::fromLatin1(name));
        return value;
    }

    QComboBox* combo(const char* name, const QStringList& labels)
    {
        auto* value = new QComboBox(&dialog_);
        value->setObjectName(QString::fromLatin1(name));
        value->addItems(labels);
        return value;
    }

    QCheckBox* checkBox(const char* label, const char* name, const bool checked)
    {
        auto* value = new QCheckBox(QString::fromUtf8(label), &dialog_);
        value->setObjectName(QString::fromLatin1(name));
        value->setChecked(checked);
        return value;
    }

    QWidget* pathRow(QLineEdit* input, const char* name, const int kind)
    {
        auto* row = new QWidget(&dialog_);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* browse = button("Browse…", name);
        layout->addWidget(input);
        layout->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, &dialog_, [this, input, kind]
        {
            const QPointer<ModelImportDialog> owner(&dialog_);
            const QPointer<QLineEdit> target(input);
            const auto generation = generation_;
            const QString before = input->text();
            const QString initial = before.isEmpty() ? context_.initialDirectory : before;
            const QString result = kind == 0
                ? QFileDialog::getOpenFileName(&dialog_, "Choose OBJ or GLB", initial, "Model sources (*.obj *.glb)")
                : QFileDialog::getSaveFileName(&dialog_, "Choose a new filename", initial,
                    kind == 1 ? "Jedi Knight model (*.3do)" : "Embedded image (*.png *.jpg *.jpeg)",
                    nullptr, QFileDialog::DontConfirmOverwrite);
            if ( owner && target && !closed_ && generation == generation_ && target->text() == before
                && !result.isEmpty() )
            {
                target->setText(result);
            }
        });
        return row;
    }

    void chooseMat()
    {
        const int row = materials_->currentRow();
        if ( row < 0 )
        {
            status_->setText("Select a material mapping first.");
            return;
        }
        const QPointer<ModelImportDialog> owner(&dialog_);
        const auto generation = generation_;
        const QString result = QFileDialog::getOpenFileName(&dialog_, "Choose MAT dimensions", context_.initialDirectory,
            "Jedi Knight materials (*.mat)");
        if ( owner && !closed_ && generation == generation_ && row < materials_->rowCount() && !result.isEmpty() )
        {
            materials_->setItem(row, 4, new QTableWidgetItem(result));
        }
    }

    void addMapping()
    {
        if ( materials_->rowCount() >= kMaterialLimit )
        {
            return;
        }
        const int row = materials_->rowCount();
        materials_->insertRow(row);
        const QStringList values{ "", "", "64", "64", "" };
        for ( int column = 0; column < values.size(); ++column )
        {
            materials_->setItem(row, column, new QTableWidgetItem(values[column]));
        }
        materials_->setCurrentCell(row, 0);
        invalidate();
    }

    bool current()
    {
        // Host callbacks may run an event loop, change inputs or destroy the dialog.
        const QPointer<ModelImportDialog> owner(&dialog_);
        const auto generation = generation_;
        const auto callback = context_.isCurrent;
        checkingContext_ = true;
        const auto resetChecking = qScopeGuard([&]
        {
            if ( owner )
            {
                checkingContext_ = false;
            }
        });
        bool valid = true;
        QString problem;
        try
        {
            valid = !callback || callback();
        }
        catch ( const std::exception& error )
        {
            valid = false;
            problem = QString::fromUtf8(error.what());
        }
        catch ( ... )
        {
            valid = false;
            problem = "The host context check failed with an unexpected exception; reopen this dialog.";
        }
        if ( !owner )
        {
            return false;
        }
        if ( closed_ || generation != generation_ )
        {
            return false;
        }
        if ( !valid )
        {
            invalidate();
            status_->setText(problem.isEmpty() ? "The host context changed; reopen this dialog." : problem);
        }
        return valid;
    }

    void updateActions()
    {
        prepare_->setEnabled(!busy_ && !closed_);
        create_->setEnabled(!busy_ && !closed_ && prepared_ != nullptr);
        saveImage_->setEnabled(!busy_ && !closed_ && prepared_ && !prepared_->image.isEmpty());
        saveImage_->setToolTip(prepared_ ? prepared_->imageProblem : QString());
        cancel_->setEnabled(busy_);
        add_->setEnabled(materials_->rowCount() < kMaterialLimit);
        remove_->setEnabled(materials_->rowCount() > 0);
    }

    void invalidate(const bool discard = true)
    {
        ++generation_;
        detail::cancelPreviewWorkerPublication(guard_);
        if ( discard )
        {
            prepared_.reset();
            report_->clear();
            status_->setText("Inputs changed; prepare again.");
        }
        updateActions();
    }

    void prepare()
    {
        if ( busy_ || closed_ || checkingContext_ )
        {
            return;
        }
        invalidate();
        if ( !current() )
        {
            return;
        }
        Request request;
        request.path = source_->text();
        bool numeric{};
        request.options.scale = parseUiNumber(scale_->text(), scale_->locale(), &numeric);
        if ( !numeric || !std::isfinite(request.options.scale) || request.options.scale <= 0
            || request.options.scale > 1e6 )
        {
            status_->setText("Scale must be a finite number greater than zero and at most 1000000.");
            return;
        }
        {
            const QSignalBlocker blocker(scale_);
            scale_->setLocale(uiNumericLocale());
            scale_->setText(formatUiNumber(request.options.scale, 'g', 17));
            scale_->setModified(false);
        }
        request.options.upAxis = axis_->currentIndex() == 0 ? assets::ObjUpAxis::Y : assets::ObjUpAxis::Z;
        request.options.origin = static_cast<assets::ObjOrigin>(origin_->currentIndex());
        request.options.normalPolicy = static_cast<assets::ObjNormalPolicy>(normals_->currentIndex());
        request.options.meshNamePrefix = mesh_->text().toStdString();
        request.options.split = split_->isChecked();
        request.options.allowUntextured = untextured_->isChecked();
        for ( int row = 0; row < materials_->rowCount(); ++row )
        {
            const auto cell = [&](const int column)
            {
                const auto* item = materials_->item(row, column);
                return item ? item->text() : QString();
            };
            assets::ObjImportMaterial material;
            material.name = cell(0).toStdString();
            material.mat = cell(1).toStdString();
            const QString matPath = cell(4);
            if ( matPath.isEmpty() )
            {
                bool widthValid{}, heightValid{};
                material.width = cell(2).toInt(&widthValid);
                material.height = cell(3).toInt(&heightValid);
                if ( !widthValid || !heightValid || material.width < 1 || material.width > 4096
                    || material.height < 1 || material.height > 4096 )
                {
                    status_->setText("Explicit MAT dimensions must be integers from 1 to 4096 texels.");
                    return;
                }
            }
            request.options.materials.push_back(std::move(material));
            request.matPaths.push_back(matPath);
        }
        guard_ = detail::makePreviewWorkerPublicationGuard(generation_);
        busy_ = true;
        publishing_ = false;
        updateActions();
        status_->setText("Capturing inputs and preparing the model…");
        watcher_->setFuture(detail::runPreviewWorkerTask(prepareModel, std::move(request), guard_));
    }

    void create(const bool image)
    {
        if ( busy_ || closed_ || checkingContext_ || !prepared_ || (image && prepared_->image.isEmpty()) || !current() )
        {
            return;
        }
        const QString entered = (image ? imageOutput_ : output_)->text();
        const QString extension = QFileInfo(entered).suffix().toLower();
        const bool validExtension = image
            ? extension == prepared_->imageSuffix || (prepared_->imageSuffix == "jpg" && extension == "jpeg")
            : extension == "3do";
        if ( entered.isEmpty() || !validExtension )
        {
            status_->setText(image ? "Choose a new image filename matching its captured PNG/JPEG format."
                                  : "Choose a new filename ending in .3do.");
            return;
        }
        const QString destination = QFileInfo(entered).absoluteFilePath();
        try
        {
            requireNewDestination(*prepared_, destination);
        }
        catch ( const std::exception& error )
        {
            status_->setText(QString::fromUtf8(error.what()));
            return;
        }
        guard_ = detail::makePreviewWorkerPublicationGuard(generation_);
        busy_ = true;
        publishing_ = true;
        updateActions();
        status_->setText("Rechecking captured inputs and staging the new file…");
        watcher_->setFuture(detail::runPreviewWorkerTask(stageModel,
            std::shared_ptr<const Prepared>(prepared_), destination, image, guard_));
    }

    void finish()
    {
        busy_ = false;
        publishing_ = false;
        const QPointer<ModelImportDialog> owner(&dialog_);
        try
        {
            const auto result = watcher_->result();
            const auto cleanup = qScopeGuard([&]
            {
                result->staged.reset();
                result->prepared.reset();
            });
            if ( closed_ || !detail::previewWorkerMayPublish(guard_, generation_) )
            {
                updateActions();
                return;
            }
            if ( !current() )
            {
                if ( owner )
                {
                    updateActions();
                }
                return;
            }
            if ( !result->error.isEmpty() )
            {
                invalidate();
                status_->setText(result->error);
            }
            else if ( result->staged )
            {
                requireNewDestination(*prepared_, result->destination);
                result->staged->publishNew(filePath(result->destination));
                result->staged.reset();
                status_->setText("Created " + result->destination + ". Other outputs and level assignments are unchanged.");
                updateActions();
                const auto callback = context_.published;
                if ( callback )
                {
                    try
                    {
                        callback(result->destination);
                    }
                    catch ( const std::exception& error )
                    {
                        if ( owner )
                        {
                            status_->setText("Created " + result->destination + ". Notification failed: "
                                + QString::fromUtf8(error.what()));
                        }
                    }
                    catch ( ... )
                    {
                        if ( owner )
                        {
                            status_->setText("Created " + result->destination + ". Notification failed with an unexpected exception.");
                        }
                    }
                }
            }
            else
            {
                prepared_ = result->prepared;
                report_->setPlainText(prepared_->report);
                status_->setText("Ready. Review the report, then explicitly create a new 3DO or save its embedded image.");
            }
        }
        catch ( const std::exception& error )
        {
            if ( !owner )
            {
                return;
            }
            status_->setText(QString::fromUtf8(error.what()));
        }
        catch ( ... )
        {
            if ( !owner )
            {
                return;
            }
            status_->setText("Model import failed with an unexpected worker or host exception.");
        }
        if ( owner )
        {
            updateActions();
        }
    }

    ModelImportDialog& dialog_;
    Context context_;
    std::uint64_t generation_{};
    bool busy_{};
    bool publishing_{};
    bool closed_{};
    bool checkingContext_{};
    detail::PreviewWorkerPublicationGuard guard_;
    std::shared_ptr<Prepared> prepared_;
    QFutureWatcher<std::shared_ptr<WorkResult>>* watcher_{};
    QLineEdit *source_{}, *scale_{}, *mesh_{}, *output_{}, *imageOutput_{};
    QComboBox *axis_{}, *origin_{}, *normals_{};
    QCheckBox *split_{}, *untextured_{};
    QTableWidget* materials_{};
    QPlainTextEdit* report_{};
    QLabel* status_{};
    QPushButton *prepare_{}, *create_{}, *saveImage_{}, *cancel_{}, *add_{}, *remove_{};
};

ModelImportDialog::ModelImportDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}

ModelImportDialog::~ModelImportDialog() = default;

void ModelImportDialog::done(const int result)
{
    impl_->close();
    QDialog::done(result);
}
} // namespace sed::ui::resources
