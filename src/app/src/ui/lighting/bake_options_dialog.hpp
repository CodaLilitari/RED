#pragma once

#include <QColor>
#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QSettings;
class QToolButton;

namespace sed::ui::lighting
{

// The bake profile settings the light bake reads from the [lighting] group of
// red.ini: fully lit material emission, one diffuse bounce, uniform sky light,
// and the two Light Baker options. A zero bounce strength or sky intensity
// turns that pass off.
struct BakeOptions
{
    double emissiveIntensity{ 1.0 };
    double emissiveRange{ 10.0 };
    double bounceStrength{ 0.0 };
    double bounceRange{ 10.0 };
    double skyIntensity{ 0.0 };
    double skyRange{ 10.0 };
    QColor skyColor{ Qt::white };
    bool angleShading{ false };
    bool waterAbsorption{ false };

    friend bool operator==(const BakeOptions&, const BakeOptions&) = default;
};

[[nodiscard]] BakeOptions defaultBakeOptions();
// Reads the options as the bake does, with the same keys and defaults.
[[nodiscard]] BakeOptions readBakeOptions(const QSettings& settings);
void writeBakeOptions(QSettings& settings, const BakeOptions& options);

// Bake Options, opened from the light bake strip. It edits a copy; the caller
// writes options() back when the dialog is accepted. The next bake uses them.
class BakeOptionsDialog final : public QDialog
{
public:
    explicit BakeOptionsDialog(const BakeOptions& options, QWidget* parent = nullptr);

    [[nodiscard]] BakeOptions options() const;
    void setOptions(const BakeOptions& options);

private:
    void setSkyColor(const QColor& color);

    QDoubleSpinBox* emissiveIntensity_{};
    QDoubleSpinBox* emissiveRange_{};
    QDoubleSpinBox* bounceStrength_{};
    QDoubleSpinBox* bounceRange_{};
    QDoubleSpinBox* skyIntensity_{};
    QDoubleSpinBox* skyRange_{};
    QToolButton* skyColorButton_{};
    QColor skyColor_{ Qt::white };
    QCheckBox* angleShading_{};
    QCheckBox* waterAbsorption_{};
};

} // namespace sed::ui::lighting
