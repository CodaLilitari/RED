#include "ui/lighting/bake_options_dialog.hpp"

#include "sed/level/emissive_resolution.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace sed::ui::lighting
{
namespace
{

// The keys and defaults light_bake.cpp reads.
constexpr auto kEmissiveIntensity = "lighting/emissive_intensity";
constexpr auto kEmissiveRange = "lighting/emissive_range";
constexpr auto kBounceStrength = "lighting/bounce_strength";
constexpr auto kBounceRange = "lighting/bounce_range";
constexpr auto kSkyIntensity = "lighting/sky_intensity";
constexpr auto kSkyRange = "lighting/sky_range";
constexpr auto kSkyColor = "lighting/sky_color";
constexpr auto kAngleShading = "lighting/angle_shading";
constexpr auto kWaterAbsorption = "lighting/water_absorption";

double finiteOr(const QVariant& value, const double fallback)
{
    bool ok = false;
    const double result = value.toDouble(&ok);
    return ok && std::isfinite(result) ? result : fallback;
}

QDoubleSpinBox* spinBox(const QString& name, const double minimum, const double maximum, const double step,
    const QString& suffix, const QString& tip, QWidget* parent)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    box->setDecimals(3);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setToolTip(tip);
    box->setAccessibleDescription(tip);
    box->setKeyboardTracking(false);
    return box;
}

QGroupBox* group(const QString& title, QWidget* parent, QFormLayout*& form)
{
    auto* box = new QGroupBox(title, parent);
    form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    return box;
}

} // namespace

BakeOptions defaultBakeOptions()
{
    BakeOptions options{};
    options.emissiveIntensity = level::kDefaultEmissiveIntensity;
    options.emissiveRange = level::kDefaultEmissiveRange;
    options.bounceRange = level::kDefaultEmissiveRange;
    options.skyRange = level::kDefaultEmissiveRange;
    return options;
}

BakeOptions readBakeOptions(const QSettings& settings)
{
    const BakeOptions d = defaultBakeOptions();
    BakeOptions options{};
    options.emissiveIntensity = finiteOr(settings.value(kEmissiveIntensity, d.emissiveIntensity), d.emissiveIntensity);
    options.emissiveRange = finiteOr(settings.value(kEmissiveRange, d.emissiveRange), d.emissiveRange);
    options.bounceStrength = finiteOr(settings.value(kBounceStrength, d.bounceStrength), d.bounceStrength);
    options.bounceRange = finiteOr(settings.value(kBounceRange, d.bounceRange), d.bounceRange);
    options.skyIntensity = finiteOr(settings.value(kSkyIntensity, d.skyIntensity), d.skyIntensity);
    options.skyRange = finiteOr(settings.value(kSkyRange, d.skyRange), d.skyRange);
    const QColor sky(settings.value(kSkyColor, QStringLiteral("#ffffff")).toString());
    options.skyColor = sky.isValid() ? sky : d.skyColor;
    options.angleShading = settings.value(kAngleShading, d.angleShading).toBool();
    options.waterAbsorption = settings.value(kWaterAbsorption, d.waterAbsorption).toBool();
    return options;
}

void writeBakeOptions(QSettings& settings, const BakeOptions& options)
{
    settings.setValue(kEmissiveIntensity, options.emissiveIntensity);
    settings.setValue(kEmissiveRange, options.emissiveRange);
    settings.setValue(kBounceStrength, options.bounceStrength);
    settings.setValue(kBounceRange, options.bounceRange);
    settings.setValue(kSkyIntensity, options.skyIntensity);
    settings.setValue(kSkyRange, options.skyRange);
    // The bake reads "#rrggbb" by position.
    settings.setValue(kSkyColor, options.skyColor.name(QColor::HexRgb));
    settings.setValue(kAngleShading, options.angleShading);
    settings.setValue(kWaterAbsorption, options.waterAbsorption);
    settings.sync();
}

