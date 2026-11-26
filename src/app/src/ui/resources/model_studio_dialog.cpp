#include "ui/resources/model_studio_dialog.hpp"

#include "ui/resources/native_model_mesh_preview.hpp"
#include "ui/resources/native_model_variant.hpp"
#include "ui/resources/model_pose_preview.hpp"
#include "ui/resources/model_uv_inspector.hpp"
#include "ui/components/dialog_window_policy.hpp"
#include "ui/components/preview_worker_pool.hpp"
#include "ui/components/ui_decimal_format.hpp"
#include "ui/shortcuts/shortcut_context_utils.hpp"
#include "sed/assets/key.hpp"
#include "../../../../core/src/assets/atomic_file_write.hpp"

#include <QApplication>
#include <QAbstractListModel>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sed::ui::resources
{
namespace
{
using Session = editor::NativeModelSession;
using Selection = editor::NativeModelSelection;
using Revision = editor::NativeModelRevision;
using Interaction = editor::NativeModelInteractionKind;

// UV ordinals can be numerous. Generate visible labels on demand rather than
// allocating an item and a QString for every row on each selection change.
class UvRowModel final : public QAbstractListModel
{
public:
    explicit UvRowModel(QObject* parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : rows_ + 1; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if ( !index.isValid() || index.row() < 0 || index.row() > rows_ || role != Qt::DisplayRole ) return {};
        return index.row() == 0 ? QObject::tr("No UV row") : QObject::tr("UV %1").arg(index.row() - 1);
    }
    void setRows(std::size_t count)
    {
        const auto rows = static_cast<int>(count);
        if ( rows == rows_ ) return;
        beginResetModel();
        rows_ = rows;
        endResetModel();
    }
private:
    int rows_{};
};

struct OpenedModel
{
    NativeModelSourceSnapshot source;
    Session session;
};

// The only mutable session owner. The GUI dispatches one task at a time and
// receives immutable document handles. A task can outlive the dialog safely.
struct StudioWorker
{
    std::unique_ptr<Session> session;
    std::optional<NativeModelSourceSnapshot> source;
};

struct StudioResult
{
    ModelStudioDialog::State state;
    QString error;
    std::optional<NativeModelSourceSnapshot> source;
    std::shared_ptr<OpenedModel> opened;
    std::shared_ptr<const NativeModelVariantReview> review;
    std::shared_ptr<NativeModelVariantStage> stage;
    std::shared_ptr<assets::detail::StagedAssetFile> lodFile;
    std::filesystem::path lodDestination;
    std::optional<assets::KeyFile> key;
    std::optional<assets::NativeModelDocument> keyDocument;
    std::filesystem::path keyIdentity;
    bool appliedChange{};
};

void captureState(const StudioWorker& worker, StudioResult& result)
{
    result.source = worker.source;
    if ( !worker.session ) return;
    const auto& session = *worker.session;
    result.state.committed = session.committed();
    result.state.displayed = session.displayed();
    result.state.selection = session.selection();
    result.state.revision = session.revision();
    result.state.dirty = session.dirty();
    result.state.canUndo = session.canUndo();
    result.state.canRedo = session.canRedo();
}

QString exactNumber(double value, const QLocale& locale)
{
    return locale.toString(value, 'g', std::numeric_limits<double>::max_digits10);
}

QString meshReadout(const assets::NativeModelDocument& document, const Selection& selection)
{
    const auto& model = document.geosets()[selection.geoset];
    const auto& mesh = model.meshes[selection.sourceMesh];
    const auto instances = std::count_if(model.nodes.begin(), model.nodes.end(), [&](const auto& node)
    {
        return node.mesh >= 0 && static_cast<std::size_t>(node.mesh) == selection.sourceMesh;
    });
    auto text = QStringLiteral("LOD %1 · source mesh %2: %3\n%4 vertices · %5 faces · %6 joint instances\n"
        "Retained authored mesh radius: %7\n")
        .arg(selection.geoset).arg(selection.sourceMesh).arg(QString::fromStdString(mesh.name))
        .arg(mesh.vertices.size()).arg(mesh.faces.size()).arg(instances)
        .arg(formatUiNumber(mesh.authoredRadius, 'g', 17));
    if ( !mesh.vertices.empty() )
    {
        std::array<double, 3> low{mesh.vertices.front().x, mesh.vertices.front().y, mesh.vertices.front().z};
        auto high = low;
        for ( const auto& vertex : mesh.vertices )
        {
            const std::array coordinates{vertex.x, vertex.y, vertex.z};
            for ( std::size_t axis = 0; axis < coordinates.size(); ++axis )
            {
                low[axis] = std::min(low[axis], coordinates[axis]);
                high[axis] = std::max(high[axis], coordinates[axis]);
            }
        }
        text += QStringLiteral("Mesh-local bounds (including loose vertices):\nX %1 … %2; Y %3 … %4; Z %5 … %6\n")
            .arg(formatUiNumber(low[0], 'g', 17), formatUiNumber(high[0], 'g', 17),
                formatUiNumber(low[1], 'g', 17), formatUiNumber(high[1], 'g', 17),
                formatUiNumber(low[2], 'g', 17), formatUiNumber(high[2], 'g', 17));
    }
    if ( selection.textureVertex )
    {
        const auto uv = *selection.textureVertex;
        std::size_t corners = 0;
        for ( const auto& face : mesh.faces )
            corners += std::count(face.texVertices.begin(), face.texVertices.end(), static_cast<std::int32_t>(uv));
        text += QStringLiteral("UV row %1: (%2, %3) texture pixels · %4 referencing corners per source mesh\n")
            .arg(uv).arg(formatUiNumber(mesh.texVertices[uv].u, 'g', 9), formatUiNumber(mesh.texVertices[uv].v, 'g', 9))
            .arg(corners);
    }
    text += QStringLiteral("\nAll LODs, hierarchy, normals, authored radii and stored lighting are retained. "
        "Shape edits may require radius, lighting and external ARCH review. Wireframe preview does not show UV appearance or establish game acceptance.");
    return text;
}
}

struct ModelStudioDialog::Impl
{
    enum class Job { None, Open, Adopt, Select, Preview, Apply, Cancel, Undo, Redo, Review, Stage, Acknowledge, Key, LodFile };
    struct PreviewRequest
    {
        Selection selection;
        Revision revision;
        Interaction kind{};
        std::uint64_t generation{};
        double scale{1};
        TVector translation{};
        float u{}, v{};
    };
    struct DeferredCompletion
    {
        Job kind;
        std::uint64_t generation;
        StudioResult result;
    };

    ModelStudioDialog& dialog;
    Context context;
    WorkHooksForTests hooks;
    std::shared_ptr<StudioWorker> worker = std::make_shared<StudioWorker>();
    State view;
    std::optional<NativeModelSourceSnapshot> original;
    QFutureWatcher<StudioResult>* watcher{};
    Job job{Job::None};
    std::uint64_t inputGeneration{}, proposalGeneration{}, uvSourceGeneration{};
    bool closed{}, refreshing{}, draft{}, proposal{}, cancelRequested{}, sealed{};
    bool decisionActive{};
    bool closeAfterHistory{};
    std::optional<DeferredCompletion> deferredCompletion;
    std::optional<PreviewRequest> pendingPreview;
    std::shared_ptr<const NativeModelVariantReview> review;
    Revision reviewedRevision;
    std::function<void()> afterSave;
    QLocale inputLocale = uiNumericLocale();
    QString loadedName;
    QString publishedMessage;
    QString contextError;
    int presentedLod{}, listedLod{-1}, listedMesh{-1};

    QLineEdit *source{}, *output{}, *keyPath{};
    QCheckBox* resource{};
    QComboBox *lod{}, *mesh{}, *uvRow{}, *operation{};
    UvRowModel* uvRows{};
    std::array<QLineEdit*, 9> numbers{};
    QFormLayout* editForm{};
    QPushButton *open{}, *preview{}, *apply{}, *cancel{}, *undo{}, *redo{}, *reviewButton{}, *save{}, *cancelReview{}, *keyButton{};
    QPushButton *browse{}, *keyBrowse{}, *destinationBrowse{};
    QPushButton *assembleLods{}, *extractLod{};
    QLabel *status{}, *identity{};
    QPlainTextEdit *details{}, *reviewText{};
    NativeModelMeshPreview* meshView{};
    ModelUvInspector* uvInspector{};

    Impl(ModelStudioDialog& owner, Context value) : dialog(owner), context(std::move(value))
    {
        dialog.setObjectName(QStringLiteral("modelStudioDialog"));
        dialog.setWindowTitle(tr("Model Studio"));
        applyDialogWindowPolicy(dialog);
        auto* layout = new QVBoxLayout(&dialog);
        auto* scroll = new QScrollArea(&dialog);
        scroll->setObjectName(QStringLiteral("modelStudioScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget(scroll);
        auto* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        bodyLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        scroll->setWidget(body);
        layout->addWidget(scroll, 1);
        auto* sourceRow = new QHBoxLayout;
        source = new QLineEdit(&dialog);
        source->setObjectName(QStringLiteral("modelStudioSource"));
        source->setPlaceholderText(tr("Classic 3DO 2.1 file or mounted resource name"));
        resource = new QCheckBox(tr("Project resource"), &dialog);
        resource->setObjectName(QStringLiteral("modelStudioResource"));
        resource->setEnabled(context.resources != nullptr);
        browse = button(tr("Browse…"), "modelStudioBrowse");
        open = button(tr("Open"), "modelStudioOpen");
        sourceRow->addWidget(source, 1);
        sourceRow->addWidget(resource);
        sourceRow->addWidget(browse);
        sourceRow->addWidget(open);
        bodyLayout->addLayout(sourceRow);
        auto* lodTools = new QHBoxLayout;
        assembleLods = button(tr("Assemble static LODs…"), "modelStudioAssembleLods");
        extractLod = button(tr("Extract static LOD…"), "modelStudioExtractLod");
        lodTools->addWidget(assembleLods);
        lodTools->addWidget(extractLod);
        lodTools->addStretch();
        bodyLayout->addLayout(lodTools);
        QObject::connect(assembleLods, &QPushButton::clicked, &dialog, [this] { lodFiles(true); });
        QObject::connect(extractLod, &QPushButton::clicked, &dialog, [this] { lodFiles(false); });
        identity = new QLabel(tr("Open a model to begin. The level remains independent."), &dialog);
        identity->setWordWrap(true);
        identity->setTextFormat(Qt::PlainText);
        identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bodyLayout->addWidget(identity);

        auto* splitter = new QSplitter(&dialog);
        bodyLayout->addWidget(splitter, 1);
        auto* visual = new QWidget(splitter);
        auto* visualLayout = new QVBoxLayout(visual);
        visualLayout->setContentsMargins(0, 0, 0, 0);
        auto* visualTabs = new QTabWidget(visual);
        visualTabs->setObjectName(QStringLiteral("modelStudioVisualTabs"));
        visualLayout->addWidget(visualTabs, 1);
        auto* geometry = new QWidget(visualTabs);
        auto* geometryLayout = new QVBoxLayout(geometry);
        geometryLayout->setContentsMargins(0, 0, 0, 0);
        meshView = new NativeModelMeshPreview(geometry);
        meshView->setObjectName(QStringLiteral("modelStudioMeshView"));
        meshView->setMinimumSize(320, 220);
        geometryLayout->addWidget(meshView, 1);
        auto* reframe = button(tr("Reframe selected mesh"), "modelStudioReframe");
        geometryLayout->addWidget(reframe);
        visualTabs->addTab(geometry, tr("Geometry"));
        uvInspector = new ModelUvInspector({context.resources, context.masterColormap, context.isCurrent}, visualTabs);
        visualTabs->addTab(uvInspector, tr("UVs"));
        details = new QPlainTextEdit(visual);
        details->setObjectName(QStringLiteral("modelStudioDetails"));
        details->setReadOnly(true);
        details->setMaximumHeight(205);
        visualLayout->addWidget(details);

        auto* controls = new QWidget(splitter);
        auto* controlLayout = new QVBoxLayout(controls);
        auto* form = new QFormLayout;
        editForm = form;
        controlLayout->addLayout(form);
        lod = combo("modelStudioLod");
        mesh = combo("modelStudioMesh");
        uvRow = combo("modelStudioUvRow");
        uvRows = new UvRowModel(uvRow);
        uvRow->setModel(uvRows);
        uvRow->setMinimumContentsLength(12);
        uvRow->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        if ( auto* list = qobject_cast<QListView*>(uvRow->view()) ) list->setUniformItemSizes(true);
        operation = combo("modelStudioOperation");
        operation->addItems({tr("Scale and translate mesh"), tr("Edit existing UV row"), tr("Edit insertion offset")});
        form->addRow(tr("LOD"), lod);
        form->addRow(tr("Source mesh"), mesh);
        form->addRow(tr("UV row"), uvRow);
        form->addRow(tr("Operation"), operation);
        const std::array names{"modelStudioScale", "modelStudioX", "modelStudioY", "modelStudioZ", "modelStudioU", "modelStudioV",
                               "modelStudioInsertX", "modelStudioInsertY", "modelStudioInsertZ"};
        const std::array labels{tr("Positive uniform scale"), tr("Translate X"), tr("Translate Y"), tr("Translate Z"),
                                tr("U (texture pixels)"), tr("V (texture pixels)"), tr("Insert X"), tr("Insert Y"), tr("Insert Z")};
        for ( std::size_t i = 0; i < numbers.size(); ++i )
        {
            numbers[i] = new QLineEdit(controls);
            numbers[i]->setObjectName(QString::fromLatin1(names[i]));
            numbers[i]->setText(i == 0 ? QStringLiteral("1") : QStringLiteral("0"));
            form->addRow(labels[i], numbers[i]);
            QObject::connect(numbers[i], &QLineEdit::textChanged, &dialog, [this] { inputChanged(); });
        }
        auto* editButtons = new QHBoxLayout;
        preview = button(tr("Preview"), "modelStudioPreview");
        apply = button(tr("Apply"), "modelStudioApply");
        cancel = button(tr("Cancel edit"), "modelStudioCancel");
        editButtons->addWidget(preview); editButtons->addWidget(apply); editButtons->addWidget(cancel);
        controlLayout->addLayout(editButtons);
        auto* historyLayout = new QHBoxLayout;
        undo = button(tr("Undo model edit"), "modelStudioUndo");
        redo = button(tr("Redo model edit"), "modelStudioRedo");
        historyLayout->addWidget(undo); historyLayout->addWidget(redo);
        controlLayout->addLayout(historyLayout);
        keyPath = new QLineEdit(controls);
        keyPath->setObjectName(QStringLiteral("modelStudioKey"));
        keyPath->setPlaceholderText(tr("Associated KEY file (optional)"));
        controlLayout->addWidget(keyPath);
        auto* keys = new QHBoxLayout;
        keyBrowse = button(tr("Choose KEY…"), "modelStudioKeyBrowse");
        keyButton = button(tr("Preview current model with KEY"), "modelStudioKeyPreview");
        keys->addWidget(keyBrowse); keys->addWidget(keyButton);
        controlLayout->addLayout(keys);
        controlLayout->addStretch();

        auto* publication = new QGroupBox(tr("Save a new variant"), &dialog);
        auto* publicationLayout = new QVBoxLayout(publication);
        auto* destinationRow = new QHBoxLayout;
        output = new QLineEdit(publication);
        output->setObjectName(QStringLiteral("modelStudioOutput"));
        output->setPlaceholderText(tr("New .3do destination; existing files are never replaced"));
        destinationBrowse = button(tr("Choose destination…"), "modelStudioOutputBrowse");
        reviewButton = button(tr("Review variant"), "modelStudioReview");
        destinationRow->addWidget(output, 1); destinationRow->addWidget(destinationBrowse); destinationRow->addWidget(reviewButton);
        publicationLayout->addLayout(destinationRow);
        reviewText = new QPlainTextEdit(publication);
        reviewText->setObjectName(QStringLiteral("modelStudioReviewText"));
        reviewText->setReadOnly(true);
        reviewText->setMaximumHeight(150);
        reviewText->hide();
        publicationLayout->addWidget(reviewText);
        auto* saveButtons = new QHBoxLayout;
        cancelReview = button(tr("Cancel review"), "modelStudioCancelReview");
        save = button(tr("Save reviewed variant"), "modelStudioSave");
        saveButtons->addStretch(); saveButtons->addWidget(cancelReview); saveButtons->addWidget(save);
        publicationLayout->addLayout(saveButtons);
        bodyLayout->addWidget(publication);
        auto* bottom = new QHBoxLayout;
        status = new QLabel(&dialog);
        status->setObjectName(QStringLiteral("modelStudioStatus"));
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        bottom->addWidget(status, 1);
        auto* close = button(tr("Close"), "modelStudioClose");
        bottom->addWidget(close);
        layout->addLayout(bottom);

        QObject::connect(open, &QPushButton::clicked, &dialog, [this] { openSource(); });
        QObject::connect(browse, &QPushButton::clicked, &dialog, [this]
        {
            if ( sealed || job != Job::None ) return;
            const auto path = QFileDialog::getOpenFileName(&dialog, tr("Open native model"), source->text(), tr("Sith model (*.3do)"));
            if ( !path.isEmpty() ) { source->setText(path); resource->setChecked(false); }
        });
        QObject::connect(destinationBrowse, &QPushButton::clicked, &dialog, [this]
        {
            if ( sealed || job != Job::None ) return;
            const auto path = QFileDialog::getSaveFileName(&dialog, tr("New model variant"), output->text(),
                tr("Sith model (*.3do)"), nullptr, QFileDialog::DontConfirmOverwrite);
            if ( !path.isEmpty() ) output->setText(path);
        });
        QObject::connect(keyBrowse, &QPushButton::clicked, &dialog, [this]
        {
            if ( sealed || job != Job::None ) return;
            const auto path = QFileDialog::getOpenFileName(&dialog, tr("Choose associated KEY"), keyPath->text(), tr("Sith animation (*.key)"));
            if ( !path.isEmpty() ) keyPath->setText(path);
        });
        QObject::connect(lod, &QComboBox::currentIndexChanged, &dialog, [this] { selectTarget(true); });
        QObject::connect(mesh, &QComboBox::currentIndexChanged, &dialog, [this] { selectTarget(false); });
        QObject::connect(uvRow, &QComboBox::currentIndexChanged, &dialog, [this] { selectTarget(false, true); });
        QObject::connect(operation, &QComboBox::currentIndexChanged, &dialog, [this] { updateControls(); display(true); });
        QObject::connect(preview, &QPushButton::clicked, &dialog, [this] { requestPreview(); });
        QObject::connect(apply, &QPushButton::clicked, &dialog, [this] { applyPreview(); });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, [this] { cancelEdit(); });
        QObject::connect(undo, &QPushButton::clicked, &dialog, [this] { history(false); });
        QObject::connect(redo, &QPushButton::clicked, &dialog, [this] { history(true); });
        QObject::connect(reviewButton, &QPushButton::clicked, &dialog, [this] { prepareReview(); });
        QObject::connect(cancelReview, &QPushButton::clicked, &dialog, [this] { discardReview(); });
        QObject::connect(save, &QPushButton::clicked, &dialog, [this] { saveVariant(); });
        QObject::connect(keyButton, &QPushButton::clicked, &dialog, [this] { previewKey(); });
        QObject::connect(reframe, &QPushButton::clicked, meshView, &NativeModelMeshPreview::reframe);
        QObject::connect(close, &QPushButton::clicked, &dialog, &ModelStudioDialog::reject);
        for ( const auto [sequence, isRedo] : {std::pair{QKeySequence::Undo, false}, std::pair{QKeySequence::Redo, true}} )
        {
            auto* shortcut = new QShortcut(QKeySequence(sequence), &dialog);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            QObject::connect(shortcut, &QShortcut::activated, &dialog, [this, isRedo]
            {
                if ( shortcuts::triggerNativeTextEditorUndoRedo(QApplication::focusWidget(), isRedo) ) return;
                history(isRedo);
            });
        }
        onUiNumericFormatChanged(&dialog, [this]
        {
            if ( !draft && !sealed && job == Job::None ) resetInputs();
        });
        updateControls();
        const auto available = dialog.screen() ? dialog.screen()->availableGeometry().size() : QSize(1108, 848);
        dialog.resize(QSize(1060, 800).boundedTo(available - QSize(48, 48)));
    }

    QPushButton* button(const QString& text, const char* name)
    {
        auto* result = new QPushButton(text, &dialog);
        result->setObjectName(QString::fromLatin1(name));
        result->setAutoDefault(false);
        return result;
    }

    QComboBox* combo(const char* name)
    {
        auto* result = new QComboBox(&dialog);
        result->setObjectName(QString::fromLatin1(name));
        return result;
    }

    bool current()
    {
        try
        {
            if ( !context.isCurrent || context.isCurrent() ) return true;
            contextError = tr("The project context changed. Reopen Model Studio; the current model has been retained.");
        }
        catch ( const std::exception& error ) { contextError = QString::fromUtf8(error.what()); }
        catch ( ... ) { contextError = tr("The project context could not be checked."); }
        return false;
    }

    void updateControls()
    {
        const bool idle = job == Job::None;
        const bool model = view.committed.has_value();
        const bool selected = view.selection.has_value();
        const bool editable = model && selected && !sealed && !closed;
        source->setEnabled(idle && !sealed);
        browse->setEnabled(idle && !sealed);
        resource->setEnabled(idle && !sealed && context.resources);
        open->setEnabled(idle && !sealed);
        assembleLods->setEnabled(idle && !sealed && !draft);
        extractLod->setEnabled(idle && !sealed && !draft);
        lod->setEnabled(idle && model && !draft && !sealed);
        mesh->setEnabled(idle && model && !draft && !sealed);
        uvRow->setEnabled(idle && selected && !draft && !sealed);
        operation->setEnabled(idle && selected && !draft && !sealed);
        for ( std::size_t i = 0; i < numbers.size(); ++i )
        {
            const int family = i < 4 ? 0 : i < 6 ? 1 : 2;
            numbers[i]->setEnabled(editable && !cancelRequested && (idle || job == Job::Preview) && family == operation->currentIndex());
            if (i >= 6)
                editForm->setRowVisible(numbers[i], operation->currentIndex() == 2);
        }
        preview->setEnabled(editable && (idle || job == Job::Preview) && !cancelRequested
            && (operation->currentIndex() != 1 || view.selection->textureVertex.has_value()));
        apply->setEnabled(editable && idle && proposal && proposalGeneration == inputGeneration);
        cancel->setEnabled(!sealed && draft && (idle || job == Job::Preview));
        undo->setEnabled(idle && !draft && !sealed && view.canUndo);
        redo->setEnabled(idle && !draft && !sealed && view.canRedo);
        output->setEnabled(idle && !sealed);
        destinationBrowse->setEnabled(idle && !sealed);
        reviewButton->setEnabled(idle && model && !draft && !sealed);
        save->setEnabled(idle && sealed && static_cast<bool>(review));
        cancelReview->setEnabled(idle && sealed);
        keyButton->setEnabled(idle && model && !sealed);
        keyPath->setEnabled(idle && !sealed);
        keyBrowse->setEnabled(idle && !sealed);
        view.busy = !idle;
        view.pendingInput = draft;
        dialog.setWindowTitle(view.dirty ? tr("Model Studio — unsaved changes") : tr("Model Studio"));
    }

    void display(bool reframe = false)
    {
        if ( !view.displayed || !view.selection )
        {
            meshView->clear();
            uvInspector->clear();
            details->clear();
            return;
        }
        const auto selection = *view.selection;
        const bool placement = operation->currentIndex() == 2;
        meshView->setMesh(*view.displayed, selection.geoset, selection.sourceMesh, reframe, placement);
        uvInspector->setModel(*view.displayed, selection, uvSourceGeneration, reframe);
        details->setPlainText(meshReadout(*view.displayed, selection));
        if (placement)
        {
            const auto& p = view.displayed->geosets()[selection.geoset].insertOffset;
            details->appendPlainText(tr("Placement at an unrotated, zero-offset marker (+): X %1, Y %2, Z %3")
                .arg(exactNumber(p.x, inputLocale), exactNumber(p.y, inputLocale), exactNumber(p.z, inputLocale)));
        }
    }

    void populateSelection(bool newSource = false)
    {
        refreshing = true;
        const QSignalBlocker blockLod(lod), blockMesh(mesh), blockUv(uvRow);
        if ( view.committed )
        {
            const auto geosets = view.committed->geosets();
            if ( newSource || lod->count() != static_cast<int>(geosets.size()) )
            {
                lod->clear();
                for ( std::size_t i = 0; i < geosets.size(); ++i ) lod->addItem(tr("LOD %1").arg(i));
            }
            if ( view.selection ) presentedLod = static_cast<int>(view.selection->geoset);
            presentedLod = geosets.empty() ? -1 : std::clamp(presentedLod, 0, static_cast<int>(geosets.size()) - 1);
            lod->setCurrentIndex(presentedLod);
            const int selectedMesh = view.selection ? static_cast<int>(view.selection->sourceMesh) : -1;
            if ( newSource || listedLod != presentedLod )
            {
                mesh->clear();
                if ( presentedLod >= 0 )
                {
                    const auto& model = geosets[static_cast<std::size_t>(presentedLod)];
                    for ( std::size_t i = 0; i < model.meshes.size(); ++i )
                        mesh->addItem(tr("%1: %2").arg(i).arg(QString::fromStdString(model.meshes[i].name)));
                }
            }
            mesh->setCurrentIndex(selectedMesh);
            if ( newSource || listedLod != presentedLod || listedMesh != selectedMesh )
            {
                uvRows->setRows(view.selection ? geosets[view.selection->geoset].meshes[view.selection->sourceMesh].texVertices.size() : 0);
            }
            uvRow->setCurrentIndex(view.selection && view.selection->textureVertex ? static_cast<int>(*view.selection->textureVertex) + 1 : 0);
            listedLod = presentedLod;
            listedMesh = selectedMesh;
        }
        refreshing = false;
    }

    void resetInputs()
    {
        refreshing = true;
        inputLocale = uiNumericLocale();
        for ( std::size_t i = 0; i < numbers.size(); ++i ) numbers[i]->setText(exactNumber(i == 0 ? 1.0 : 0.0, inputLocale));
        if (view.committed && !view.committed->geosets().empty())
        {
            const auto& p = view.committed->geosets().front().insertOffset;
            const std::array values{p.x, p.y, p.z};
            for (std::size_t i = 0; i < values.size(); ++i)
                numbers[6+i]->setText(exactNumber(values[i], inputLocale));
        }
        if ( view.committed && view.selection && view.selection->textureVertex )
        {
            const auto selection = *view.selection;
            const auto uv = view.committed->geosets()[selection.geoset].meshes[selection.sourceMesh].texVertices[*selection.textureVertex];
            numbers[4]->setText(inputLocale.toString(uv.u, 'g', std::numeric_limits<float>::max_digits10));
            numbers[5]->setText(inputLocale.toString(uv.v, 'g', std::numeric_limits<float>::max_digits10));
        }
        refreshing = false;
    }

    void inputChanged()
    {
        if ( refreshing || !view.committed || sealed ) return;
        if ( !draft ) inputLocale = uiNumericLocale();
        draft = true;
        const bool displayedProposal = proposal;
        proposal = false;
        ++inputGeneration;
        pendingPreview.reset();
        view.displayed = view.committed;
        if ( displayedProposal ) display();
        status->setText(tr("Inputs changed. Preview before applying; Cancel edit restores the committed model."));
        updateControls();
    }

    using Task = std::function<void(StudioWorker&, StudioResult&)>;
    void launch(Job kind, Task task, std::uint64_t generation = 0)
    {
        if ( job != Job::None || closed ) return;
        job = kind;
        updateControls();
        watcher = new QFutureWatcher<StudioResult>(&dialog);
        auto* active = watcher;
        QObject::connect(active, &QFutureWatcher<StudioResult>::finished, &dialog, [this, active, kind, generation]
        {
            auto result = active->result();
            active->deleteLater();
            watcher = nullptr;
            if ( closed ) return;
            if ( decisionActive )
            {
                deferredCompletion = DeferredCompletion{kind, generation, std::move(result)};
                return;
            }
            job = Job::None;
            complete(kind, generation, std::move(result));
        });
        active->setFuture(detail::runPreviewWorkerTask([state = worker, task = std::move(task)]
        {
            StudioResult result;
            try { task(*state, result); }
            catch ( const std::exception& error ) { result.error = QString::fromUtf8(error.what()); }
            catch ( ... ) { result.error = QStringLiteral("Model operation failed."); }
            captureState(*state, result);
            return result;
        }));
    }

    // Implemented below: each completion can schedule only one follow-up.
    void complete(Job kind, std::uint64_t generation, StudioResult result);
    void openSource();
    void lodFiles(bool assemble);
    void adopt(std::shared_ptr<OpenedModel> opened);
    bool resolveUnsaved(std::function<void()> continuation);
    bool requestClose();
    void resumeDeferredCompletion();
    void selectTarget(bool changedLod, bool changedUv = false);
    void requestPreview();
    void launchPreview(PreviewRequest request);
    void applyPreview();
    void cancelEdit();
    void history(bool isRedo);
    void prepareReview();
    void discardReview();
    void saveVariant();
    void previewKey();
};

void ModelStudioDialog::Impl::complete(Job kind, std::uint64_t generation, StudioResult result)
{
    if ( kind == Job::Open )
    {
        if ( !result.error.isEmpty() )
        {
            status->setText(result.error);
            updateControls();
            return;
        }
        auto candidate = std::move(result.opened);
        auto continuation = [this, candidate] { adopt(candidate); };
        updateControls();
        if ( resolveUnsaved(continuation) ) continuation();
        return;
    }

    view = std::move(result.state);
    original = std::move(result.source);
    const bool succeeded = result.error.isEmpty();
    const bool livePreview = kind == Job::Preview && generation == inputGeneration && !cancelRequested;
    if ( kind == Job::Preview )
    {
        proposal = succeeded && livePreview;
        if ( proposal )
        {
            proposalGeneration = generation;
            status->setText(tr("Preview ready. Apply commits one model edit; Cancel restores the baseline."));
        }
    }
    if ( !proposal ) view.displayed = view.committed;
    if ( succeeded && (kind == Job::Adopt || kind == Job::Select || kind == Job::Apply || kind == Job::Cancel || kind == Job::Undo || kind == Job::Redo) )
    {
        draft = false;
        proposal = false;
        cancelRequested = false;
        pendingPreview.reset();
        view.displayed = view.committed;
        populateSelection(kind == Job::Adopt);
        resetInputs();
        if ( kind == Job::Adopt )
        {
            ++uvSourceGeneration;
            loadedName = original->source.name;
            identity->setText(tr("%1\nSource SHA-256: %2\n%3").arg(loadedName, original->sha256, original->originIdentity));
            status->setText(tr("Model opened. Choose an explicit LOD and source mesh."));
        }
        else if ( kind == Job::Apply ) status->setText(result.appliedChange
            ? tr("Model edit applied. Save Variant publishes the cumulative committed edits.")
            : tr("The preview contains no changes. Model history is unchanged."));
        else if ( kind == Job::Cancel ) status->setText(tr("Pending edit cancelled. Exact committed model restored."));
        else if ( kind == Job::Undo || kind == Job::Redo ) status->setText(tr("Exact model revision restored."));
    }
    if ( kind == Job::Review )
    {
        sealed = succeeded;
        review = std::move(result.review);
        if ( review )
        {
            reviewedRevision = view.revision;
            reviewText->setPlainText(tr("Destination: %1\nOutput SHA-256: %2\n\n%3")
                .arg(review->destinationText, review->sha256, review->summary));
            reviewText->show();
            status->setText(tr("Review holds this exact committed revision. Save reviewed variant creates the file."));
        }
        else
        {
            afterSave = {};
            reviewText->hide();
        }
    }
    if ( kind == Job::Stage )
    {
        if ( succeeded )
        {
            // Construct all success text before the irreversible create-only
            // step. No prompts or event processing occur across this guard.
            publishedMessage = tr("Created %1\nSHA-256: %2").arg(review->destinationText, review->sha256);
            try
            {
                if ( !sealed || !review || view.revision != reviewedRevision )
                    throw std::runtime_error("The model or project context changed. Review the variant again.");
                if ( !current() ) throw std::runtime_error(contextError.toStdString());
                publishNativeModelVariant(*result.stage);
            }
            catch ( const std::exception& error )
            {
                result.error = QString::fromUtf8(error.what());
            }
            if ( result.stage->published() )
            {
                // The file exists even if acknowledgement or later reporting
                // fails. Keep the session sealed until the serial owner acks.
                const auto revision = reviewedRevision;
                status->setText(publishedMessage);
                launch(Job::Acknowledge, [revision](StudioWorker& state, StudioResult&)
                {
                    if ( !state.session || !state.session->acknowledgeSaved(revision) )
                        throw std::runtime_error("The variant was created, but its saved revision is no longer current.");
                });
                return;
            }
        }
        sealed = false;
        review.reset();
        afterSave = {};
        reviewText->hide();
    }
    if ( kind == Job::Acknowledge )
    {
        sealed = false;
        review.reset();
        reviewText->hide();
        status->setText(publishedMessage);
        if ( !succeeded ) status->setText(publishedMessage + QLatin1Char('\n') + result.error);
        auto continuation = std::exchange(afterSave, {});
        updateControls();
        if ( succeeded && continuation ) { continuation(); return; }
    }
    if (succeeded && kind == Job::LodFile && result.lodFile)
    {
        if (!current()) result.error = contextError;
        else try
        {
            result.lodFile->publishNew(result.lodDestination);
            status->setText(tr("Saved %1").arg(QString::fromStdU16String(result.lodDestination.u16string())));
        }
        catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
    }
    if ( succeeded && kind == Job::Key && result.key && result.keyDocument )
    {
        try
        {
            auto* pose = createModelPosePreview(&dialog, *result.keyDocument, std::move(*result.key), result.keyIdentity);
            pose->setAttribute(Qt::WA_DeleteOnClose);
            if ( view.selection )
            {
                if ( auto* comparison = pose->findChild<QComboBox*>(QStringLiteral("poseCompareLod")) )
                    comparison->setCurrentIndex(static_cast<int>(view.selection->geoset));
            }
            pose->show();
            status->setText(tr("KEY preview owns the displayed model snapshot, including unsaved edits."));
        }
        catch ( const std::exception& error ) { result.error = QString::fromUtf8(error.what()); }
    }
    if ( !result.error.isEmpty() && (kind != Job::Preview || livePreview) && kind != Job::Acknowledge )
        status->setText(result.error);
    display(kind == Job::Adopt);
    updateControls();
    if ( closeAfterHistory && (kind == Job::Undo || kind == Job::Redo) )
    {
        closeAfterHistory = false;
        requestClose();
        return;
    }
    if ( kind == Job::Preview )
    {
        if ( cancelRequested )
        {
            launch(Job::Cancel, [](StudioWorker& state, StudioResult&) { state.session->cancel(); });
        }
        else if ( pendingPreview )
        {
            const auto next = *pendingPreview;
            pendingPreview.reset();
            launchPreview(next);
        }
    }
}

void ModelStudioDialog::Impl::lodFiles(const bool assemble)
{
    if (job != Job::None || draft || sealed) return;
    QDialog form(&dialog);
    form.setObjectName("modelStudioLodFiles");
    form.setWindowTitle(assemble ? tr("Assemble static LODs") : tr("Extract static LOD"));
    QFormLayout layout(&form);
    auto pathRow = [&](const QString& label, const QString& id, const bool output)
    {
        auto* row = new QHBoxLayout;
        auto* edit = new QLineEdit(&form);
        edit->setMinimumWidth(270);
        edit->setObjectName(id);
        auto* browsePath = new QPushButton(tr("Browse…"), &form);
        row->addWidget(edit);
        row->addWidget(browsePath);
        layout.addRow(label, row);
        QObject::connect(browsePath, &QPushButton::clicked, &form, [&, edit, output]
        {
            const auto filter = tr("Sith model (*.3do)");
            const auto path = output
                ? QFileDialog::getSaveFileName(&form, tr("New model"), edit->text(), filter, nullptr, QFileDialog::DontConfirmOverwrite)
                : QFileDialog::getOpenFileName(&form, tr("Source model"), edit->text(), filter);
            if (!path.isEmpty()) edit->setText(path);
        });
        return edit;
    };
    std::vector<QLineEdit*> fields;
    const int count = assemble ? 4 : 1;
    for (int i = 0; i < count; ++i)
    {
        const auto name = assemble ? (i == 0 ? tr("LOD 0 (nearest)") : tr("LOD %1").arg(i)) : tr("Source model");
        auto* field = pathRow(name, QStringLiteral("lodSource%1").arg(i), false);
        if (assemble && i > 1) field->setPlaceholderText(tr("Optional"));
        if (!assemble && view.committed) field->setPlaceholderText(tr("Current committed model"));
        fields.push_back(field);
    }
    QSpinBox ordinal(&form);
    ordinal.setRange(0, 3);
    ordinal.setObjectName("lodNumber");
    if (!assemble)
    {
        ordinal.setValue(view.selection ? static_cast<int>(view.selection->geoset) : 0);
        layout.addRow(tr("LOD to extract"), &ordinal);
    }
    else ordinal.hide();
    auto* destination = pathRow(tr("New output file"), "lodDestination", true);
    QDialogButtonBox actions(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &form);
    layout.addRow(&actions);
    QObject::connect(&actions, &QDialogButtonBox::accepted, &form, &QDialog::accept);
    QObject::connect(&actions, &QDialogButtonBox::rejected, &form, &QDialog::reject);
    if (form.exec() != QDialog::Accepted) return;
    QStringList paths;
    bool gap = false;
    for (auto* field : fields)
    {
        const auto path = field->text().trimmed();
        if (path.isEmpty()) { gap = true; continue; }
        if (gap) { status->setText(tr("Fill the LOD rows in order, starting at LOD 0.")); return; }
        paths.push_back(path);
    }
    if (assemble && paths.size() < 2) { status->setText(tr("Choose at least two source LODs.")); return; }
    if (!assemble && paths.empty() && !view.committed) { status->setText(tr("Choose a source model.")); return; }
    const auto output = destination->text().trimmed();
    if (!output.endsWith(".3do", Qt::CaseInsensitive)) { status->setText(tr("Choose a new .3do output file.")); return; }
    if (!current()) { status->setText(contextError); return; }
    const auto index = static_cast<std::size_t>(ordinal.value());
    const auto target = std::filesystem::path(QFileInfo(output).absoluteFilePath().toStdU16String());
    launch(Job::LodFile, [paths, target, assemble, index](StudioWorker& state, StudioResult& result)
    {
        std::vector<assets::Model3do> models;
        for (const auto& path : paths)
        {
            const auto input = readNativeModelSource({path, false, {}});
            auto geosets = assets::readModelGeosets({input.bytes.constData(), static_cast<std::size_t>(input.bytes.size())});
            if (assemble)
            {
                if (geosets.size() != 1) throw std::runtime_error("LOD " + std::to_string(models.size()) + ": choose a single-geoset source.");
                models.push_back(std::move(geosets.front()));
            }
            else models = std::move(geosets);
        }
        std::string contents;
        if (assemble) contents = assets::assembleStaticModelLods(models);
        else
        {
            const auto geosets = paths.empty() ? state.session->committed().geosets() : std::span<const assets::Model3do>(models);
            if (index >= geosets.size()) throw std::runtime_error("The source model has no LOD " + std::to_string(index) + ".");
            contents = assets::extractStaticModelLod(geosets[index]);
        }
        const auto checked = assets::readModelGeosets(contents);
        if (checked.size() != (assemble ? models.size() : 1)) throw std::runtime_error("LOD output failed readback.");
        result.lodDestination = target;
        result.lodFile = std::make_shared<assets::detail::StagedAssetFile>(target);
        result.lodFile->writeAndClose(std::as_bytes(std::span(contents.data(), contents.size())));
    });
}

void ModelStudioDialog::Impl::openSource()
{
    if ( job != Job::None || sealed ) return;
    if ( !current() ) { status->setText(contextError); return; }
    NativeModelSource requested{source->text(), resource->isChecked(), context.resources};
    status->setText(tr("Opening model…"));
    launch(Job::Open, [requested = std::move(requested)](StudioWorker&, StudioResult& result)
    {
        auto input = readNativeModelSource(requested);
        auto session = Session::open(std::string_view(input.bytes.constData(), static_cast<std::size_t>(input.bytes.size())));
        for ( std::size_t i = 0; i < session.committed().geosets().size(); ++i )
        {
            if ( !session.committed().geosets()[i].meshes.empty() )
            {
                session.setSelection(Selection{i, 0, std::nullopt});
                break;
            }
        }
        result.opened = std::make_shared<OpenedModel>(OpenedModel{std::move(input), std::move(session)});
    });
}

void ModelStudioDialog::Impl::adopt(std::shared_ptr<OpenedModel> opened)
{
    if ( !current() ) { status->setText(contextError); return; }
    ++inputGeneration;
    launch(Job::Adopt, [opened = std::move(opened)](StudioWorker& state, StudioResult&)
    {
        auto prepared = std::make_unique<Session>(std::move(opened->session));
        auto source = std::optional<NativeModelSourceSnapshot>(opened->source);
        state.session = std::move(prepared);
        state.source = std::move(source);
    });
}

bool ModelStudioDialog::Impl::resolveUnsaved(std::function<void()> continuation)
{
    if ( !view.dirty && !draft ) return true;
    if ( decisionActive ) return false;
    QMessageBox decision(QMessageBox::Warning, tr("Keep model work?"),
        draft ? tr("This model has an unapplied edit. Apply or Cancel the edit before saving. Discard abandons the pending edit and any unsaved model changes.")
              : tr("This model has unsaved committed changes. Save a new variant before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, &dialog);
    decision.setObjectName(QStringLiteral("modelStudioUnsavedDecision"));
    decision.setDefaultButton(QMessageBox::Cancel);
    decision.button(QMessageBox::Save)->setEnabled(!draft && job == Job::None);
    decisionActive = true;
    const auto answer = decision.exec();
    decisionActive = false;
    if ( answer == QMessageBox::Discard ) return true;
    if ( answer == QMessageBox::Save )
    {
        afterSave = std::move(continuation);
        if ( review ) saveVariant(); else prepareReview();
    }
    else resumeDeferredCompletion();
    return false;
}

void ModelStudioDialog::Impl::resumeDeferredCompletion()
{
    if ( !deferredCompletion || closed ) return;
    auto deferred = std::move(*deferredCompletion);
    deferredCompletion.reset();
    job = Job::None;
    complete(deferred.kind, deferred.generation, std::move(deferred.result));
}

bool ModelStudioDialog::Impl::requestClose()
{
    if ( closed ) return true;
    if ( decisionActive ) return false;
    if ( job == Job::Undo || job == Job::Redo )
    {
        closeAfterHistory = true;
        status->setText(tr("Finishing the model history change before checking for unsaved work…"));
        return false;
    }
    const auto close = [this]
    {
        closed = true;
        ++inputGeneration;
        pendingPreview.reset();
        afterSave = {};
        review.reset();
        deferredCompletion.reset();
        uvInspector->clear();
        dialog.QDialog::reject();
    };
    if ( resolveUnsaved(close) ) { close(); return true; }
    return false;
}

void ModelStudioDialog::Impl::selectTarget(bool changedLod, bool changedUv)
{
    if ( refreshing || draft || sealed || job != Job::None || !view.committed || lod->currentIndex() < 0 ) return;
    const auto g = static_cast<std::size_t>(lod->currentIndex());
    presentedLod = lod->currentIndex();
    const auto& meshes = view.committed->geosets()[g].meshes;
    std::optional<Selection> selected;
    if ( !meshes.empty() )
    {
        const auto m = changedLod ? std::size_t{} : static_cast<std::size_t>(std::max(0, mesh->currentIndex()));
        selected = Selection{g, std::min(m, meshes.size() - 1), std::nullopt};
        if ( changedUv && uvRow->currentIndex() > 0 ) selected->textureVertex = static_cast<std::size_t>(uvRow->currentIndex() - 1);
    }
    launch(Job::Select, [selected](StudioWorker& state, StudioResult&) { state.session->setSelection(selected); });
}

void ModelStudioDialog::Impl::requestPreview()
{
    if ( !view.selection || sealed || (job != Job::None && job != Job::Preview) || cancelRequested ) return;
    PreviewRequest request;
    request.selection = *view.selection;
    request.revision = view.revision;
    request.kind = std::array{Interaction::MeshTransform, Interaction::TextureVertex, Interaction::InsertOffset}.at(operation->currentIndex());
    draft = true;
    proposal = false;
    request.generation = ++inputGeneration;
    view.displayed = view.committed;
    const auto number = [&](std::size_t index)
    {
        bool valid = false;
        const auto value = parseUiNumber(numbers[index]->text(), inputLocale, &valid);
        if ( !valid || !std::isfinite(value) ) throw std::runtime_error("Enter a complete finite number before previewing.");
        return value;
    };
    try
    {
        if ( request.kind == Interaction::MeshTransform )
        {
            request.scale = number(0);
            if ( request.scale <= 0 ) throw std::runtime_error("Uniform scale must be strictly positive.");
            request.translation = {number(1), number(2), number(3)};
        }
        else if (request.kind == Interaction::InsertOffset)
            request.translation = {number(6), number(7), number(8)};
        else
        {
            if ( !request.selection.textureVertex ) throw std::runtime_error("Select an existing UV row first.");
            const auto narrow = [&](std::size_t index)
            {
                const auto value = number(index);
                if ( std::abs(value) > std::numeric_limits<float>::max() )
                    throw std::runtime_error("The UV value is outside the stored float range.");
                const auto result = static_cast<float>(value);
                if ( value != 0 && result == 0 ) throw std::runtime_error("The UV value would underflow to zero.");
                return result;
            };
            request.u = narrow(4); request.v = narrow(5);
        }
    }
    catch ( const std::exception& error )
    {
        pendingPreview.reset();
        status->setText(QString::fromUtf8(error.what()));
        display();
        updateControls();
        return;
    }
    if ( job == Job::Preview ) pendingPreview = request;
    else launchPreview(request);
    display();
    updateControls();
}

void ModelStudioDialog::Impl::launchPreview(PreviewRequest request)
{
    const auto before = hooks.beforePreview;
    status->setText(tr("Preparing model preview…"));
    launch(Job::Preview, [request, before](StudioWorker& state, StudioResult&)
    {
        auto& session = *state.session;
        if ( session.revision() != request.revision ) throw std::runtime_error("The model revision changed. Preview again.");
        if ( !session.interaction() )
        {
            session.setSelection(request.selection);
            if ( request.kind == Interaction::MeshTransform ) session.beginMeshTransform();
            else if (request.kind == Interaction::InsertOffset) session.beginInsertOffsetEdit();
            else session.beginTextureVertexEdit();
        }
        if ( session.selection() != request.selection || session.interaction() != request.kind )
            throw std::runtime_error("Cancel the current edit before changing its target or operation.");
        if ( before ) before();
        if ( request.kind == Interaction::MeshTransform ) session.previewMeshTransform(request.scale, request.translation);
        else if (request.kind == Interaction::InsertOffset) session.previewInsertOffset(request.translation);
        else session.previewTextureVertex(request.u, request.v);
    }, request.generation);
}

void ModelStudioDialog::Impl::applyPreview()
{
    if ( job != Job::None || sealed || !proposal || proposalGeneration != inputGeneration ) return;
    const auto revision = view.revision;
    launch(Job::Apply, [revision](StudioWorker& state, StudioResult& result)
    {
        if ( state.session->revision() != revision ) throw std::runtime_error("The model revision changed. Preview again.");
        result.appliedChange = state.session->apply();
    });
}

void ModelStudioDialog::Impl::cancelEdit()
{
    if ( sealed || !draft || (job != Job::None && job != Job::Preview) ) return;
    ++inputGeneration;
    proposal = false;
    pendingPreview.reset();
    view.displayed = view.committed;
    cancelRequested = true;
    display();
    if ( job == Job::None ) launch(Job::Cancel, [](StudioWorker& state, StudioResult&) { state.session->cancel(); });
    updateControls();
}

void ModelStudioDialog::Impl::history(bool isRedo)
{
    if ( job != Job::None || draft || sealed || !view.committed || (isRedo ? !view.canRedo : !view.canUndo) ) return;
    launch(isRedo ? Job::Redo : Job::Undo, [isRedo](StudioWorker& state, StudioResult&)
    {
        if ( isRedo ) state.session->redo(); else state.session->undo();
    });
}

void ModelStudioDialog::Impl::prepareReview()
{
    if ( job != Job::None || sealed || draft || !view.committed || !original ) return;
    if ( !current() ) { status->setText(contextError); afterSave = {}; return; }
    if ( output->text().isEmpty() )
    {
        const auto path = QFileDialog::getSaveFileName(&dialog, tr("New model variant"), {},
            tr("Sith model (*.3do)"), nullptr, QFileDialog::DontConfirmOverwrite);
        if ( path.isEmpty() ) { afterSave = {}; return; }
        output->setText(path);
    }
    const auto destination = output->text();
    const auto revision = view.revision;
    status->setText(tr("Reviewing cumulative model edits and source identity…"));
    launch(Job::Review, [destination, revision](StudioWorker& state, StudioResult& result)
    {
        if ( !state.session || state.session->interaction() || state.session->revision() != revision )
            throw std::runtime_error("Resolve the pending edit before reviewing a variant.");
        result.review = reviewNativeModelVariant(*state.source, state.session->committed(), destination);
    });
}

void ModelStudioDialog::Impl::discardReview()
{
    if ( job != Job::None ) return;
    sealed = false;
    review.reset();
    afterSave = {};
    reviewText->hide();
    status->setText(tr("Variant review cancelled. The model and its history are unchanged."));
    updateControls();
}

void ModelStudioDialog::Impl::saveVariant()
{
    if ( job != Job::None || !sealed || !review || view.revision != reviewedRevision ) return;
    if ( !current() ) { discardReview(); status->setText(contextError); return; }
    const auto accepted = review;
    const auto revision = reviewedRevision;
    const auto afterStage = hooks.afterStage;
    status->setText(tr("Staging the reviewed model variant…"));
    launch(Job::Stage, [accepted, revision, afterStage](StudioWorker& state, StudioResult& result)
    {
        if ( state.session->revision() != revision || state.session->interaction() )
            throw std::runtime_error("The reviewed model revision is no longer idle and current.");
        result.stage = stageNativeModelVariant(accepted);
        if ( afterStage ) afterStage();
    });
}

void ModelStudioDialog::Impl::previewKey()
{
    if ( job != Job::None || sealed || !view.displayed ) return;
    const auto snapshot = *view.displayed;
    const auto name = keyPath->text();
    if ( name.isEmpty() ) { status->setText(tr("Choose an associated KEY file first.")); return; }
    launch(Job::Key, [snapshot, name](StudioWorker&, StudioResult& result)
    {
        QFile input(name);
        if ( !input.open(QIODevice::ReadOnly) ) throw std::runtime_error(input.errorString().toStdString());
        constexpr qint64 maximum = 16 * 1024 * 1024;
        const auto bytes = input.read(maximum + 1);
        if ( input.error() != QFile::NoError || !input.atEnd() || bytes.size() > maximum )
            throw std::runtime_error("The KEY could not be read within the preview byte limit.");
        std::istringstream stream(bytes.toStdString());
        result.key = assets::loadFromKEY(stream);
        result.keyDocument = snapshot;
        result.keyIdentity = QFileInfo(name).filesystemAbsoluteFilePath();
    });
}

ModelStudioDialog::ModelStudioDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}

ModelStudioDialog::~ModelStudioDialog() = default;

ModelStudioDialog::State ModelStudioDialog::state() const
{
    return impl_->view;
}

void ModelStudioDialog::setWorkHooksForTests(WorkHooksForTests hooks)
{
    impl_->hooks = std::move(hooks);
}

void ModelStudioDialog::reject()
{
    impl_->requestClose();
}

void ModelStudioDialog::closeEvent(QCloseEvent* event)
{
    if ( impl_->requestClose() ) event->accept(); else event->ignore();
}

} // namespace sed::ui::resources
