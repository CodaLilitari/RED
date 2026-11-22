#include "ui/resources/model_uv_inspector.hpp"

#include "ui/components/preview_worker_pool.hpp"

#include <QAbstractTableModel>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPalette>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableView>
#include <QVBoxLayout>

#include <optional>
#include <set>

namespace sed::ui::resources
{
namespace
{
// Generate table values on demand, including when one UV row has many uses.
class CornerTable final : public QAbstractTableModel
{
public:
    explicit CornerTable(NativeModelUvPreview& canvas) : QAbstractTableModel(&canvas), canvas_(canvas) {}

    void refresh()
    {
        beginResetModel();
        count_ = static_cast<int>(canvas_.selectedCorners().size());
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : count_; }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 2; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if ( orientation == Qt::Horizontal && role == Qt::DisplayRole )
            return section == 0 ? tr("Source face") : tr("Corner");
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        const auto& corners = canvas_.selectedCorners();
        if ( role != Qt::DisplayRole || !index.isValid() || index.row() < 0
            || static_cast<std::size_t>(index.row()) >= corners.size() ) return {};
        const auto& address = corners[static_cast<std::size_t>(index.row())];
        return QVariant::fromValue(static_cast<qulonglong>(index.column() == 0 ? address.face : address.corner));
    }

private:
    NativeModelUvPreview& canvas_;
    int count_{};
};

struct CaptureResult
{
    std::optional<ModelUvMaterialCapture> capture;
    QString error;
};

QString captureReadout(const ModelUvMaterialCapture& capture, bool looseMaterial, bool loosePalette)
{
    const QString materialType = looseMaterial ? QObject::tr("Loose MAT override") : QObject::tr("Project MAT");
    QStringList lines{QObject::tr("%1: %2\nSHA-256 %3").arg(materialType, capture.material.description, capture.material.sha256)};
    if ( !capture.palette.sha256.isEmpty() )
    {
        const QString paletteType = loosePalette ? QObject::tr("Loose CMP") : QObject::tr("Project CMP");
        lines << QObject::tr("%1: %2\nSHA-256 %3").arg(paletteType, capture.palette.description, capture.palette.sha256);
    }
    if ( capture.kind == assets::MatPreviewKind::SolidIndexed )
    {
        lines << QObject::tr("Solid palette index %1; no texture dimensions.").arg(capture.solidColorIndex);
        if ( capture.solidColor.isValid() ) lines << QObject::tr("Unlit color %1").arg(capture.solidColor.name());
    }
    else
    {
        lines << QObject::tr("Unlit cel 0, mip 0 · %1 × %2 pixels").arg(capture.sourceSize.width()).arg(capture.sourceSize.height());
    }
    if ( !capture.colorsUnavailable.isEmpty() ) lines << capture.colorsUnavailable;
    return lines.join(QLatin1Char('\n'));
}
}

struct ModelUvInspector::State
{
    ModelUvInspector& widget;
    Context context;
    WorkHooksForTests hooks;
    std::optional<assets::NativeModelDocument> document;
    editor::NativeModelSelection selection;
    std::uint64_t sourceGeneration{}, requestGeneration{};
    std::optional<ModelUvMaterialCapture> captured;
    QSize materialExtent;
    QFutureWatcher<CaptureResult>* active{};
    NativeModelUvPreview* canvas{};
    CornerTable* rows{};
    QComboBox* slot{};
    QLineEdit* mat{};
    QLineEdit* cmp{};
    QPushButton* load{};
    QLabel* status{};
    QWidget* solidChip{};
    QLabel* summary{};

