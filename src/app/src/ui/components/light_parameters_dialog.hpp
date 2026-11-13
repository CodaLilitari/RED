#pragma once

#include "ui/components/live_color_popup.hpp"
#include "ui/components/scrub_float_edit.hpp"
#include "ui/components/ui_decimal_format.hpp"
#include "ui/components/viewport_icons.hpp"

#include <QCloseEvent>
#include <QCheckBox>
#include <QCursor>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace sed::ui
{

/**
 * Type-aware, live light editor presented as a transient viewport popup.
 *
 * There is deliberately no modal accept/cancel step: every palette movement,
 * scrub, or valid typed value updates the document immediately.  Qt::Popup
 * supplies the familiar color-picker behavior where Escape or a click outside
 * dismisses the editor without rolling the live edit back.
 *
 * IJIM RGB fields can exceed one.  They are presented as a normal color plus
 * a separate intensity, preserving the authored RGB as
 * `displayColor * intensity` without exposing three low-level channel boxes.
 */
class LightParametersDialog final : public QWidget
{
public:
    enum class Mode : std::uint8_t
    {
        baked,
        motsBaked,
        jkdf2Baked,
        ijimThing,
        classicThing,
        ijimHeadlight,
        classicHeadlight,
        ijimSector,
    };

    struct Values
    {
        double red{ 1.0 };
        double green{ 1.0 };
        double blue{ 1.0 };
        double intensity{ 1.0 };
        double range{};
        double minRange{};
        double maxRange{};
        double offsetX{};
        double offsetY{};
        double offsetZ{};
        std::optional<double> rgbIntensity{};
        std::optional<double> legacyRgbRange{};
        // Intent is separate from the displayed source's initial values:
        // unrelated edits must preserve each selected light's metadata.
        bool rgbIntensityEdited{};
        bool legacyRgbRangeEdited{};
        bool colorEdited{};
        bool intensityEdited{};
        bool rangeEdited{};

        bool operator==(const Values&) const = default;
    };

    using PreviewCallback = std::function<void(const Values&)>;
    using ClosedCallback = std::function<void()>;

    LightParametersDialog(
        QWidget* parent,
        QString title,
        const Mode mode,
        const Values& initial,
        PreviewCallback previewCallback = {},
        ClosedCallback closedCallback = {})
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
        , mode_(mode)
        , initial_(initial)
        , authoredRgbIntensity_(initial.rgbIntensity)
        , authoredLegacyRgbRange_(initial.legacyRgbRange)
        , previewCallback_(std::move(previewCallback))
        , closedCallback_(std::move(closedCallback))
    {
        setObjectName(QStringLiteral("lightParametersPopup"));
        setWindowTitle(title);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setStyleSheet(QStringLiteral(
            "QWidget#lightParametersPopup { background-color: #252525; border: 1px solid #51565d; border-radius: 6px; }"
            "QLabel#lightParametersTitle { color: #e6e9ed; font-weight: 600; padding: 0 1px 2px 1px; }"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(9, 9, 9, 9);
        root->setSpacing(7);

        auto* titleLabel = new QLabel(std::move(title), this);
        titleLabel->setObjectName(QStringLiteral("lightParametersTitle"));
        root->addWidget(titleLabel);

        if (hasColor())
        {
            const double rawRed = std::max(0.0, finiteOr(initial.red, 0.0));
            const double rawGreen = std::max(0.0, finiteOr(initial.green, 0.0));
            const double rawBlue = std::max(0.0, finiteOr(initial.blue, 0.0));
            const double scale = usesColorIntensity()
                ? std::max({ rawRed, rawGreen, rawBlue })
                : 1.0;
            if (usesColorIntensity() && scale > 1.0e-12)
            {
                displayColor_ = { rawRed / scale, rawGreen / scale, rawBlue / scale };
            }
            else
            {
                displayColor_ = {
                    std::clamp(rawRed, 0.0, 1.0),
                    std::clamp(rawGreen, 0.0, 1.0),
                    std::clamp(rawBlue, 0.0, 1.0),
                };
            }

            colorEditor_ = new LiveColorPopup(
                this,
                currentDisplayColor(),
                false,
                [this](const QColor& color)
                {
                    if (!color.isValid())
                    {
                        return;
                    }
                    colorEdited_ = true;
                    // Use QColor's native 16-bit channels so concise picker
                    // values do not gain float promotion noise when authored.
                    const QRgba64 rgba = color.rgba64();
                    constexpr double channelMax = 65535.0;
                    displayColor_ = {
                        static_cast<double>(rgba.red()) / channelMax,
                        static_cast<double>(rgba.green()) / channelMax,
                        static_cast<double>(rgba.blue()) / channelMax,
                    };
                    if (colorIntensity_ && colorIntensity_->value() <= 1.0e-12
                        && (color.red() != 0 || color.green() != 0 || color.blue() != 0))
                    {
                        colorIntensity_->setValue(1.0);
                    }
                    notifyPreview();
                },
                {},
                LiveColorPopup::Presentation::Embedded);
            root->addWidget(colorEditor_);

            if (usesColorIntensity())
            {
                colorIntensity_ = makeScrub(
                    QStringLiteral("lightParameters.colorIntensity"),
                    0.0,
                    64.0,
                    scale,
                    0.05,
                    true);
                colorIntensity_->setToolTip(QStringLiteral(
                    "Brightness multiplier for the selected color; values may exceed 1."));
            }
        }

        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(5);
        root->addLayout(form);

        if (colorIntensity_)
        {
            form->addRow(QStringLiteral("Intensity"), colorIntensity_);
        }

        if (hasScalarIntensity())
        {
            scalarIntensity_ = makeScrub(
                QStringLiteral("lightParameters.intensity"),
                0.0,
                100000.0,
                initial.intensity,
                0.05,
                false,
                [this](ScrubFloatEdit*, double) { scalarIntensityEdited_ = true; });
            form->addRow(QStringLiteral("Intensity"), scalarIntensity_);
        }

        if (hasRange())
        {
            range_ = makeScrub(
                QStringLiteral("lightParameters.range"),
                0.0,
                100000.0,
                initial.range,
                0.1,
                false,
                [this](ScrubFloatEdit*, double) { rangeEdited_ = true; });
            form->addRow(QStringLiteral("Range"), range_);
        }

        if ( mode_ == Mode::motsBaked )
        {
            rgbFollowsWhite_ = new QCheckBox(QStringLiteral("RGB follows white"), this);
            rgbFollowsWhite_->setObjectName(QStringLiteral("lightParameters.rgbFollowsWhite"));
            rgbFollowsWhite_->setChecked(!initial.rgbIntensity.has_value());
            form->addRow(rgbFollowsWhite_);
            rgbIntensity_ = makeScrub(QStringLiteral("lightParameters.rgbIntensity"),
                0.0, 1000.0, initial.rgbIntensity.value_or(initial.intensity), 0.05, false,
                [this](ScrubFloatEdit*, const double value)
                {
                    if ( !rgbFollowsWhite_->isChecked() && std::isfinite(value)
                        && value >= 0.0 && value <= 1000.0 )
                    {
                        authoredRgbIntensity_ = value;
                        rgbIntensityEdited_ = true;
                    }
                });
            rgbIntensity_->setEnabled(!rgbFollowsWhite_->isChecked());
            rgbIntensity_->setToolTip(QStringLiteral(
                "Independent MotS RGB intensity. Explicit zero disables RGB; follow white removes the override."));
            form->addRow(QStringLiteral("RGB intensity"), rgbIntensity_);
            connect(rgbFollowsWhite_, &QCheckBox::toggled, this, [this](const bool follows)
                {
                    authoredRgbIntensity_ = follows ? std::nullopt : std::optional<double>(
                        std::clamp(finiteOr(rgbIntensity_->value(), 0.0), 0.0, 1000.0));
                    rgbIntensityEdited_ = true;
                    rgbIntensity_->setEnabled(!follows);
                    notifyPreview();
                });

            retainLegacyRgbRange_ = new QCheckBox(QStringLiteral("Retain legacy RGB range"), this);
            retainLegacyRgbRange_->setObjectName(QStringLiteral("lightParameters.retainLegacyRgbRange"));
            retainLegacyRgbRange_->setChecked(initial.legacyRgbRange.has_value());
            form->addRow(retainLegacyRgbRange_);
            legacyRgbRange_ = makeScrub(QStringLiteral("lightParameters.legacyRgbRange"),
                -100000.0, 100000.0, initial.legacyRgbRange.value_or(0.0), 0.1, false,
                [this](ScrubFloatEdit*, const double value)
                {
                    if ( retainLegacyRgbRange_->isChecked() && std::isfinite(value)
                        && value >= -100000.0 && value <= 100000.0 )
                    {
                        authoredLegacyRgbRange_ = value;
                        legacyRgbRangeEdited_ = true;
                    }
                });
            legacyRgbRange_->setEnabled(retainLegacyRgbRange_->isChecked());
            legacyRgbRange_->setToolTip(QStringLiteral(
                "Retained legacy metadata only. The primary Range controls both white and RGB falloff."));
            form->addRow(QStringLiteral("Legacy RGB range"), legacyRgbRange_);
            connect(retainLegacyRgbRange_, &QCheckBox::toggled, this, [this](const bool retain)
                {
                    authoredLegacyRgbRange_ = retain ? std::optional<double>(std::clamp(
                        finiteOr(legacyRgbRange_->value(), 0.0), -100000.0, 100000.0)) : std::nullopt;
                    legacyRgbRangeEdited_ = true;
                    legacyRgbRange_->setEnabled(retain);
                    notifyPreview();
                });
        }

        if (hasSectorRanges())
        {
            sectorRangeRatio_ = validSectorRangeRatio(initial.minRange, initial.maxRange);
            minRange_ = makeScrub(
                QStringLiteral("lightParameters.minRange"),
                0.0,
                100000.0,
                initial.minRange,
                0.1,
                false,
                [this](ScrubFloatEdit* edited, const double value)
                {
                    synchronizeSectorRanges(edited, value);
                });
            maxRange_ = makeScrub(
                QStringLiteral("lightParameters.maxRange"),
                0.0,
                100000.0,
                initial.maxRange,
                0.1,
                false,
                [this](ScrubFloatEdit* edited, const double value)
                {
                    synchronizeSectorRanges(edited, value);
                });
            minRange_->setToolTip(QStringLiteral(
                "Outer falloff range; zero lets the engine derive it."));
            maxRange_->setToolTip(QStringLiteral(
                "Full-strength inner range; zero lets the engine derive it."));

            auto* rangeRow = new QWidget(this);
            rangeRow->setObjectName(QStringLiteral("lightParameters.sectorRanges"));
            auto* rangeLayout = new QHBoxLayout(rangeRow);
            rangeLayout->setContentsMargins(0, 0, 0, 0);
            rangeLayout->setSpacing(4);

            auto* rangeEditors = new QWidget(rangeRow);
            auto* rangeEditorsLayout = new QGridLayout(rangeEditors);
            rangeEditorsLayout->setContentsMargins(0, 0, 0, 0);
            rangeEditorsLayout->setHorizontalSpacing(5);
            rangeEditorsLayout->setVerticalSpacing(3);
            auto* minLabel = new QLabel(QStringLiteral("Min"), rangeEditors);
            auto* maxLabel = new QLabel(QStringLiteral("Max"), rangeEditors);
            minLabel->setMinimumWidth(
                std::max(minLabel->sizeHint().width(), maxLabel->sizeHint().width()));
            rangeEditorsLayout->addWidget(minLabel, 0, 0);
            rangeEditorsLayout->addWidget(minRange_, 0, 1);
            rangeEditorsLayout->addWidget(maxLabel, 1, 0);
            rangeEditorsLayout->addWidget(maxRange_, 1, 1);
            rangeEditorsLayout->setColumnStretch(1, 1);
            rangeLayout->addWidget(rangeEditors, 1);

            sectorRangeLink_ = new QToolButton(rangeRow);
            sectorRangeLink_->setObjectName(QStringLiteral("lightParameters.linkRanges"));
            sectorRangeLink_->setAccessibleName(QStringLiteral("Link point-light ranges"));
            sectorRangeLink_->setCheckable(true);
            sectorRangeLink_->setChecked(true);
            sectorRangeLink_->setAutoRaise(true);
            sectorRangeLink_->setToolButtonStyle(Qt::ToolButtonIconOnly);
            sectorRangeLink_->setIconSize(QSize(18, 18));
            sectorRangeLink_->setFixedSize(24, 48);
            sectorRangeLink_->setStyleSheet(QStringLiteral(
                "QToolButton { border: 0; background: transparent; padding: 0; }"));
            updateSectorRangeLinkAppearance();
            connect(sectorRangeLink_, &QToolButton::toggled, this, [this](const bool checked)
                {
                    sectorRangesLinked_ = checked;
                    if (checked && minRange_ && maxRange_)
                    {
                        sectorRangeRatio_ = validSectorRangeRatio(
                            minRange_->value(),
                            maxRange_->value());
                    }
                    updateSectorRangeLinkAppearance();
                });
            rangeLayout->addWidget(sectorRangeLink_, 0, Qt::AlignVCenter);
            form->addRow(QStringLiteral("Ranges"), rangeRow);
        }

        if (hasOffset())
        {
            offset_[0] = makeScrub(
                QStringLiteral("lightParameters.offsetX"),
                -100000.0,
                100000.0,
                initial.offsetX,
                0.05);
            offset_[1] = makeScrub(
                QStringLiteral("lightParameters.offsetY"),
                -100000.0,
                100000.0,
                initial.offsetY,
                0.05);
            offset_[2] = makeScrub(
                QStringLiteral("lightParameters.offsetZ"),
                -100000.0,
                100000.0,
                initial.offsetZ,
                0.05);
            form->addRow(QStringLiteral("Offset X"), offset_[0]);
            form->addRow(QStringLiteral("Offset Y"), offset_[1]);
            form->addRow(QStringLiteral("Offset Z"), offset_[2]);
        }

        auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        escape->setContext(Qt::WidgetWithChildrenShortcut);
        connect(escape, &QShortcut::activated, this, &QWidget::close);

        root->activate();
        adjustSize();
        setFixedWidth(std::max(306, sizeHint().width()));
    }

    [[nodiscard]] Mode mode() const noexcept { return mode_; }

    [[nodiscard]] Values values() const
    {
        Values out = initial_;
        out.rgbIntensity = authoredRgbIntensity_;
        out.legacyRgbRange = authoredLegacyRgbRange_;
        out.rgbIntensityEdited = rgbIntensityEdited_;
        out.legacyRgbRangeEdited = legacyRgbRangeEdited_;
        out.colorEdited = colorEdited_;
        out.intensityEdited = scalarIntensityEdited_;
        out.rangeEdited = rangeEdited_;
        if (hasColor() && colorEdited_)
        {
            // Avoid quantizing authored RGB merely because the author adjusted
            // an unrelated range/offset field.
            const double scale = colorIntensity_ ? colorIntensity_->value() : 1.0;
            out.red = displayColor_[0] * scale;
            out.green = displayColor_[1] * scale;
            out.blue = displayColor_[2] * scale;
        }
        if (scalarIntensity_)
        {
            out.intensity = scalarIntensity_->value();
        }
        if (range_)
        {
            out.range = range_->value();
        }
        if (minRange_)
        {
            out.minRange = minRange_->value();
        }
        if (maxRange_)
        {
            out.maxRange = maxRange_->value();
        }
        if (offset_[0])
        {
            out.offsetX = offset_[0]->value();
            out.offsetY = offset_[1]->value();
            out.offsetZ = offset_[2]->value();
        }
        return out;
    }

    /** Places the transient editor near the invoking viewport/property control. */
    void showAt(QPoint globalPosition = QCursor::pos())
    {
        adjustSize();
        globalPosition += QPoint(8, 8);
        const QScreen* screen = QGuiApplication::screenAt(globalPosition);
        if (!screen && parentWidget())
        {
            screen = parentWidget()->screen();
        }
        if (screen)
        {
            const QRect available = screen->availableGeometry().adjusted(6, 6, -6, -6);
            if (globalPosition.x() + width() > available.right())
            {
                globalPosition.setX(available.right() - width());
            }
            if (globalPosition.y() + height() > available.bottom())
            {
                globalPosition.setY(globalPosition.y() - height() - 16);
            }
            globalPosition.setX(std::max(globalPosition.x(), available.left()));
            globalPosition.setY(std::max(globalPosition.y(), available.top()));
        }
        move(globalPosition);
        show();
        raise();
        activateWindow();
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (!closeReported_)
        {
            closeReported_ = true;
            if (closedCallback_)
            {
                closedCallback_();
            }
        }
        QWidget::closeEvent(event);
    }

private:
    [[nodiscard]] bool hasColor() const noexcept
    {
        return mode_ != Mode::jkdf2Baked
            && mode_ != Mode::classicThing
            && mode_ != Mode::classicHeadlight;
    }

    [[nodiscard]] bool usesColorIntensity() const noexcept
    {
        return mode_ == Mode::ijimThing
            || mode_ == Mode::ijimHeadlight
            || mode_ == Mode::ijimSector;
    }

    [[nodiscard]] bool hasScalarIntensity() const noexcept
    {
        return mode_ == Mode::baked
            || mode_ == Mode::motsBaked
            || mode_ == Mode::jkdf2Baked
            || mode_ == Mode::classicThing
            || mode_ == Mode::classicHeadlight;
    }

    [[nodiscard]] bool hasRange() const noexcept
    {
        return mode_ == Mode::baked
            || mode_ == Mode::motsBaked
            || mode_ == Mode::jkdf2Baked
            || mode_ == Mode::ijimThing
            || mode_ == Mode::ijimHeadlight;
    }

    [[nodiscard]] bool hasSectorRanges() const noexcept
    {
        return mode_ == Mode::ijimSector;
    }

    [[nodiscard]] bool hasOffset() const noexcept
    {
        return mode_ == Mode::ijimHeadlight || mode_ == Mode::classicHeadlight;
    }

    static double finiteOr(const double value, const double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }

    using BeforePreviewCallback = std::function<void(ScrubFloatEdit*, double)>;

    ScrubFloatEdit* makeScrub(
        const QString& objectName,
        const double minimum,
        const double maximum,
        const double value,
        const double step,
        const bool editsColor = false,
        BeforePreviewCallback beforePreview = {})
    {
        auto* scrub = new ScrubFloatEdit(this);
        scrub->setObjectName(objectName);
        scrub->setDecimals(6);
        scrub->setRange(minimum, maximum);
        scrub->setSingleStep(step);
        scrub->setPixelsPerStep(5.0);
        scrub->setValue(std::clamp(finiteOr(value, 0.0), minimum, maximum));

        const auto changed = [this, scrub, editsColor, beforePreview](const double value)
        {
            if (beforePreview)
            {
                beforePreview(scrub, value);
            }
            colorEdited_ = colorEdited_ || editsColor;
            notifyPreview();
        };
        connect(scrub, &ScrubFloatEdit::scrubValueChanged, this, changed);
        connect(scrub, &ScrubFloatEdit::valueCommitted, this, changed);
        connect(scrub->lineEdit(), &QLineEdit::textEdited, this,
            [this, scrub, editsColor, beforePreview](const QString& text)
            {
                bool valid = false;
                const double typed = parseUiNumber(text, scrub->lineEdit()->locale(), &valid);
                if (!valid || !std::isfinite(typed))
                {
                    return;
                }
                if (beforePreview)
                {
                    beforePreview(scrub, typed);
                }
                colorEdited_ = colorEdited_ || editsColor;
                notifyPreview();
            });
        return scrub;
    }

    static double validSectorRangeRatio(const double minRange, const double maxRange) noexcept
    {
        constexpr double fallbackRatio = 4.0;
        return std::isfinite(minRange) && std::isfinite(maxRange)
                && minRange > 1.0e-12 && maxRange > 1.0e-12
            ? minRange / maxRange
            : fallbackRatio;
    }

    void synchronizeSectorRanges(ScrubFloatEdit* edited, const double value)
    {
        if (!sectorRangesLinked_ || !minRange_ || !maxRange_)
        {
            return;
        }
        const double ratio = std::isfinite(sectorRangeRatio_)
                && sectorRangeRatio_ > 1.0e-12
            ? sectorRangeRatio_
            : 4.0;
        if (edited == minRange_)
        {
            maxRange_->setValue(value / ratio);
        }
        else if (edited == maxRange_)
        {
            minRange_->setValue(value * ratio);
        }
    }

    void updateSectorRangeLinkAppearance()
    {
        if (!sectorRangeLink_)
        {
            return;
        }
        sectorRangeLink_->setIcon(makeViewportIcon(sectorRangesLinked_
            ? ViewportIcon::rangeLinked
            : ViewportIcon::rangeUnlinked));
        sectorRangeLink_->setToolTip(sectorRangesLinked_
            ? QStringLiteral("Ranges linked: editing either preserves the current min:max ratio.")
            : QStringLiteral("Ranges unlinked: edit minimum and maximum independently."));
    }

    [[nodiscard]] QColor currentDisplayColor() const
    {
        return QColor::fromRgbF(
            std::clamp(displayColor_[0], 0.0, 1.0),
            std::clamp(displayColor_[1], 0.0, 1.0),
            std::clamp(displayColor_[2], 0.0, 1.0));
    }

    void notifyPreview()
    {
        if ( rgbFollowsWhite_ && rgbFollowsWhite_->isChecked() && scalarIntensity_ )
        {
            rgbIntensity_->setValue(scalarIntensity_->value());
        }
        if (previewCallback_)
        {
            previewCallback_(values());
        }
    }

    Mode mode_{};
    Values initial_{};
    std::optional<double> authoredRgbIntensity_{};
    std::optional<double> authoredLegacyRgbRange_{};
    bool rgbIntensityEdited_{};
    bool legacyRgbRangeEdited_{};
    bool scalarIntensityEdited_{};
    bool rangeEdited_{};
    PreviewCallback previewCallback_{};
    ClosedCallback closedCallback_{};
    std::array<double, 3> displayColor_{ 1.0, 1.0, 1.0 };
    bool colorEdited_{};
    bool closeReported_{};
    LiveColorPopup* colorEditor_{};
    ScrubFloatEdit* colorIntensity_{};
    ScrubFloatEdit* scalarIntensity_{};
    ScrubFloatEdit* range_{};
    QCheckBox* rgbFollowsWhite_{};
    ScrubFloatEdit* rgbIntensity_{};
    QCheckBox* retainLegacyRgbRange_{};
    ScrubFloatEdit* legacyRgbRange_{};
    ScrubFloatEdit* minRange_{};
    ScrubFloatEdit* maxRange_{};
    QToolButton* sectorRangeLink_{};
    bool sectorRangesLinked_{ true };
    double sectorRangeRatio_{ 4.0 };
    std::array<ScrubFloatEdit*, 3> offset_{};
};

} // namespace sed::ui
