#include "ui/resources/model_lighting_dialog.hpp"

#include "gl_view.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/model_lighting.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/assets/tpl.hpp"
#include "ui/components/dialog_window_policy.hpp"
#include "ui/components/preview_worker_pool.hpp"

// The app and agent adapters share the private asset publication protocol.
// Keep filesystem orchestration here; the model-lighting core stays Qt-free.
#include "../../../../core/src/assets/atomic_file_write.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sed::ui::resources
{
namespace
{

std::span<const std::byte> byteSpan(const QByteArray& bytes)
{
    return {reinterpret_cast<const std::byte*>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

QString hashText(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

struct Source
{
    QString name;
    bool resource{};
    std::shared_ptr<const assets::ResourceLocator> resources;
};

QByteArray readSource(const Source& source, const std::size_t maximum)
{
    if ( source.name.isEmpty() || !source.name.endsWith(QStringLiteral(".3do"), Qt::CaseInsensitive) )
    {
        throw std::runtime_error("Choose a source ending in .3do.");
    }
    if ( source.resource )
    {
        if ( !source.resources )
        {
            throw std::runtime_error("No project resource context is available.");
        }
        const auto bytes = source.resources->tryReadBinary(source.name.toStdString(), maximum);
        if ( !bytes )
        {
            throw std::runtime_error("The selected resource is missing or exceeds the source-byte limit.");
        }
        return QByteArray(reinterpret_cast<const char*>(bytes->data()), static_cast<qsizetype>(bytes->size()));
    }
    QFile file(source.name);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error(file.errorString().toStdString());
    }
    const QByteArray bytes = file.read(static_cast<qint64>(maximum) + 1);
    if ( file.error() != QFile::NoError || !file.atEnd() )
    {
        throw std::runtime_error("The complete model could not be read within the source-byte limit.");
    }
    if ( static_cast<std::size_t>(bytes.size()) > maximum )
    {
        throw std::runtime_error("The model exceeds the source-byte limit.");
    }
    return bytes;
}

QString colorText(const ColorF& color)
{
    return QStringLiteral("(%1, %2, %3)").arg(color.r, 0, 'g', 8)
        .arg(color.g, 0, 'g', 8).arg(color.b, 0, 'g', 8);
}

QString inspectionText(const assets::ModelLightingInspection& inspection)
{
    QString text = QStringLiteral("3DO %1; %2 LODs; %3 source meshes; %4 render meshes; %5 vertices\n"
        "%6 orphan meshes (preserved); %7 shared meshes\n")
        .arg(inspection.version).arg(inspection.geosets).arg(inspection.sourceMeshes)
        .arg(inspection.renderMeshes).arg(inspection.vertices).arg(inspection.orphanMeshes).arg(inspection.sharedMeshes);
    for ( const auto& mesh : inspection.meshes )
    {
        text += QStringLiteral("\nLOD %1, mesh %2: %3 — %4 vertices, %5 faces, %6 instances\n"
            "  Vertex light: %7 to %8; face extra: %9 to %10\n"
            "  Normals: %11 missing, %12 zero, %13 non-unit; %14 saturated vertices\n  Effective face modes:")
            .arg(mesh.geoset).arg(mesh.sourceMesh).arg(QString::fromStdString(mesh.name))
            .arg(mesh.vertices).arg(mesh.faces).arg(mesh.renderInstances)
            .arg(colorText(mesh.minimumVertexLight)).arg(colorText(mesh.maximumVertexLight))
            .arg(colorText(mesh.minimumFaceExtra)).arg(colorText(mesh.maximumFaceExtra))
            .arg(mesh.missingNormals).arg(mesh.zeroNormals).arg(mesh.nonUnitNormals).arg(mesh.saturatedVertices);
        for ( const auto& mode : mesh.effectiveLightModes )
        {
            text += QStringLiteral(" %1 (%2 faces)").arg(mode.mode).arg(mode.faces);
        }
        text += QLatin1Char('\n');
    }
    return text;
}

QDoubleSpinBox* number(QWidget* parent, const double value, const double minimum,
    const double maximum, const int decimals = 9)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(decimals);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

/** Both views own separate renderer caches and identical, fixed lighting.
 * They share only immutable parsed models and a single camera-control set. */
class ComparisonDialog final : public QDialog
{
public:
    ComparisonDialog(const ModelLightingDialog::Context& context,
        std::array<std::shared_ptr<const assets::Model3do>, 2> models,
        const int lod, QWidget* parent)
        : QDialog(parent)
        , models_(std::move(models))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr("Model lighting comparison — LOD %1").arg(lod));
        applyDialogWindowPolicy(*this);
        resize(1000, 600);
        auto* layout = new QVBoxLayout(this);
        auto* note = new QLabel(tr("Neutral-material stored-light comparison: zero sector ambient, no dynamic lights. "
            "Original face modes and extra light are preserved. Fully Lit / Diffuse faces can hide vertex changes. "
            "This is not runtime acceptance."), this);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* viewsLayout = new QHBoxLayout;
        layout->addLayout(viewsLayout, 1);

        QVector3D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D maximum = -minimum;
        bool hasVertices = false;
        for ( const auto& model : models_ )
        {
            for ( const auto& mesh : model->meshes )
            {
                for ( const auto& vertex : mesh.vertices )
                {
                    const QVector3D point(static_cast<float>(vertex.x), static_cast<float>(vertex.y), static_cast<float>(vertex.z));
                    for ( int axis = 0; axis < 3; ++axis )
                    {
                        minimum[axis] = std::min(minimum[axis], point[axis]);
                        maximum[axis] = std::max(maximum[axis], point[axis]);
                    }
                    hasVertices = true;
                }
            }
        }
        if ( !hasVertices || (maximum - minimum).lengthSquared() <= 1e-6f )
        {
            minimum = {-0.5f, -0.5f, -0.5f};
            maximum = {0.5f, 0.5f, 0.5f};
        }
        minimum_ = minimum;
        maximum_ = maximum;
        ThingBox box;
        box.x1 = minimum.x(); box.y1 = minimum.y(); box.z1 = minimum.z();
        box.x2 = maximum.x(); box.y2 = maximum.y(); box.z2 = maximum.z();
        for ( std::size_t side = 0; side < views_.size(); ++side )
        {
            auto* column = new QVBoxLayout;
            viewsLayout->addLayout(column, 1);
            column->addWidget(new QLabel(side == 0 ? tr("Original") : tr("Proposed"), this));
            auto* view = new GlView(this);
            views_[side] = view;
            view->setMinimumSize(320, 260);
            column->addWidget(view, 1);
            view->setMountedArchiveWatchingEnabled(false);
            view->setResourceLocator(&resources_, false);
            view->setRenderPipeline(GlView::RenderPipeline::GamePreview);
            view->setShowGrid(false);
            view->setShowAxisWidget(false);
            view->setShowToastOverlays(false);
            view->setShowWireframe(false);
            view->setShowVertices(false);
            view->setShowSolid(true);
            view->setShowTextures(true);
            view->setShowFog(false);
            view->setShowThings(true);
            view->setShowEditorCubes(false);
            view->setShowPreviewCubes(false);
            view->setShowThingGizmos(false);
            view->setThingViewMode(GlView::ThingViewMode::Shaded);
            view->setLightingMode(GlView::LightingMode::Gouraud);
            view->setThingModelLodEnabled(false);
            view->setThingLightRenderingEnabled(false);
            view->setSectorPointLightRenderingEnabled(false);
            view->setCameraProjectionType(app::ViewportCamera::ProjectionType::Orthographic);
            view->setViewportPickingEnabled(false);
            view->setHoverPickEnabled(false);
            view->setPreviewInteractionEnabled(false);
            view->setTemplateDatabase(&templates_[side]);
            const std::string modelName = "__model_lighting_comparison.3do";
            view->primeModel3do(modelName, models_[side]);
            QImage white(1, 1, QImage::Format_RGBA8888);
            white.fill(Qt::white);
            for ( const auto& material : models_[side]->materials )
            {
                view->primeMaterialTexture(material, context.colormap, white, 0);
            }
            ThingTemplate entry;
            entry.name = "__model_lighting_comparison";
            entry.bbox = box;
            TplValue modelValue;
            modelValue.name = "model3d";
            modelValue.type = ValueType::str;
            modelValue.adjType = AdjType::model3do;
            modelValue.stringValue = modelName;
            entry.values.push_back(std::move(modelValue));
            templates_[side].addOrUpdate(entry, true);
            auto& level = levels_[side];
            level.kind = context.project;
            level.masterCMP = context.colormap;
            level.ppunit = 320.0;
            level.templates.push_back(entry);
            level.sectors.emplace_back();
            level.sectors.back().ambient = {0, 0, 0, 1};
            Thing thing;
            thing.name = entry.name;
            thing.templateName = entry.name;
            thing.sector = 0;
            thing.bbox = box;
            level.things.push_back(thing);
            view->setLevel(&level, GlView::SetLevelMode::resetView);
        }
        auto* pose = new QHBoxLayout;
        layout->addLayout(pose);
        const std::array<QString, 3> labels{tr("Yaw"), tr("Pitch"), tr("Roll")};
        const std::array<double, 3> defaults{45.0, 25.0, 0.0};
        for ( std::size_t index = 0; index < angles_.size(); ++index )
        {
            pose->addWidget(new QLabel(labels[index], this));
            angles_[index] = number(this, defaults[index], -360.0, 360.0, 3);
            pose->addWidget(angles_[index]);
            connect(angles_[index], &QDoubleSpinBox::valueChanged, this, [this]() { updatePose(); });
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        updatePose();
    }

    ~ComparisonDialog() override
    {
        // GlViews retain non-owning Level/Template pointers, so destroy them
        // before these value members (QObject's child cleanup runs later).
        for ( auto* view : views_ )
        {
            delete view;
        }
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QDialog::showEvent(event);
        updatePose();
    }

private:
    void updatePose()
    {
        for ( auto* view : views_ )
        {
            view->setOrbitViewImmediate(static_cast<float>(angles_[0]->value()),
                static_cast<float>(angles_[1]->value()), static_cast<float>(angles_[2]->value()));
            view->frameWorldBounds(minimum_, maximum_, false);
        }
    }

    std::array<std::shared_ptr<const assets::Model3do>, 2> models_;
    std::array<Level, 2> levels_;
    std::array<assets::Templates, 2> templates_;
    assets::ResourceLocator resources_;
    std::array<GlView*, 2> views_{};
    std::array<QDoubleSpinBox*, 3> angles_{};
    QVector3D minimum_;
    QVector3D maximum_;
};

struct JobResult
{
    QByteArray source;
    std::optional<assets::ModelLightingInspection> inspection;
    std::optional<assets::ModelLightingBakeResult> bake;
    std::array<std::shared_ptr<const assets::Model3do>, 2> preview;
    QString error;
};

struct LimitControl
{
    const char* name;
    std::size_t assets::ModelLightingLimits::* member;
    QSpinBox* spin{};
};

} // namespace

struct ModelLightingDialog::Impl
{
    ModelLightingDialog& dialog;
    Context context;
    QComboBox* sourceKind{};
    QLineEdit* sourceEdit{};
    QLineEdit* outputEdit{};
    QComboBox* profile{};
    QComboBox* blend{};
    std::array<QDoubleSpinBox*, 3> ambient{};
    QTableWidget* lights{};
    QCheckBox* occlusion{};
    QDoubleSpinBox* bias{};
    QPlainTextEdit* diagnostics{};
    QLabel* status{};
    QPushButton* load{};
    QPushButton* calculate{};
    QPushButton* save{};
    QPushButton* compare{};
    QPushButton* cancel{};
    QSpinBox* lod{};
    QFutureWatcher<JobResult>* watcher{};
    QPointer<ComparisonDialog> comparison;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::uint64_t generation{};
    bool busy{};
    Source capturedSource;
    QByteArray sourceBytes;
    std::optional<assets::ModelLightingBakeResult> candidate;
    std::vector<LimitControl> limits;

    explicit Impl(ModelLightingDialog& owner, Context value)
        : dialog(owner), context(std::move(value))
    {
        dialog.setObjectName(QStringLiteral("modelLightingDialog"));
        dialog.setWindowTitle(QObject::tr("Model Lighting"));
        applyDialogWindowPolicy(dialog);
        dialog.resize(980, 720);
        auto* root = new QVBoxLayout(&dialog);
        auto* sourceRow = new QHBoxLayout;
        sourceKind = new QComboBox(&dialog);
        sourceKind->addItems({QObject::tr("Loose model file"), QObject::tr("Project resource name")});
        sourceRow->addWidget(sourceKind);
        sourceEdit = new QLineEdit(&dialog);
        sourceEdit->setObjectName(QStringLiteral("modelLightingSource"));
        sourceEdit->setPlaceholderText(QObject::tr("Original .3do path or project resource name"));
        sourceRow->addWidget(sourceEdit, 1);
        auto* browse = new QPushButton(QObject::tr("Browse…"), &dialog);
        sourceRow->addWidget(browse);
        load = new QPushButton(QObject::tr("Load and inspect"), &dialog);
        load->setObjectName(QStringLiteral("modelLightingLoad"));
        sourceRow->addWidget(load);
        root->addLayout(sourceRow);
        auto* tabs = new QTabWidget(&dialog);
        root->addWidget(tabs, 1);
        auto* options = new QWidget(tabs);
        auto* optionsLayout = new QVBoxLayout(options);
        auto* form = new QFormLayout;
        profile = new QComboBox(options);
        profile->setObjectName(QStringLiteral("modelLightingProfile"));
        profile->addItems({QObject::tr("JKDF2 — 2.1 scalar"), QObject::tr("MotS — 2.1 scalar"), QObject::tr("IJIM — 2.3 RGB")});
        profile->setCurrentIndex(context.project == ProjectType::IJIM ? 2 : context.project == ProjectType::MOTS ? 1 : 0);
        form->addRow(QObject::tr("Existing model profile (no version conversion)"), profile);
        blend = new QComboBox(options);
        blend->setObjectName(QStringLiteral("modelLightingBlend"));
        blend->addItems({QObject::tr("Replace stored light"), QObject::tr("Add to stored light")});
        form->addRow(QObject::tr("Blend"), blend);
        auto* ambientRow = new QHBoxLayout;
        const std::array<const char*, 3> channelNames{"Red", "Green", "Blue"};
        for ( std::size_t index = 0; index < ambient.size(); ++index )
        {
            ambient[index] = number(options, 0.0, 0.0, 1.0);
            ambient[index]->setObjectName(QStringLiteral("modelLightingAmbient") + QString::fromLatin1(channelNames[index]));
            ambientRow->addWidget(new QLabel(QString::fromLatin1(channelNames[index]), options));
            ambientRow->addWidget(ambient[index]);
            QObject::connect(ambient[index], &QDoubleSpinBox::valueChanged, &dialog, [this, index]()
            {
                if ( index == 0 && profile->currentIndex() != 2 )
                {
                    linkAmbient();
                }
                invalidate(false);
            });
        }
        form->addRow(QObject::tr("Stored ambient baseline"), ambientRow);
        occlusion = new QCheckBox(QObject::tr("Self-occlusion: same-LOD opaque faces, no texture cutouts"), options);
        occlusion->setObjectName(QStringLiteral("modelLightingOcclusion"));
        form->addRow(occlusion);
        bias = number(options, 1e-6, 1e-12, 1000.0, 12);
        bias->setObjectName(QStringLiteral("modelLightingShadowBias"));
        form->addRow(QObject::tr("Shadow endpoint bias (model units)"), bias);
        optionsLayout->addLayout(form);
        optionsLayout->addWidget(new QLabel(QObject::tr("Ordered lights in model coordinates. Directional XYZ points from the sample toward the light."), options));
        lights = new QTableWidget(0, 9, options);
        lights->setObjectName(QStringLiteral("modelLightingLights"));
        lights->setHorizontalHeaderLabels({QObject::tr("Kind"), QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
            QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B"), QObject::tr("Intensity"), QObject::tr("Point range")});
        lights->setSelectionBehavior(QAbstractItemView::SelectRows);
        lights->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        optionsLayout->addWidget(lights, 1);
        auto* lightButtons = new QHBoxLayout;
        const auto addButton = [&](const QString& label, auto action)
        {
            auto* button = new QPushButton(label, options);
            lightButtons->addWidget(button);
            QObject::connect(button, &QPushButton::clicked, &dialog, action);
        };
        addButton(QObject::tr("Add point"), [this]() { addLight(false); });
        addButton(QObject::tr("Add directional"), [this]() { addLight(true); });
        addButton(QObject::tr("Remove selected"), [this]()
        {
            if ( lights->currentRow() >= 0 )
            {
                lights->removeRow(lights->currentRow());
                invalidate(false);
            }
        });
        addButton(QObject::tr("Move up"), [this]() { moveLight(-1); });
        addButton(QObject::tr("Move down"), [this]() { moveLight(1); });
        optionsLayout->addLayout(lightButtons);
        tabs->addTab(options, QObject::tr("Lighting recipe"));
        diagnostics = new QPlainTextEdit(tabs);
        diagnostics->setReadOnly(true);
        diagnostics->setObjectName(QStringLiteral("modelLightingDiagnostics"));
        tabs->addTab(diagnostics, QObject::tr("All-LOD inspection / result"));
        auto* limitsPage = new QWidget(tabs);
        auto* limitForm = new QFormLayout(limitsPage);
        limits = {{"Source/output bytes", &assets::ModelLightingLimits::sourceBytes},
            {"Source lines", &assets::ModelLightingLimits::lines},
            {"Tokens per line", &assets::ModelLightingLimits::tokensPerLine},
            {"LODs / geosets", &assets::ModelLightingLimits::geosets},
            {"Meshes", &assets::ModelLightingLimits::meshes},
            {"Vertices", &assets::ModelLightingLimits::vertices},
            {"Triangles", &assets::ModelLightingLimits::triangles},
            {"Hierarchy nodes", &assets::ModelLightingLimits::hierarchyNodes},
            {"Lights", &assets::ModelLightingLimits::lights},
            {"Work units", &assets::ModelLightingLimits::workUnits}};
        const assets::ModelLightingLimits defaults;
        for ( auto& limit : limits )
        {
            limit.spin = new QSpinBox(limitsPage);
            limit.spin->setRange(1, static_cast<int>(defaults.*(limit.member)));
            limit.spin->setValue(limit.spin->maximum());
            limitForm->addRow(QString::fromLatin1(limit.name), limit.spin);
            QObject::connect(limit.spin, &QSpinBox::valueChanged, &dialog, [this]() { invalidate(false); });
        }
        limitForm->addRow(new QLabel(QObject::tr("Operational budgets may be lowered. They are not game-format maxima."), limitsPage));
        tabs->addTab(limitsPage, QObject::tr("Operational limits"));
        auto* actions = new QHBoxLayout;
        calculate = new QPushButton(QObject::tr("Calculate proposed lighting"), &dialog);
        calculate->setObjectName(QStringLiteral("modelLightingCalculate"));
        cancel = new QPushButton(QObject::tr("Cancel calculation"), &dialog);
        lod = new QSpinBox(&dialog);
        lod->setRange(0, 0);
        lod->setObjectName(QStringLiteral("modelLightingPreviewLod"));
        compare = new QPushButton(QObject::tr("Compare original / proposed"), &dialog);
        actions->addWidget(calculate);
        actions->addWidget(cancel);
        actions->addWidget(new QLabel(QObject::tr("Preview LOD"), &dialog));
        actions->addWidget(lod);
        actions->addWidget(compare);
        root->addLayout(actions);
        auto* outputRow = new QHBoxLayout;
        outputEdit = new QLineEdit(&dialog);
        outputEdit->setObjectName(QStringLiteral("modelLightingOutput"));
        outputEdit->setPlaceholderText(QObject::tr("New variant .3do destination — existing files are refused"));
        outputRow->addWidget(outputEdit, 1);
        auto* outputBrowse = new QPushButton(QObject::tr("Choose destination…"), &dialog);
        outputRow->addWidget(outputBrowse);
        save = new QPushButton(QObject::tr("Save new variant"), &dialog);
        save->setObjectName(QStringLiteral("modelLightingSave"));
        outputRow->addWidget(save);
        root->addLayout(outputRow);
        status = new QLabel(QObject::tr("Load a model to inspect its source. Saving creates an independent asset; bindings are unchanged."), &dialog);
        status->setWordWrap(true);
        status->setObjectName(QStringLiteral("modelLightingStatus"));
        root->addWidget(status);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        root->addWidget(buttons);
        QObject::connect(browse, &QPushButton::clicked, &dialog, [this]()
        {
            const auto path = QFileDialog::getOpenFileName(&dialog, QObject::tr("Select original model"), sourceEdit->text(), QObject::tr("Sith model (*.3do)"));
            if ( !path.isEmpty() )
            {
                sourceKind->setCurrentIndex(0);
                sourceEdit->setText(path);
            }
        });
        QObject::connect(outputBrowse, &QPushButton::clicked, &dialog, [this]()
        {
            const auto path = QFileDialog::getSaveFileName(&dialog, QObject::tr("Choose a new model variant"), outputEdit->text(),
                QObject::tr("Sith model (*.3do)"), nullptr, QFileDialog::DontConfirmOverwrite);
            if ( !path.isEmpty() )
            {
                outputEdit->setText(path);
            }
        });
        QObject::connect(sourceEdit, &QLineEdit::textChanged, &dialog, [this]() { invalidate(true); });
        QObject::connect(sourceKind, &QComboBox::currentIndexChanged, &dialog, [this]() { invalidate(true); });
        QObject::connect(profile, &QComboBox::currentIndexChanged, &dialog, [this]() { updateProfile(); invalidate(false); });
        QObject::connect(blend, &QComboBox::currentIndexChanged, &dialog, [this]() { invalidate(false); });
        QObject::connect(occlusion, &QCheckBox::toggled, &dialog, [this]() { invalidate(false); });
        QObject::connect(bias, &QDoubleSpinBox::valueChanged, &dialog, [this]() { invalidate(false); });
        QObject::connect(load, &QPushButton::clicked, &dialog, [this]()
        {
            guardOperation([this]()
            {
                loadSource();
            });
        });
        QObject::connect(calculate, &QPushButton::clicked, &dialog, [this]()
        {
            guardOperation([this]()
            {
                bake();
            });
        });
        QObject::connect(save, &QPushButton::clicked, &dialog, [this]()
        {
            guardOperation([this]()
            {
                publish();
            });
        });
        QObject::connect(compare, &QPushButton::clicked, &dialog, [this]()
        {
            guardOperation([this]()
            {
                showComparison();
            });
        });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this]() { invalidate(false); });
        QObject::connect(&dialog, &QDialog::finished, &dialog, [this]() { invalidate(false); });
        updateProfile();
        updateButtons();
    }

    ~Impl()
    {
        if ( cancellation )
        {
            cancellation->store(true);
        }
        if ( watcher )
        {
            QObject::disconnect(watcher, nullptr, &dialog, nullptr);
        }
        if ( comparison )
        {
            delete comparison.data();
        }
    }

    void updateButtons()
    {
        load->setEnabled(!busy);
        calculate->setEnabled(!busy && !sourceBytes.isEmpty());
        save->setEnabled(!busy && candidate.has_value());
        compare->setEnabled(!busy && candidate.has_value());
        cancel->setEnabled(busy);
    }

    void invalidate(const bool sourceChanged)
    {
        ++generation;
        if ( cancellation )
        {
            cancellation->store(true);
        }
        candidate.reset();
        if ( comparison )
        {
            comparison->close();
        }
        if ( sourceChanged )
        {
            sourceBytes.clear();
            diagnostics->clear();
        }
        status->setText(busy ? QObject::tr("Cancelling the old request; its result will be discarded.")
            : QObject::tr("Inputs changed. Calculate again before saving a variant."));
        updateButtons();
    }

    bool current() const
    {
        return !context.isCurrent || context.isCurrent();
    }

    void operationFailed(const QString& message)
    {
        watcher = nullptr;
        busy = false;
        invalidate(true);
        status->setText(QObject::tr("Model lighting failed: %1\nReload the source or reopen this dialog after restoring the document/resource context.")
            .arg(message));
    }

    template<class Operation>
    void guardOperation(Operation&& operation)
    {
        try
        {
            std::forward<Operation>(operation)();
        }
        catch ( const std::exception& error )
        {
            operationFailed(QString::fromUtf8(error.what()));
        }
        catch ( ... )
        {
            operationFailed(QObject::tr("An unexpected non-standard exception prevented completion."));
        }
    }

    void linkAmbient()
    {
        for ( std::size_t channel = 1; channel < ambient.size(); ++channel )
        {
            const QSignalBlocker blocker(ambient[channel]);
            ambient[channel]->setValue(ambient[0]->value());
        }
    }

    void updateProfile()
    {
        const bool scalar = profile->currentIndex() != 2;
        ambient[1]->setReadOnly(scalar);
        ambient[2]->setReadOnly(scalar);
        if ( scalar )
        {
            linkAmbient();
        }
        for ( int row = 0; row < lights->rowCount(); ++row )
        {
            linkLightColor(row);
        }
    }

    void linkLightColor(const int row)
    {
        const bool scalar = profile->currentIndex() != 2;
        const auto* red = qobject_cast<QDoubleSpinBox*>(lights->cellWidget(row, 4));
        for ( int column : {5, 6} )
        {
            auto* channel = qobject_cast<QDoubleSpinBox*>(lights->cellWidget(row, column));
            channel->setReadOnly(scalar);
            if ( scalar )
            {
                const QSignalBlocker blocker(channel);
                channel->setValue(red->value());
            }
        }
    }

    assets::ModelLightingLight lightAt(const int row) const
    {
        assets::ModelLightingLight light;
        light.kind = qobject_cast<QComboBox*>(lights->cellWidget(row, 0))->currentIndex() == 0
            ? assets::ModelLightKind::Point : assets::ModelLightKind::Directional;
        const auto value = [this, row](const int column)
        {
            return qobject_cast<QDoubleSpinBox*>(lights->cellWidget(row, column))->value();
        };
        light.position = light.directionToLight = {value(1), value(2), value(3)};
        light.color = {static_cast<float>(value(4)), static_cast<float>(value(5)), static_cast<float>(value(6)), 1.0f};
        light.intensity = value(7);
        light.range = value(8);
        return light;
    }

    void appendLight(const assets::ModelLightingLight& light)
    {
        const int row = lights->rowCount();
        lights->insertRow(row);
        auto* kind = new QComboBox(lights);
        kind->addItems({QObject::tr("Point position"), QObject::tr("Direction to light")});
        kind->setCurrentIndex(light.kind == assets::ModelLightKind::Point ? 0 : 1);
        lights->setCellWidget(row, 0, kind);
        const auto position = light.kind == assets::ModelLightKind::Point ? light.position : light.directionToLight;
        const std::array<double, 8> values{position.x, position.y, position.z,
            light.color.r, light.color.g, light.color.b, light.intensity, light.range};
        for ( int column = 1; column <= 8; ++column )
        {
            const bool color = column >= 4 && column <= 6;
            auto* spin = number(lights, values[static_cast<std::size_t>(column - 1)],
                column <= 3 ? -1e6 : column == 8 ? 1e-12 : 0.0,
                color ? 1.0 : 1e6, column == 8 ? 12 : 9);
            lights->setCellWidget(row, column, spin);
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, [this, spin, column]()
            {
                if ( column == 4 )
                {
                    for ( int currentRow = 0; currentRow < lights->rowCount(); ++currentRow )
                    {
                        if ( lights->cellWidget(currentRow, 4) == spin )
                        {
                            linkLightColor(currentRow);
                            break;
                        }
                    }
                }
                invalidate(false);
            });
        }
        auto* range = qobject_cast<QDoubleSpinBox*>(lights->cellWidget(row, 8));
        range->setEnabled(kind->currentIndex() == 0);
        QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, [this, kind, range]()
        {
            range->setEnabled(kind->currentIndex() == 0);
            invalidate(false);
        });
        linkLightColor(row);
    }

    void addLight(const bool directional)
    {
        if ( lights->rowCount() >= 256 )
        {
            status->setText(QObject::tr("At most 256 lights are supported."));
            return;
        }
        assets::ModelLightingLight light;
        light.kind = directional ? assets::ModelLightKind::Directional : assets::ModelLightKind::Point;
        light.position = {0, 0, 1};
        appendLight(light);
        lights->selectRow(lights->rowCount() - 1);
        invalidate(false);
    }

    void moveLight(const int delta)
    {
        const int row = lights->currentRow();
        if ( row < 0 || row + delta < 0 || row + delta >= lights->rowCount() )
        {
            return;
        }
        std::vector<assets::ModelLightingLight> values;
        for ( int index = 0; index < lights->rowCount(); ++index )
        {
            values.push_back(lightAt(index));
        }
        std::swap(values[static_cast<std::size_t>(row)], values[static_cast<std::size_t>(row + delta)]);
        lights->setRowCount(0);
        for ( const auto& light : values )
        {
            appendLight(light);
        }
        lights->selectRow(row + delta);
        invalidate(false);
    }

    assets::ModelLightingOptions options() const
    {
        assets::ModelLightingOptions result;
        result.profile = profile->currentIndex() == 2 ? assets::ModelLightingProfile::IjimRgb : assets::ModelLightingProfile::ClassicScalar;
        result.blend = blend->currentIndex() == 0 ? assets::ModelLightingBlend::Replace : assets::ModelLightingBlend::Add;
        result.ambient = {static_cast<float>(ambient[0]->value()), static_cast<float>(ambient[1]->value()),
            static_cast<float>(ambient[2]->value()), 1.0f};
        result.selfOcclusion = occlusion->isChecked();
        result.shadowBias = bias->value();
        for ( const auto& limit : limits )
        {
            result.limits.*(limit.member) = static_cast<std::size_t>(limit.spin->value());
        }
        for ( int row = 0; row < lights->rowCount(); ++row )
        {
            result.lights.push_back(lightAt(row));
        }
        return result;
    }

    void start(std::function<JobResult()> work, const int operation, const int previewLod = 0)
    {
        const auto requestedGeneration = generation;
        const auto token = cancellation;
        busy = true;
        updateButtons();
        auto ownedWatcher = std::make_unique<QFutureWatcher<JobResult>>(&dialog);
        auto* pending = ownedWatcher.get();
        watcher = pending;
        QObject::connect(pending, &QFutureWatcher<JobResult>::finished, &dialog,
            [this, pending, requestedGeneration, token, operation, previewLod]()
        {
            watcher = nullptr;
            busy = false;
            pending->deleteLater();
            guardOperation([&]()
            {
                if ( token->load() || requestedGeneration != generation || !current() )
                {
                    candidate.reset();
                    status->setText(QObject::tr("Result discarded: cancelled or its source/options/context changed."));
                    updateButtons();
                    return;
                }
                auto result = pending->result();
                if ( !result.error.isEmpty() )
                {
                    status->setText(result.error);
                }
                else if ( operation == 0 )
                {
                    sourceBytes = std::move(result.source);
                    diagnostics->setPlainText(QObject::tr("Source SHA-256: %1\n\n").arg(hashText(sourceBytes))
                        + inspectionText(*result.inspection));
                    lod->setMaximum(static_cast<int>(result.inspection->geosets) - 1);
                    status->setText(QObject::tr("Source inspected. Choose the existing model's profile and calculate a proposed variant."));
                }
                else if ( operation == 1 )
                {
                    candidate = std::move(result.bake);
                    const QByteArray output(candidate->text.data(), static_cast<qsizetype>(candidate->text.size()));
                    diagnostics->setPlainText(QObject::tr("Source SHA-256: %1\nProposed SHA-256: %2\n"
                        "Changed vertices: %3; clamped: %4; occluded samples: %5; zero normals: %6\n\nORIGINAL\n%7\nPROPOSED\n%8")
                        .arg(hashText(sourceBytes)).arg(hashText(output)).arg(candidate->changedVertices)
                        .arg(candidate->clampedVertices).arg(candidate->occludedSamples).arg(candidate->zeroNormalVertices)
                        .arg(inspectionText(candidate->before)).arg(inspectionText(candidate->after)));
                    status->setText(QObject::tr("Proposed lighting calculated for every LOD. Compare it, then save an explicit new .3do variant."));
                }
                else
                {
                    if ( comparison )
                    {
                        comparison->close();
                    }
                    auto previewContext = context;
                    previewContext.project = profile->currentIndex() == 2 ? ProjectType::IJIM
                        : profile->currentIndex() == 1 ? ProjectType::MOTS : ProjectType::JKDF2;
                    comparison = new ComparisonDialog(previewContext, std::move(result.preview), previewLod, &dialog);
                    comparison->setWindowModality(Qt::WindowModal);
                    comparison->show();
                }
                updateButtons();
            });
        });
        pending->setFuture(detail::runPreviewWorkerTask([work = std::move(work)]()
        {
            try
            {
                return work();
            }
            catch ( const std::exception& error )
            {
                JobResult result;
                result.error = QString::fromUtf8(error.what());
                return result;
            }
        }));
        ownedWatcher.release();
    }

    bool begin()
    {
        if ( busy )
        {
            return false;
        }
        if ( !current() )
        {
            invalidate(false);
            status->setText(QObject::tr("The document/resource context changed. Close this dialog and reopen Model Lighting."));
            return false;
        }
        cancellation = std::make_shared<std::atomic_bool>(false);
        return true;
    }

    void loadSource()
    {
        if ( !begin() )
        {
            return;
        }
        candidate.reset();
        sourceBytes.clear();
        capturedSource = {sourceEdit->text(), sourceKind->currentIndex() == 1, context.resources};
        if ( !capturedSource.resource )
        {
            capturedSource.name = QFileInfo(capturedSource.name).absoluteFilePath();
        }
        const auto source = capturedSource;
        const auto requestedLimits = options().limits;
        status->setText(QObject::tr("Reading and inspecting immutable source bytes…"));
        start([source, requestedLimits]()
        {
            JobResult result;
            result.source = readSource(source, requestedLimits.sourceBytes);
            result.inspection = assets::inspectModelLighting(byteSpan(result.source), requestedLimits);
            return result;
        }, 0);
    }

    void bake()
    {
        if ( sourceBytes.isEmpty() || !begin() )
        {
            return;
        }
        candidate.reset();
        auto recipe = options();
        const auto token = cancellation;
        recipe.cancelled = [token]() { return token->load(); };
        const auto source = sourceBytes;
        status->setText(QObject::tr("Calculating all-LOD lighting from the captured source…"));
        start([source, recipe = std::move(recipe)]()
        {
            JobResult result;
            result.bake = assets::bakeModelLighting(byteSpan(source), recipe);
            return result;
        }, 1);
    }

    void showComparison()
    {
        if ( !candidate || !begin() )
        {
            return;
        }
        const auto before = sourceBytes;
        const QByteArray after(candidate->text.data(), static_cast<qsizetype>(candidate->text.size()));
        const int selectedLod = lod->value();
        const auto token = cancellation;
        status->setText(QObject::tr("Preparing the selected LOD for a controlled comparison…"));
        start([before, after, selectedLod, token]()
        {
            JobResult result;
            result.preview[0] = std::make_shared<assets::Model3do>(assets::loadFrom3DO(byteSpan(before), {selectedLod, true}));
            if ( !token->load() )
            {
                result.preview[1] = std::make_shared<assets::Model3do>(assets::loadFrom3DO(byteSpan(after), {selectedLod, true}));
            }
            return result;
        }, 2, selectedLod);
    }

    void publish()
    {
        if ( busy || !candidate )
        {
            return;
        }
        try
        {
            const QString target = outputEdit->text();
            if ( target.isEmpty() || !target.endsWith(QStringLiteral(".3do"), Qt::CaseInsensitive) )
            {
                throw std::runtime_error("Choose a new destination ending in .3do.");
            }
            const auto stillCurrent = [this]()
            {
                bool sameSource = false;
                try
                {
                    sameSource = current()
                        && readSource(capturedSource, assets::ModelLightingLimits{}.sourceBytes) == sourceBytes;
                }
                catch ( const std::exception& error )
                {
                    invalidate(false);
                    throw std::runtime_error(std::string("Could not verify the source or document/resource context: ")
                        + error.what());
                }
                if ( !sameSource )
                {
                    invalidate(false);
                    throw std::runtime_error("Source or document/resource context changed. Reload and calculate again; nothing was published.");
                }
            };
            stillCurrent();
            const auto destination = std::filesystem::path(QFileInfo(target).absoluteFilePath().toStdWString());
            assets::detail::StagedAssetFile staged(destination);
            staged.writeAndClose(std::as_bytes(std::span(candidate->text.data(), candidate->text.size())));
            stillCurrent();
            staged.publishNew(destination);
            status->setText(QObject::tr("Saved new variant: %1. Source and document bindings were preserved.").arg(target));
        }
        catch ( const std::exception& error )
        {
            status->setText(QObject::tr("Save refused: %1\nChoose an unused .3do destination on a filesystem supporting atomic create-only publication.")
                .arg(QString::fromUtf8(error.what())));
        }
        updateButtons();
    }
};

ModelLightingDialog::ModelLightingDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}

ModelLightingDialog::~ModelLightingDialog() = default;

} // namespace sed::ui::resources