    State(ModelUvInspector& owner, Context project) : widget(owner), context(std::move(project))
    {
        auto* layout = new QVBoxLayout(&widget);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* inputs = new QGridLayout;
        layout->addLayout(inputs);
        slot = new QComboBox;
        slot->setObjectName("uvMaterialSlot");
        inputs->addWidget(new QLabel(tr("Material slot")), 0, 0);
        inputs->addWidget(slot, 0, 1, 1, 2);
        const auto pathRow = [&](int row, QString label, QString placeholder, QString filter,
                                 const char* fieldName, const char* buttonName)
        {
            auto* field = new QLineEdit;
            field->setObjectName(fieldName);
            field->setPlaceholderText(placeholder);
            auto* browse = new QPushButton(tr("Browse…"));
            browse->setObjectName(buttonName);
            inputs->addWidget(new QLabel(label), row, 0);
            inputs->addWidget(field, row, 1);
            inputs->addWidget(browse, row, 2);
            QObject::connect(browse, &QPushButton::clicked, &widget, [this, field, label, filter]
            {
                const auto path = QFileDialog::getOpenFileName(&widget, label, field->text(), filter);
                if ( !path.isEmpty() ) field->setText(path);
            });
            QObject::connect(field, &QLineEdit::textChanged, &widget, [this, field] { invalidate(field == cmp); });
            return field;
        };
        mat = pathRow(1, tr("Loose MAT"), tr("Use selected project material"), tr("Sith material (*.mat)"),
            "uvMaterialPath", "uvMaterialBrowse");
        cmp = pathRow(2, tr("Loose CMP"), tr("Use project master colormap"), tr("Sith colormap (*.cmp)"),
            "uvPalettePath", "uvPaletteBrowse");
        load = new QPushButton(tr("Load material"));
        load->setObjectName("uvLoadMaterial");
        auto* reframe = new QPushButton(tr("Reframe UV"));
        reframe->setObjectName("uvReframe");
        inputs->addWidget(load, 3, 1);
        inputs->addWidget(reframe, 3, 2);
        status = new QLabel;
        status->setObjectName("uvCaptureStatus");
        status->setTextFormat(Qt::PlainText);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        status->setWordWrap(true);
        auto* readout = new QHBoxLayout;
        solidChip = new QWidget;
        solidChip->setObjectName("uvSolidColorChip");
        solidChip->setAccessibleName(tr("Captured solid material color"));
        solidChip->setFixedSize(32, 24);
        solidChip->setAutoFillBackground(true);
        readout->addWidget(solidChip, 0, Qt::AlignTop);
        readout->addWidget(status, 1);
        layout->addLayout(readout);
        canvas = new NativeModelUvPreview;
        layout->addWidget(canvas, 1);
        summary = new QLabel;
        summary->setObjectName("uvCornerSummary");
        summary->setWordWrap(true);
        layout->addWidget(summary);
        auto* table = new QTableView;
        table->setObjectName("uvCornerTable");
        rows = new CornerTable(*canvas);
        table->setModel(rows);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setMaximumHeight(90);
        layout->addWidget(table);
        QObject::connect(reframe, &QPushButton::clicked, canvas, &NativeModelUvPreview::reframe);
        QObject::connect(load, &QPushButton::clicked, &widget, [this] { start(); });
        QObject::connect(slot, &QComboBox::currentIndexChanged, &widget, [this]
        {
            const QSignalBlocker block(mat);
            mat->clear();
            invalidate();
            updateCanvas(true);
        });
        invalidate();
    }

    int materialSlot() const { return slot->currentIndex() < 0 ? -1 : slot->currentData().toInt(); }

    void updateButton() { load->setEnabled(document.has_value() && materialSlot() >= 0 && !active); }

    void invalidate(bool keepMaterialExtent = false)
    {
        ++requestGeneration;
        captured.reset();
        solidChip->hide();
        if ( !keepMaterialExtent ) materialExtent = {};
        canvas->setMaterial({}, materialExtent);
        status->setText(materialExtent.isEmpty() ? tr("Material dimensions and colors unavailable.")
            : tr("%1 × %2 pixels · colors unavailable.").arg(materialExtent.width()).arg(materialExtent.height()));
        updateButton();
    }

    QString contextError() const
    {
        try
        {
            if ( !context.isCurrent || context.isCurrent() ) return {};
            return tr("Project context changed. Reopen Model Studio to capture this material.");
        }
        catch ( const std::exception& error )
        {
            return tr("Project context check failed. %1").arg(QString::fromUtf8(error.what()));
        }
        catch ( ... )
        {
            return tr("Project context could not be checked.");
        }
    }

    void updateCanvas(bool fit)
    {
        if ( !document ) canvas->clear();
        else canvas->setModel(*document, selection, materialSlot(), fit);
        rows->refresh();
        summary->setText(canvas->overlayAvailable()
            ? tr("%1 corners in this material · %2 uses of the selected UV row in this mesh")
                .arg(canvas->selectedCorners().size()).arg(canvas->totalSelectedUses())
            : canvas->overlayError());
    }

