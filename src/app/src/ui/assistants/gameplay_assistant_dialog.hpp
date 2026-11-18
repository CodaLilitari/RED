#pragma once

#include "sed/editor/selection.hpp"
#include "sed/level.hpp"
#include "sed/level/gameplay_assistants.hpp"

#include <QDialog>

#include <array>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;

namespace sed::ui::assistants
{

/**
 * One dialog for the five gameplay assistants. It takes its targets from the
 * selection (door pieces, the lift, switches, goal sectors and Things, the
 * camera), offers the settings, and **Try it** runs the result on a copy of
 * the level in the COG runtime and shows what happened. Nothing changes the
 * level until the caller applies `applyTo` to the document.
 */
class GameplayAssistantDialog final : public QDialog
{
public:
    GameplayAssistantDialog(level::AssistantKind kind, const Level& level, const editor::Selection& selection,
        const assets::Templates* templates, std::vector<CogValue> scriptSymbols, std::string scriptText,
        QWidget* parent = nullptr);

    /** What the dialog will build from the selection, for refusing an empty one up front; empty when it's usable. */
    [[nodiscard]] QString selectionProblem() const;

    /** Runs the assistant with the dialog's settings. */
    level::AssistantResult applyTo(Level& level) const;

    /** Applies to a copy of the level, runs the runtime check and shows the result. */
    level::AssistantCheck tryIt();

    [[nodiscard]] QString resultText() const;

private:
    QDoubleSpinBox* spin(const QString& objectName, double value, double low, double high, int decimals = 3);
    std::array<double, 3> offset() const;

    level::AssistantKind kind_;
    const Level& level_;
    const assets::Templates* templates_;
    std::vector<CogValue> symbols_;
    std::string scriptText_;

    std::vector<std::int32_t> things_;
    std::vector<SurfaceRef> buttons_;
    std::vector<SurfaceRef> adjoins_;
    std::vector<std::int32_t> sectors_;

    std::array<QDoubleSpinBox*, 3> offset_{};
    QDoubleSpinBox* speed_{};
    QDoubleSpinBox* wait_{};
    QDoubleSpinBox* light_{};
    QDoubleSpinBox* startWait_{};
    QDoubleSpinBox* ramDamage_{};
    QDoubleSpinBox* fireInterval_{};
    QDoubleSpinBox* survival_{};
    QSpinBox* goalBase_{};
    std::vector<QComboBox*> goalTriggers_{};
    QCheckBox* endWhenDone_{};
    QCheckBox* once_{};
    QPlainTextEdit* result_{};
};

} // namespace sed::ui::assistants
