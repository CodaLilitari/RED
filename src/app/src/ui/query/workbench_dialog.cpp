#include "ui/query/workbench_dialog.hpp"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace sed::ui::query
{
namespace
{

QString failureText(const level::WorkbenchFailure& failure)
{
    return QString::fromStdString(failure.code + ": " + failure.message);
}

qulonglong unsignedMask(const QString& text, bool& valid)
{
    const auto value = text.trimmed();
    return value.toULongLong(&valid, value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10);
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
    resize(760, 680);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs);

    const auto addNumber = [this](QFormLayout* form, const char* name, const QString& label,
        double value, double minimum = -level::kMaxWorkbenchCoordinate,
        double maximum = level::kMaxWorkbenchCoordinate)
    {
        auto* spin = new QDoubleSpinBox;
        spin->setObjectName(QString::fromLatin1(name));
        spin->setDecimals(9);
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        numbers_.emplace(name, spin);
        form->addRow(label, spin);
        return spin;
    };
    const auto addVector = [&](QFormLayout* form, const char* prefix, const QString& label,
        const TVector value)
    {
        const double values[]{value.x, value.y, value.z};
        auto* row = new QWidget;
        auto* components = new QHBoxLayout(row);
        components->setContentsMargins(0, 0, 0, 0);
        for ( int i = 0; i < 3; ++i )
        {
            const std::string name = std::string(prefix) + "XYZ"[i];
            auto* spin = new QDoubleSpinBox(row);
            spin->setObjectName(QString::fromStdString(name));
            spin->setDecimals(9);
            spin->setRange(-level::kMaxWorkbenchCoordinate, level::kMaxWorkbenchCoordinate);
            spin->setValue(values[i]);
            spin->setAccessibleName(label + QLatin1Char(' ') + QLatin1Char("XYZ"[i]));
            numbers_.emplace(name, spin);
            auto* axisLabel = new QLabel(QString(QLatin1Char("XYZ"[i])), row);
            axisLabel->setBuddy(spin);
            components->addWidget(axisLabel);
            components->addWidget(spin);
        }
        form->addRow(label, row);
    };
    const auto scrollTab = [tabs](QWidget* page, const QString& label)
    {
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        tabs->addTab(scroll, label);
    };

    // Query tab: exact filters over matching rows, original indices kept.
    auto* queryPage = new QWidget(tabs);
    auto* queryLayout = new QVBoxLayout(queryPage);
    collectionCombo_ = new QComboBox(queryPage);
    collectionCombo_->setObjectName(QStringLiteral("workbenchCollection"));
    collectionCombo_->addItem(QStringLiteral("Things"),
        static_cast<int>(level::WorkbenchCollection::things));
    collectionCombo_->addItem(QStringLiteral("Sectors"),
        static_cast<int>(level::WorkbenchCollection::sectors));
    collectionCombo_->addItem(QStringLiteral("Lights"),
        static_cast<int>(level::WorkbenchCollection::lights));
    collectionCombo_->addItem(QStringLiteral("Surfaces in sector"),
        static_cast<int>(level::WorkbenchCollection::surfaces));
    queryLayout->addWidget(collectionCombo_);
    searchEdit_ = new QLineEdit(queryPage);
    searchEdit_->setPlaceholderText(
        tr("Substring search (things: name or template)"));
    queryLayout->addWidget(searchEdit_);
    auto* filterForm = new QFormLayout;
    querySector_ = new QSpinBox(queryPage);
    querySector_->setObjectName(QStringLiteral("workbenchQuerySector"));
    querySector_->setRange(0, std::numeric_limits<int>::max());
    filterForm->addRow(tr("Surface sector index"), querySector_);
    queryLimit_ = new QSpinBox(queryPage);
    queryLimit_->setObjectName(QStringLiteral("workbenchQueryLimit"));
    queryLimit_->setRange(1, 200);
    queryLimit_->setValue(50);
    filterForm->addRow(tr("Rows per page"), queryLimit_);
    const auto addFilter = [&](const char* key, const char* objectName, const QString& label)
    {
        auto* enabled = new QCheckBox(label, queryPage);
        auto* value = new QLineEdit(queryPage);
        value->setObjectName(QString::fromLatin1(objectName));
        enabled->setObjectName(value->objectName() + QStringLiteral("Enabled"));
        queryFields_.emplace(key, value);
        queryEnabled_.emplace(key, enabled);
        filterForm->addRow(enabled, value);
        connect(value, &QLineEdit::textChanged, this, [enabled](const QString& text)
        {
            if ( !text.isEmpty() )
            {
                enabled->setChecked(true);
            }
        });
    };
    addFilter("name", "workbenchFilterName", tr("Exact name"));
    addFilter("template", "workbenchFilterTemplate", tr("Exact template"));
    addFilter("layer", "workbenchFilterLayer", tr("Exact layer"));
    addFilter("material", "workbenchFilterMaterial", tr("Exact material index"));
    for ( const char* family : {"flags", "surfFlags", "faceFlags", "adjoinFlags"} )
    {
        for ( const char* predicate : {"any", "all", "none"} )
        {
            const std::string key = std::string(family) + "." + predicate;
            const std::string objectName = "workbenchFilter_" + key;
            addFilter(key.c_str(), objectName.c_str(),
                QString::fromStdString(key) + tr(" (decimal or 0x hex)"));
        }
    }
    auto* filterBox = new QGroupBox(tr("Exact predicates (checked fields combine with AND)"), queryPage);
    filterBox->setLayout(filterForm);
    queryLayout->addWidget(filterBox);
    auto* runQueryButton = new QPushButton(tr("Run query"), queryPage);
    runQueryButton->setObjectName(QStringLiteral("workbenchQueryRun"));
    queryLayout->addWidget(runQueryButton);
    queryOut_ = new QPlainTextEdit(queryPage);
    queryOut_->setReadOnly(true);
    queryOut_->setObjectName(QStringLiteral("workbenchQueryOutput"));
    queryOut_->setMinimumHeight(150);
    queryLayout->addWidget(queryOut_);
    queryStatus_ = new QLabel(queryPage);
    queryLayout->addWidget(queryStatus_);
    auto* navigation = new QHBoxLayout;
    queryPrevious_ = new QPushButton(tr("Previous page"), queryPage);
    queryPrevious_->setObjectName(QStringLiteral("workbenchQueryPrevious"));
    queryNext_ = new QPushButton(tr("Next page"), queryPage);
    queryNext_->setObjectName(QStringLiteral("workbenchQueryNext"));
    queryPrevious_->setEnabled(false);
    queryNext_->setEnabled(false);
    navigation->addWidget(queryPrevious_);
    navigation->addWidget(queryNext_);
    queryLayout->addLayout(navigation);
    scrollTab(queryPage, tr("Query"));
    connect(runQueryButton, &QPushButton::clicked,
        this, [this]() { runQuery(); });
    connect(queryPrevious_, &QPushButton::clicked, this, [this]()
    {
        const auto limit = static_cast<std::size_t>(queryLimit_->value());
        queryOffset_ = queryOffset_ > limit ? queryOffset_ - limit : 0;
        runQuery(false);
    });
    connect(queryNext_, &QPushButton::clicked, this, [this]()
    {
        if ( queryNextOffset_ )
        {
            queryOffset_ = *queryNextOffset_;
            runQuery(false);
        }
    });
    const auto invalidateQuery = [this]()
    {
        this->invalidateQuery();
    };
    connect(collectionCombo_, &QComboBox::currentIndexChanged, this, [this, invalidateQuery]()
    {
        updateQueryFields();
        invalidateQuery();
    });
    connect(searchEdit_, &QLineEdit::textChanged, this, invalidateQuery);
    connect(queryLimit_, &QSpinBox::valueChanged, this, invalidateQuery);
    connect(querySector_, &QSpinBox::valueChanged, this, invalidateQuery);
    for ( const auto& [key, field] : queryFields_ )
    {
        connect(field, &QLineEdit::textChanged, this, invalidateQuery);
        connect(queryEnabled_.at(key), &QCheckBox::toggled, this, invalidateQuery);
    }
    updateQueryFields();

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
    directionCombo_->setObjectName(QStringLiteral("workbenchUvProjection"));
    directionCombo_->addItem(QStringLiteral("Cylindrical"), -1);
    directionCombo_->addItem(QStringLiteral("Spherical"), -2);
    uvForm->addRow(tr("Projection direction"), directionCombo_);
    densitySpin_ = new QDoubleSpinBox(uvPage);
    densitySpin_->setRange(0.0, level::kMaxWorkbenchCoordinate);
    densitySpin_->setValue(16.0);
    uvForm->addRow(tr("Texels per unit"), densitySpin_);
    densitySpin_->setObjectName(QStringLiteral("workbenchUvDensity"));
    densitySpin_->setDecimals(9);
    addVector(uvForm, "workbenchUvOrigin", tr("Origin / center"), {});
    addVector(uvForm, "workbenchUvAxis", tr("Cylinder axis / sphere pole"), {0.0, 0.0, 1.0});
    auto* runUvButton = new QPushButton(tr("Project selected surfaces"), uvPage);
    uvForm->addRow(runUvButton);
    uvStatus_ = new QLabel(uvPage);
    uvStatus_->setWordWrap(true);
    uvForm->addRow(uvStatus_);
    runUvButton->setObjectName(QStringLiteral("workbenchProjectUv"));
    scrollTab(uvPage, tr("Batch UV"));
    connect(runUvButton, &QPushButton::clicked,
        this, [this]() { runProjectUv(); });

    // Arrays use the same admitted layouts and preparation as the agent.
    auto* arrayPage = new QWidget(tabs);
    auto* arrayForm = new QFormLayout(arrayPage);
    templateEdit_ = new QLineEdit(arrayPage);
    arrayForm->addRow(tr("Thing template"), templateEdit_);
    prefixEdit_ = new QLineEdit(QStringLiteral("array"), arrayPage);
    arrayForm->addRow(tr("Name prefix"), prefixEdit_);
    countSpin_ = new QSpinBox(arrayPage);
    countSpin_->setObjectName(QStringLiteral("workbenchArrayCount"));
    countSpin_->setRange(1, 256);
    countSpin_->setValue(4);
    arrayForm->addRow(tr("Count (max 256)"), countSpin_);
    shapeCombo_ = new QComboBox(arrayPage);
    shapeCombo_->setObjectName(QStringLiteral("workbenchArrayShape"));
    shapeCombo_->addItem(tr("Linear"), static_cast<int>(level::PlacementLayoutKind::linear));
    shapeCombo_->addItem(tr("Grid"), static_cast<int>(level::PlacementLayoutKind::grid));
    shapeCombo_->addItem(tr("Radial"), static_cast<int>(level::PlacementLayoutKind::radial));
    arrayForm->addRow(tr("Shape"), shapeCombo_);
    addVector(arrayForm, "workbenchArrayOrigin", tr("Origin"), {});
    auto* linearBox = new QGroupBox(tr("Linear step"), arrayPage);
    auto* linearForm = new QFormLayout(linearBox);
    addVector(linearForm, "workbenchArrayStep", tr("Step"), {1.0, 0.0, 0.0});
    arrayForm->addRow(linearBox);
    auto* gridBox = new QGroupBox(tr("Grid (U varies fastest; max 256 total)"), arrayPage);
    auto* gridForm = new QFormLayout(gridBox);
    countUSpin_ = new QSpinBox(gridBox);
    countVSpin_ = new QSpinBox(gridBox);
    countUSpin_->setObjectName(QStringLiteral("workbenchArrayCountU"));
    countVSpin_->setObjectName(QStringLiteral("workbenchArrayCountV"));
    for ( auto* spin : {countUSpin_, countVSpin_} )
    {
        spin->setRange(1, 256);
        spin->setValue(2);
    }
    gridForm->addRow(tr("Count U"), countUSpin_);
    gridForm->addRow(tr("Count V"), countVSpin_);
    addVector(gridForm, "workbenchArrayStepU", tr("Step U"), {1.0, 0.0, 0.0});
    addVector(gridForm, "workbenchArrayStepV", tr("Step V"), {0.0, 1.0, 0.0});
    arrayForm->addRow(gridBox);
    auto* radialBox = new QGroupBox(tr("Radial (end angle excluded; fixed output orientation)"), arrayPage);
    auto* radialForm = new QFormLayout(radialBox);
    addVector(radialForm, "workbenchArrayAxis", tr("Axis"), {0.0, 0.0, 1.0});
    addNumber(radialForm, "workbenchArrayRadius", tr("Radius"), 1.0, 0.0);
    addNumber(radialForm, "workbenchArrayStart", tr("Start angle (degrees)"), 0.0);
    addNumber(radialForm, "workbenchArraySweep", tr("Sweep (degrees)"), 360.0);
    arrayForm->addRow(radialBox);
    const auto updateShape = [this, linearBox, gridBox, radialBox]()
    {
        const auto kind = static_cast<level::PlacementLayoutKind>(shapeCombo_->currentData().toInt());
        linearBox->setVisible(kind == level::PlacementLayoutKind::linear);
        gridBox->setVisible(kind == level::PlacementLayoutKind::grid);
        radialBox->setVisible(kind == level::PlacementLayoutKind::radial);
        countSpin_->setEnabled(kind != level::PlacementLayoutKind::grid);
    };
    connect(shapeCombo_, &QComboBox::currentIndexChanged, this, updateShape);
    updateShape();
    addVector(arrayForm, "workbenchThingOrientation", tr("Thing orientation (degrees)"), {});
    layerSpin_ = new QSpinBox(arrayPage);
    layerSpin_->setRange(0, std::numeric_limits<int>::max());
    layerSpin_->setObjectName(QStringLiteral("workbenchArrayLayer"));
    arrayForm->addRow(tr("Layer index"), layerSpin_);
    explicitSector_ = new QCheckBox(tr("Pin Things to sector (for overlapping geometry)"), arrayPage);
    sectorSpin_ = new QSpinBox(arrayPage);
    sectorSpin_->setRange(0, std::numeric_limits<int>::max());
    sectorSpin_->setEnabled(false);
    connect(explicitSector_, &QCheckBox::toggled, sectorSpin_, &QSpinBox::setEnabled);
    arrayForm->addRow(explicitSector_, sectorSpin_);
    addNumber(arrayForm, "workbenchLightIntensity", tr("Light white intensity"), 10.0, 0.0, 1000.0);
    lightRgbFollowsWhite_ = new QCheckBox(tr("RGB follows white (MotS)"), arrayPage);
    lightRgbFollowsWhite_->setObjectName(QStringLiteral("workbenchLightRgbFollowsWhite"));
    lightRgbFollowsWhite_->setChecked(true);
    lightRgbFollowsWhite_->setEnabled(level_.kind == ProjectType::MOTS);
    arrayForm->addRow(lightRgbFollowsWhite_);
    addNumber(arrayForm, "workbenchLightRgbIntensity", tr("Independent RGB intensity"), 10.0, 0.0, 1000.0);
    auto* rgbIntensity = numbers_.at("workbenchLightRgbIntensity");
    rgbIntensity->setEnabled(false);
    connect(lightRgbFollowsWhite_, &QCheckBox::toggled, this, [this, rgbIntensity](const bool inherit)
    {
        rgbIntensity->setEnabled(!inherit && level_.kind == ProjectType::MOTS);
    });
    addNumber(arrayForm, "workbenchLightRange", tr("Light range"), 2.0, 0.0001, 100000.0);
    for ( const char channel : std::string("RGB") )
    {
        const std::string name = std::string("workbenchLightColor") + channel;
        addNumber(arrayForm, name.c_str(), tr("Light color ") + QLatin1Char(channel), 1.0, 0.0, 1.0);
    }
    lightFlags_ = new QLineEdit(QStringLiteral("0"), arrayPage);
    arrayForm->addRow(tr("Light flags (decimal or 0x hex)"), lightFlags_);
    auto* preview = new QPushButton(tr("Preview positions"), arrayPage);
    preview->setObjectName(QStringLiteral("workbenchPreviewPositions"));
    arrayForm->addRow(preview);
    arrayPreview_ = new QPlainTextEdit(arrayPage);
    arrayPreview_->setObjectName(QStringLiteral("workbenchArrayPreview"));
    arrayPreview_->setReadOnly(true);
    arrayPreview_->setMaximumHeight(160);
    arrayForm->addRow(arrayPreview_);
    connect(preview, &QPushButton::clicked, this, &WorkbenchDialog::previewArray);
    auto* runThingsButton = new QPushButton(tr("Create thing array"), arrayPage);
    auto* runLightsButton = new QPushButton(tr("Create light array"), arrayPage);
    runThingsButton->setObjectName(QStringLiteral("workbenchCreateThings"));
    runLightsButton->setObjectName(QStringLiteral("workbenchCreateLights"));
    arrayForm->addRow(runThingsButton);
    arrayForm->addRow(runLightsButton);
    arrayStatus_ = new QLabel(arrayPage);
    arrayStatus_->setWordWrap(true);
    arrayForm->addRow(arrayStatus_);
    scrollTab(arrayPage, tr("Arrays"));
    connect(runThingsButton, &QPushButton::clicked,
        this, [this]() { runThingArray(); });
    connect(runLightsButton, &QPushButton::clicked,
        this, [this]() { runLightArray(); });
    const auto invalidateArrayPreview = [this]()
    {
        arrayPreview_->clear();
        arrayStatus_->clear();
    };
    connect(shapeCombo_, &QComboBox::currentIndexChanged, this, invalidateArrayPreview);
    for ( auto* count : {countSpin_, countUSpin_, countVSpin_} )
    {
        connect(count, &QSpinBox::valueChanged, this, invalidateArrayPreview);
    }
    for ( const auto& [name, spin] : numbers_ )
    {
        if ( name.starts_with("workbenchArray") )
        {
            connect(spin, &QDoubleSpinBox::valueChanged, this, invalidateArrayPreview);
        }
    }

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

void WorkbenchDialog::invalidateQuery()
{
    queryOffset_ = 0;
    queryNextOffset_.reset();
    queryNext_->setEnabled(false);
    queryPrevious_->setEnabled(false);
    queryOut_->clear();
    queryStatus_->setText(tr("Query changed. Run it to refresh the results."));
}

void WorkbenchDialog::updateQueryFields()
{
    const auto collection = static_cast<level::WorkbenchCollection>(collectionCombo_->currentData().toInt());
    const bool things = collection == level::WorkbenchCollection::things;
    const bool surfaces = collection == level::WorkbenchCollection::surfaces;
    searchEdit_->setEnabled(things);
    querySector_->setEnabled(surfaces);
    for ( const auto& [key, field] : queryFields_ )
    {
        const bool supported = key == "layer" || (things && (key == "name" || key == "template"))
            || (surfaces && (key == "material" || key.starts_with("surfFlags.")
                || key.starts_with("faceFlags.") || key.starts_with("adjoinFlags.")))
            || (!surfaces && !things && key.starts_with("flags."));
        field->setEnabled(supported);
        queryEnabled_.at(key)->setEnabled(supported);
        field->setVisible(supported);
        queryEnabled_.at(key)->setVisible(supported);
    }
}

void WorkbenchDialog::runQuery(const bool resetPage)
{
    if ( resetPage )
    {
        queryOffset_ = 0;
    }
    const auto collection = static_cast<level::WorkbenchCollection>(
        collectionCombo_->currentData().toInt());
    level::ExactQueryFilter filter;
    bool valid = true;
    for ( const auto& [key, field] : queryFields_ )
    {
        if ( !field->isEnabled() || !queryEnabled_.at(key)->isChecked() )
        {
            continue;
        }
        const auto value = field->text();
        if ( key == "name" )
        {
            filter.name = value.toStdString();
        }
        else if ( key == "template" )
        {
            filter.templateName = value.toStdString();
        }
        else if ( key == "layer" || key == "material" )
        {
            bool ok = false;
            const int number = value.toInt(&ok, 10);
            valid = valid && ok;
            (key == "layer" ? filter.layer : filter.materialIndex) = number;
        }
        else
        {
            bool ok = false;
            const auto number = unsignedMask(value, ok);
            valid = valid && ok && number <= std::numeric_limits<std::uint32_t>::max();
            auto* predicate = key.starts_with("surfFlags.") ? &filter.surfFlags
                : key.starts_with("faceFlags.") ? &filter.faceFlags
                : key.starts_with("adjoinFlags.") ? &filter.adjoinFlags : &filter.flags;
            if ( !predicate->has_value() )
            {
                predicate->emplace();
            }
            auto& mask = key.ends_with(".any") ? predicate->value().any
                : key.ends_with(".all") ? predicate->value().all : predicate->value().none;
            mask = static_cast<std::uint32_t>(number);
        }
    }
    queryNext_->setEnabled(false);
    queryPrevious_->setEnabled(false);
    queryNextOffset_.reset();
    queryOut_->clear();
    if ( !valid )
    {
        queryStatus_->setText(tr("Invalid predicate: indices need integers; flag masks need unsigned 32-bit values."));
        return;
    }
    const level::QueryResult result = level::queryWorkbench(
        level_, collection,
        collection == level::WorkbenchCollection::surfaces ? std::optional<std::int32_t>(querySector_->value()) : std::nullopt,
        filter.empty() ? std::nullopt : std::optional<level::ExactQueryFilter>(filter),
        searchEdit_->isEnabled() ? searchEdit_->text().toStdString() : std::string{},
        {queryOffset_, static_cast<std::size_t>(queryLimit_->value())});
    if ( !result.ok() )
    {
        showRefusal(result.failure, queryStatus_);
        return;
    }
    QString body = tr("Total matches: %1\n")
        .arg(static_cast<qulonglong>(result.totalMatches));
    for ( const std::int32_t index : result.indices )
    {
        body += QString::number(index);
        body += QLatin1Char('\n');
    }
    queryOut_->setPlainText(body);
    queryNextOffset_ = result.nextOffset;
    queryNext_->setEnabled(queryNextOffset_.has_value());
    queryPrevious_->setEnabled(queryOffset_ > 0);
    queryStatus_->setText(tr("Showing %1 rows from match offset %2. Indices refer to the original collection.")
        .arg(result.indices.size()).arg(queryOffset_));
}

void WorkbenchDialog::runProjectUv()
{
    if (selectedSurfaces_.empty())
    {
        uvStatus_->setText(tr("No surfaces are selected."));
        return;
    }
    const int directionData = directionCombo_->currentData().toInt();
    const level::PreparedBatchUv prepared = directionData == -1
        ? level::prepareCylindricalSurfaceUv(level_, selectedSurfaces_,
              vectorValue("workbenchUvOrigin"), vectorValue("workbenchUvAxis"), densitySpin_->value())
        : directionData == -2
        ? level::prepareSphericalSurfaceUv(level_, selectedSurfaces_,
              vectorValue("workbenchUvOrigin"), vectorValue("workbenchUvAxis"), densitySpin_->value())
        : level::prepareProjectSurfaceUv(level_, selectedSurfaces_,
              static_cast<level::UvProjectionDirection>(directionData),
              vectorValue("workbenchUvOrigin"), densitySpin_->value());
    if (!prepared.ok())
    {
        showRefusal(prepared.failure, uvStatus_);
        return;
    }
    if (!handlers_.applyBatchUv || !handlers_.applyBatchUv(prepared))
    {
        uvStatus_->setText(tr("The UV edit was refused. Check Preview mode, hidden/locked source layers, "
            "and changed document/resource context or source data, then reopen Workbench."));
        return;
    }
    uvStatus_->setText(tr("Batch UV applied as one undo step."));
}

TVector WorkbenchDialog::vectorValue(const char* prefix) const
{
    return {numbers_.at(std::string(prefix) + "X")->value(),
        numbers_.at(std::string(prefix) + "Y")->value(),
        numbers_.at(std::string(prefix) + "Z")->value()};
}

level::PlacementPositions WorkbenchDialog::arrayPositions() const
{
    level::PlacementLayout layout{};
    layout.kind = static_cast<level::PlacementLayoutKind>(shapeCombo_->currentData().toInt());
    layout.origin = vectorValue("workbenchArrayOrigin");
    layout.count = countSpin_->value();
    layout.step = vectorValue("workbenchArrayStep");
    layout.countU = countUSpin_->value();
    layout.countV = countVSpin_->value();
    layout.stepU = vectorValue("workbenchArrayStepU");
    layout.stepV = vectorValue("workbenchArrayStepV");
    layout.axis = vectorValue("workbenchArrayAxis");
    layout.radius = numbers_.at("workbenchArrayRadius")->value();
    layout.startDegrees = numbers_.at("workbenchArrayStart")->value();
    layout.sweepDegrees = numbers_.at("workbenchArraySweep")->value();
    return level::computePlacementPositions(layout);
}

void WorkbenchDialog::previewArray()
{
    const auto positions = arrayPositions();
    arrayPreview_->clear();
    if ( !positions.ok() )
    {
        showRefusal(positions.failure, arrayStatus_);
        return;
    }
    QString text = tr("%1 positions (placement validation runs when creating):\n").arg(positions.positions.size());
    for ( const auto& point : positions.positions )
    {
        text += QStringLiteral("%1, %2, %3\n").arg(point.x, 0, 'g', 12)
            .arg(point.y, 0, 'g', 12).arg(point.z, 0, 'g', 12);
    }
    arrayPreview_->setPlainText(text);
    arrayStatus_->clear();
}

void WorkbenchDialog::runThingArray()
{
    const auto positions = arrayPositions();
    if (!positions.ok())
    {
        showRefusal(positions.failure, arrayStatus_);
        return;
    }
    const level::PreparedThingArray prepared = level::prepareThingArray(
        level_, positions.positions,
        templateEdit_->text().toStdString(),
        prefixEdit_->text().toStdString(),
        vectorValue("workbenchThingOrientation"), layerSpin_->value(),
        explicitSector_->isChecked() ? std::optional<std::int32_t>(sectorSpin_->value()) : std::nullopt, 0,
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
        arrayStatus_->setText(tr("The array was refused. Check Preview mode, hidden/locked target layers, "
            "and changed document/resource context or source data, then reopen Workbench."));
        return;
    }
    arrayStatus_->setText(tr("Thing array applied as one undo step."));
    invalidateQuery();
}

void WorkbenchDialog::runLightArray()
{
    const auto positions = arrayPositions();
    if (!positions.ok())
    {
        showRefusal(positions.failure, arrayStatus_);
        return;
    }
    bool flagsValid = false;
    const auto flags = unsignedMask(lightFlags_->text(), flagsValid);
    if ( !flagsValid || flags > static_cast<qulonglong>(std::numeric_limits<std::int32_t>::max()) )
    {
        arrayStatus_->setText(tr("Light flags must be a decimal or hexadecimal value from 0 through 2147483647."));
        return;
    }
    const ColorF color{
        static_cast<float>(numbers_.at("workbenchLightColorR")->value()),
        static_cast<float>(numbers_.at("workbenchLightColorG")->value()),
        static_cast<float>(numbers_.at("workbenchLightColorB")->value()), 1.0f};
    const level::PreparedLightArray prepared = level::prepareLightArray(
        level_, positions.positions, color, numbers_.at("workbenchLightIntensity")->value(),
        numbers_.at("workbenchLightRange")->value(), static_cast<std::int32_t>(flags), layerSpin_->value(), 0,
        lightRgbFollowsWhite_->isChecked() ? std::nullopt
            : std::optional<double>{numbers_.at("workbenchLightRgbIntensity")->value()});
    if (!prepared.ok())
    {
        showRefusal(prepared.failure, arrayStatus_);
        return;
    }
    if (!handlers_.applyLightArray || !handlers_.applyLightArray(prepared))
    {
        arrayStatus_->setText(tr("The array was refused. Check Preview mode, hidden/locked target layers, "
            "and changed document/resource context or source data, then reopen Workbench."));
        return;
    }
    arrayStatus_->setText(tr("Light array applied as one undo step."));
    invalidateQuery();
}

} // namespace sed::ui::query