    void start()
    {
        if ( active || !document || materialSlot() < 0 ) return;
        invalidate(true);
        if ( const auto error = contextError(); !error.isEmpty() )
        {
            invalidate();
            status->setText(error);
            return;
        }
        const auto& model = document->geosets()[selection.geoset];
        ModelUvMaterialRequest request{context.resources, model.materials[static_cast<std::size_t>(materialSlot())],
            context.masterColormap, {}, {}};
        if ( !mat->text().isEmpty() ) request.materialPath = QFileInfo(mat->text()).filesystemFilePath();
        if ( !cmp->text().isEmpty() ) request.palettePath = QFileInfo(cmp->text()).filesystemFilePath();
        status->setText(tr("Capturing material…"));
        active = new QFutureWatcher<CaptureResult>(&widget);
        updateButton();
        QObject::connect(active, &QFutureWatcher<CaptureResult>::finished, &widget,
            [this, generation = requestGeneration, watcher = active]
        {
            auto result = watcher->future().takeResult();
            active = nullptr;
            watcher->deleteLater();
            updateButton();
            if ( generation != requestGeneration ) return;
            if ( const auto error = contextError(); !error.isEmpty() )
            {
                invalidate();
                status->setText(error);
                return;
            }
            if ( !result.capture )
            {
                invalidate();
                status->setText(tr("Material unavailable. %1").arg(result.error));
                return;
            }
            captured = std::move(result.capture);
            if ( captured->solidColor.isValid() )
            {
                auto palette = solidChip->palette();
                palette.setColor(QPalette::Window, captured->solidColor);
                solidChip->setPalette(palette);
                solidChip->show();
            }
            materialExtent = captured->sourceSize;
            canvas->setMaterial(captured->image, materialExtent);
            status->setText(captureReadout(*captured, !mat->text().isEmpty(), !cmp->text().isEmpty()));
        });
        active->setFuture(detail::runPreviewWorkerTask([request = std::move(request), before = hooks.beforeCapture]
        {
            CaptureResult result;
            try
            {
                if ( before ) before();
                result.capture = captureModelUvMaterial(request);
            }
            catch ( const std::exception& e ) { result.error = QString::fromUtf8(e.what()); }
            catch ( ... ) { result.error = tr("Could not capture material data."); }
            return result;
        }));
    }

    void setModel(assets::NativeModelDocument replacement, editor::NativeModelSelection target,
                  std::uint64_t generation, bool fit)
    {
        const bool newSource = !document || generation != sourceGeneration;
        const bool newMesh = newSource || target.geoset != selection.geoset || target.sourceMesh != selection.sourceMesh;
        document = std::move(replacement);
        selection = target;
        sourceGeneration = generation;
        if ( newMesh )
        {
            const QSignalBlocker blockSlot(slot), blockMat(mat), blockCmp(cmp);
            slot->clear();
            mat->clear();
            if ( newSource ) cmp->clear();
            const auto models = document->geosets();
            if ( target.geoset < models.size() )
            {
                const auto& model = models[target.geoset];
                if ( target.sourceMesh < model.meshes.size() )
                {
                    std::set<int> used;
                    for ( const auto& face : model.meshes[target.sourceMesh].faces )
                    {
                        if ( face.material >= 0 && static_cast<std::size_t>(face.material) < model.materials.size() )
                            used.insert(face.material);
                    }
                    for ( int index : used )
                    {
                        slot->addItem(tr("%1 · %2").arg(index).arg(QString::fromStdString(model.materials[index])), index);
                    }
                }
            }
            invalidate();
        }
        updateCanvas(newMesh || fit);
    }
};

ModelUvInspector::ModelUvInspector(Context context, QWidget* parent)
    : QWidget(parent), state_(std::make_unique<State>(*this, std::move(context)))
{
    setObjectName("modelStudioUvInspector");
}

ModelUvInspector::~ModelUvInspector() = default;
void ModelUvInspector::setModel(assets::NativeModelDocument doc, editor::NativeModelSelection selection,
                                std::uint64_t sourceGeneration, bool fit)
{
    state_->setModel(std::move(doc), selection, sourceGeneration, fit);
}

void ModelUvInspector::clear()
{
    state_->document.reset();
    const QSignalBlocker blockSlot(state_->slot), blockMat(state_->mat), blockCmp(state_->cmp);
    state_->slot->clear();
    state_->mat->clear();
    state_->cmp->clear();
    state_->invalidate();
    state_->updateCanvas(false);
}

bool ModelUvInspector::busy() const { return state_->active != nullptr; }
NativeModelUvPreview* ModelUvInspector::canvas() const { return state_->canvas; }
const ModelUvMaterialCapture* ModelUvInspector::capture() const { return state_->captured ? &*state_->captured : nullptr; }
void ModelUvInspector::setWorkHooksForTests(WorkHooksForTests hooks) { state_->hooks = std::move(hooks); }
}
