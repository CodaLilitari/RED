#include "ui/query/workbench_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sed::ui::query
{
namespace
{

QString failureText(const level::WorkbenchFailure& failure)
{
    return QString::fromStdString(failure.code + ": " + failure.message);
}

} // namespace

WorkbenchDialog::WorkbenchDialog(
    const Level& level,
    std::vector<SurfaceRef> selectedSurfaces,
    level::CogSchemaResolver schemaResolver,
    Handlers handlers,
    QWidget* parent)
    : QDialog(parent)
    , level_(level)
    , selectedSurfaces_(std::move(selectedSurfaces))
    , schemaResolver_(std::move(schemaResolver))
    , handlers_(std::move(handlers))
{
    setWindowTitle(tr("Workbench"));
    resize(680, 520);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs);

    // Query tab: exact filters over matching rows, original indices kept.
    auto* queryPage = new QWidget(tabs);
    auto* queryLayout = new QVBoxLayout(queryPage);
    collectionCombo_ = new QComboBox(queryPage);
    collectionCombo_->addItem(QStringLiteral("Things"),
        static_cast<int>(level::WorkbenchCollection::things));
    collectionCombo_->addItem(QStringLiteral("Sectors"),
        static_cast<int>(level::WorkbenchCollection::sectors));
    collectionCombo_->addItem(QStringLiteral("Lights"),
        static_cast<int>(level::WorkbenchCollection::lights));
    queryLayout->addWidget(collectionCombo_);
    searchEdit_ = new QLineEdit(queryPage);
    searchEdit_->setPlaceholderText(
        tr("Substring search (things: name or template)"));
    queryLayout->addWidget(searchEdit_);
    auto* runQueryButton = new QPushButton(tr("Run query"), queryPage);
    queryLayout->addWidget(runQueryButton);
    queryOut_ = new QPlainTextEdit(queryPage);
    queryOut_->setReadOnly(true);
    queryLayout->addWidget(queryOut_);
    queryStatus_ = new QLabel(queryPage);
    queryLayout->addWidget(queryStatus_);
    tabs->addTab(queryPage, tr("Query"));
    connect(runQueryButton, &QPushButton::clicked,
        this, [this]() { runQuery(); });

    // Batch UV tab: one preflighted projection over the selection.
    auto* uvPage = new QWidget(tabs);
    auto* uvForm = new QFormLayout(uvPage);
    directionCombo_ = new QComboBox(uvPage);
    directionCombo_->addItem(QStringLiteral("+X"),
        static_cast<int>(level::UvProjectionDirection::positiveX));
    directionCombo_->addItem(QStringLiteral("-X"),
        static_cast<int>(level::UvProjectionDirection::negativeX));
    directionCombo_->addItem(QStringLiteral("+Y"),
        static_cast<int>(level::UvProjectionDirection::positiveY));
    directionCombo_->addItem(QStringLiteral("-Y"),
        static_cast<int>(level::UvProjectionDirection::negativeY));
    directionCombo_->addItem(QStringLiteral("+Z"),
        static_cast<int>(level::UvProjectionDirection::positiveZ));
    directionCombo_->addItem(QStringLiteral("-Z"),
        static_cast<int>(level::UvProjectionDirection::negativeZ));
    uvForm->addRow(tr("Projection direction"), directionCombo_);
    densitySpin_ = new QDoubleSpinBox(uvPage);
    densitySpin_->setRange(0.001, 100000.0);
    densitySpin_->setValue(16.0);
    uvForm->addRow(tr("Texels per unit"), densitySpin_);
    auto* runUvButton = new QPushButton(tr("Project selected surfaces"), uvPage);
    uvForm->addRow(runUvButton);
    uvStatus_ = new QLabel(uvPage);
    uvStatus_->setWordWrap(true);
    uvForm->addRow(uvStatus_);
    tabs->addTab(uvPage, tr("Batch UV"));
    connect(runUvButton, &QPushButton::clicked,
        this, [this]() { runProjectUv(); });

    // Arrays tab: deterministic linear arrays of new things or lights.
    auto* arrayPage = new QWidget(tabs);
    auto* arrayForm = new QFormLayout(arrayPage);
    templateEdit_ = new QLineEdit(arrayPage);
    arrayForm->addRow(tr("Thing template"), templateEdit_);
    prefixEdit_ = new QLineEdit(QStringLiteral("array"), arrayPage);
    arrayForm->addRow(tr("Name prefix"), prefixEdit_);
    countSpin_ = new QSpinBox(arrayPage);
    countSpin_->setRange(1, 256);
    countSpin_->setValue(4);
    arrayForm->addRow(tr("Count (max 256)"), countSpin_);
    stepXSpin_ = new QDoubleSpinBox(arrayPage);
    stepYSpin_ = new QDoubleSpinBox(arrayPage);
    stepZSpin_ = new QDoubleSpinBox(arrayPage);
    for (QDoubleSpinBox* spin : { stepXSpin_, stepYSpin_, stepZSpin_ })
    {
        spin->setRange(-100000.0, 100000.0);
    }
    stepXSpin_->setValue(1.0);
    arrayForm->addRow(tr("Step X"), stepXSpin_);
    arrayForm->addRow(tr("Step Y"), stepYSpin_);
    arrayForm->addRow(tr("Step Z"), stepZSpin_);
    auto* runThingsButton = new QPushButton(tr("Create thing array"), arrayPage);
    auto* runLightsButton = new QPushButton(tr("Create light array"), arrayPage);
    arrayForm->addRow(runThingsButton);
    arrayForm->addRow(runLightsButton);
    arrayStatus_ = new QLabel(arrayPage);
    arrayStatus_->setWordWrap(true);
    arrayForm->addRow(arrayStatus_);
    tabs->addTab(arrayPage, tr("Arrays"));
    connect(runThingsButton, &QPushButton::clicked,
        this, [this]() { runThingArray(); });
    connect(runLightsButton, &QPushButton::clicked,
        this, [this]() { runLightArray(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void WorkbenchDialog::showRefusal(
    const std::optional<level::WorkbenchFailure>& failure, QLabel* status)
{
    if (failure.has_value() && status)
    {
        status->setText(failureText(*failure));
    }
}

void WorkbenchDialog::runQuery()
{
    const auto collection = static_cast<level::WorkbenchCollection>(
        collectionCombo_->currentData().toInt());
    const level::QueryResult result = level::queryWorkbench(
        level_, collection, std::nullopt, std::nullopt,
        searchEdit_->text().toStdString(), {});
    if (!result.ok())
    {
        showRefusal(result.failure, queryStatus_);
        return;
    }
    QString body = tr("Total matches: %1\n")
        .arg(static_cast<qulonglong>(result.totalMatches));
    for (const std::int32_t index : result.indices)
    {
        body += QString::number(index);
        body += QLatin1Char('\n');
    }
    queryOut_->setPlainText(body);
    queryStatus_->clear();
}

void WorkbenchDialog::runProjectUv()
{
    if (selectedSurfaces_.empty())
    {
        uvStatus_->setText(tr("No surfaces are selected."));
        return;
    }
    const auto direction = static_cast<level::UvProjectionDirection>(
        directionCombo_->currentData().toInt());
    const level::PreparedBatchUv prepared = level::prepareProjectSurfaceUv(
        level_, selectedSurfaces_, direction, TVector{},
        densitySpin_->value());
    if (!prepared.ok())
    {
        showRefusal(prepared.failure, uvStatus_);
        return;
    }
    if (!handlers_.applyBatchUv || !handlers_.applyBatchUv(prepared))
    {
        uvStatus_->setText(tr("The prepared UV edit is stale; run it again."));
        return;
    }
    uvStatus_->setText(tr("Batch UV applied as one undo step."));
}

void WorkbenchDialog::runThingArray()
{
    level::PlacementLayout layout{};
    layout.kind = level::PlacementLayoutKind::linear;
    layout.count = countSpin_->value();
    layout.step = TVector{ stepXSpin_->value(), stepYSpin_->value(),
        stepZSpin_->value() };
    const level::PlacementPositions positions =
        level::computePlacementPositions(layout);
    if (!positions.ok())
    {
        showRefusal(positions.failure, arrayStatus_);
        return;
    }
    const level::PreparedThingArray prepared = level::prepareThingArray(
        level_, positions.positions,
        templateEdit_->text().toStdString(),
        prefixEdit_->text().toStdString(),
        TVector{}, 0, std::nullopt, 0,
        schemaResolver_ ? std::optional<level::CogSchemaResolver>(
                              schemaResolver_)
                          : std::nullopt);
    if (!prepared.ok())
    {
        showRefusal(prepared.failure, arrayStatus_);
        return;
    }
    if (!handlers_.applyThingArray || !handlers_.applyThingArray(prepared))
    {
        arrayStatus_->setText(tr("The prepared array is stale; run it again."));
        return;
    }
    arrayStatus_->setText(tr("Thing array applied as one undo step."));
}

void WorkbenchDialog::runLightArray()
{
    level::PlacementLayout layout{};
    layout.kind = level::PlacementLayoutKind::linear;
    layout.count = countSpin_->value();
    layout.step = TVector{ stepXSpin_->value(), stepYSpin_->value(),
        stepZSpin_->value() };
    const level::PlacementPositions positions =
        level::computePlacementPositions(layout);
    if (!positions.ok())
    {
        showRefusal(positions.failure, arrayStatus_);
        return;
    }
    const level::PreparedLightArray prepared = level::prepareLightArray(
        level_, positions.positions, ColorWhite, 10.0, 2.0, 0, 0, 0);
    if (!prepared.ok())
    {
        showRefusal(prepared.failure, arrayStatus_);
        return;
    }
    if (!handlers_.applyLightArray || !handlers_.applyLightArray(prepared))
    {
        arrayStatus_->setText(tr("The prepared array is stale; run it again."));
        return;
    }
    arrayStatus_->setText(tr("Light array applied as one undo step."));
}

} // namespace sed::ui::query