BakeOptionsDialog::BakeOptionsDialog(const BakeOptions& options, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("bakeOptionsDialog"));
    setWindowTitle(tr("Bake Options"));
    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("These settings apply to every baked-light recalculation, manual or automatic, and are kept in red.ini. "
           "Changing them does not recalculate; use Recalculate afterwards."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QFormLayout* form = nullptr;
    QGroupBox* emission = group(tr("Fully lit materials"), this, form);
    emissiveIntensity_ = spinBox(QStringLiteral("bakeOptions.emissiveIntensity"), 0.001, 1000.0, 0.1, {},
        tr("How strongly the material of a fully lit surface lights the corners around it."), emission);
    emissiveRange_ = spinBox(QStringLiteral("bakeOptions.emissiveRange"), 0.001, 100000.0, 1.0, tr(" JKU"),
        tr("How far a fully lit surface's light reaches."), emission);
    form->addRow(tr("Intensity"), emissiveIntensity_);
    form->addRow(tr("Range"), emissiveRange_);
    layout->addWidget(emission);

    QGroupBox* bounce = group(tr("Bounce"), this, form);
    bounceStrength_ = spinBox(QStringLiteral("bakeOptions.bounceStrength"), 0.0, 1000.0, 0.1, {},
        tr("One diffuse bounce: every surface re-emits its average baked color. 0 turns the bounce off."), bounce);
    bounceStrength_->setSpecialValueText(tr("Off"));
    bounceRange_ = spinBox(QStringLiteral("bakeOptions.bounceRange"), 0.001, 100000.0, 1.0, tr(" JKU"),
        tr("How far bounced light reaches."), bounce);
    form->addRow(tr("Strength"), bounceStrength_);
    form->addRow(tr("Range"), bounceRange_);
    layout->addWidget(bounce);

    QGroupBox* sky = group(tr("Sky light"), this, form);
    skyIntensity_ = spinBox(QStringLiteral("bakeOptions.skyIntensity"), 0.0, 1000.0, 0.1, {},
        tr("Uniform light from fully lit sky surfaces. 0 turns it off. A sun (light flag 0x02) is separate and "
           "needs none of this."),
        sky);
    skyIntensity_->setSpecialValueText(tr("Off"));
    skyRange_ = spinBox(QStringLiteral("bakeOptions.skyRange"), 0.001, 100000.0, 1.0, tr(" JKU"),
        tr("How far sky light reaches from a sky surface."), sky);
    skyColorButton_ = new QToolButton(sky);
    skyColorButton_->setObjectName(QStringLiteral("bakeOptions.skyColor"));
    skyColorButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    skyColorButton_->setToolTip(tr("The color of sky light."));
    skyColorButton_->setAccessibleName(tr("Sky light color"));
    connect(skyColorButton_, &QToolButton::clicked, this, [this]()
        {
            const QColor chosen = QColorDialog::getColor(skyColor_, this, tr("Sky light color"));
            if ( chosen.isValid() )
            {
                setSkyColor(chosen);
            }
        });
    form->addRow(tr("Intensity"), skyIntensity_);
    form->addRow(tr("Range"), skyRange_);
    form->addRow(tr("Color"), skyColorButton_);
    layout->addWidget(sky);

    QGroupBox* baker = group(tr("Light Baker options"), this, form);
    angleShading_ = new QCheckBox(tr("Angle shading"), baker);
    angleShading_->setObjectName(QStringLiteral("bakeOptions.angleShading"));
    angleShading_->setToolTip(
        tr("Shade point lights and fully lit surfaces by the angle their light strikes a surface at (N·L), as "
           "LadyEebs' JED Light Baker does."));
    waterAbsorption_ = new QCheckBox(tr("Water absorption"), baker);
    waterAbsorption_->setObjectName(QStringLiteral("bakeOptions.waterAbsorption"));
    waterAbsorption_->setToolTip(
        tr("Light that crosses underwater sectors takes on their tint, once per body of water, as in LadyEebs' JED "
           "Light Baker."));
    form->addRow(angleShading_);
    form->addRow(waterAbsorption_);
    layout->addWidget(baker);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    buttons->setObjectName(QStringLiteral("bakeOptions.buttons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
        [this]() { setOptions(defaultBakeOptions()); });
    layout->addWidget(buttons);

    setOptions(options);
}

BakeOptions BakeOptionsDialog::options() const
{
    BakeOptions result{};
    result.emissiveIntensity = emissiveIntensity_->value();
    result.emissiveRange = emissiveRange_->value();
    result.bounceStrength = bounceStrength_->value();
    result.bounceRange = bounceRange_->value();
    result.skyIntensity = skyIntensity_->value();
    result.skyRange = skyRange_->value();
    result.skyColor = skyColor_;
    result.angleShading = angleShading_->isChecked();
    result.waterAbsorption = waterAbsorption_->isChecked();
    return result;
}

void BakeOptionsDialog::setOptions(const BakeOptions& options)
{
    emissiveIntensity_->setValue(options.emissiveIntensity);
    emissiveRange_->setValue(options.emissiveRange);
    bounceStrength_->setValue(options.bounceStrength);
    bounceRange_->setValue(options.bounceRange);
    skyIntensity_->setValue(options.skyIntensity);
    skyRange_->setValue(options.skyRange);
    setSkyColor(options.skyColor);
    angleShading_->setChecked(options.angleShading);
    waterAbsorption_->setChecked(options.waterAbsorption);
}

void BakeOptionsDialog::setSkyColor(const QColor& color)
{
    skyColor_ = color.isValid() ? color : QColor(Qt::white);
    QPixmap swatch(16, 16);
    swatch.fill(skyColor_);
    skyColorButton_->setIcon(QIcon(swatch));
    skyColorButton_->setText(skyColor_.name(QColor::HexRgb));
}

} // namespace sed::ui::lighting
