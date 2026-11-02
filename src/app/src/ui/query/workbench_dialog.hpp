#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QDialog>

#include "sed/level.hpp"
#include "sed/level/cog_bindings.hpp"
#include "sed/level/workbench.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;

namespace sed::ui::query
{

/**
 * Thin dialogs over the shared workbench core commands (F-12). Every
 * mutation runs the same core prepare step the agent uses and applies as
 * one undo step through the supplied handler; a prepare refusal is shown
 * with its stable code and changes nothing. Runtime-unverified here.
 */
class WorkbenchDialog final : public QDialog
{
public:
    struct Handlers
    {
        /** Applies one prepared step as a single undo step. */
        std::function<bool(const level::PreparedBatchUv&)> applyBatchUv{};
        std::function<bool(const level::PreparedThingArray&)> applyThingArray{};
        std::function<bool(const level::PreparedLightArray&)> applyLightArray{};
    };

    WorkbenchDialog(
        const Level& level,
        std::vector<SurfaceRef> selectedSurfaces,
        level::CogSchemaResolver schemaResolver,
        Handlers handlers,
        QWidget* parent = nullptr);

private:
    void runQuery();
    void runProjectUv();
    void runThingArray();
    void runLightArray();
    void showRefusal(const std::optional<level::WorkbenchFailure>& failure,
        QLabel* status);

    const Level& level_;
    std::vector<SurfaceRef> selectedSurfaces_{};
    level::CogSchemaResolver schemaResolver_{};
    Handlers handlers_{};

    QComboBox* collectionCombo_{};
    QLineEdit* searchEdit_{};
    QPlainTextEdit* queryOut_{};
    QLabel* queryStatus_{};

    QComboBox* directionCombo_{};
    QDoubleSpinBox* densitySpin_{};
    QLabel* uvStatus_{};

    QLineEdit* templateEdit_{};
    QLineEdit* prefixEdit_{};
    QSpinBox* countSpin_{};
    QDoubleSpinBox* stepXSpin_{};
    QDoubleSpinBox* stepYSpin_{};
    QDoubleSpinBox* stepZSpin_{};
    QLabel* arrayStatus_{};
};

} // namespace sed::ui::query
