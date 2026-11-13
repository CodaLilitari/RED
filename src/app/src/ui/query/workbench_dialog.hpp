#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <QDialog>

#include "sed/level.hpp"
#include "sed/level/cog_bindings.hpp"
#include "sed/level/workbench.hpp"

class QComboBox;
class QCheckBox;
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
    void runQuery(bool resetPage = true);
    void updateQueryFields();
    void invalidateQuery();
    level::PlacementPositions arrayPositions() const;
    TVector vectorValue(const char* prefix) const;
    void previewArray();
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
    QSpinBox* querySector_{};
    QSpinBox* queryLimit_{};
    QPushButton* queryNext_{};
    QPushButton* queryPrevious_{};
    std::size_t queryOffset_{};
    std::optional<std::size_t> queryNextOffset_{};
    std::map<std::string, QLineEdit*> queryFields_{};
    std::map<std::string, QCheckBox*> queryEnabled_{};

    QComboBox* directionCombo_{};
    QDoubleSpinBox* densitySpin_{};
    QLabel* uvStatus_{};
    std::map<std::string, QDoubleSpinBox*> numbers_{};

    QLineEdit* templateEdit_{};
    QLineEdit* prefixEdit_{};
    QSpinBox* countSpin_{};
    QComboBox* shapeCombo_{};
    QSpinBox* countUSpin_{};
    QSpinBox* countVSpin_{};
    QSpinBox* layerSpin_{};
    QSpinBox* sectorSpin_{};
    QCheckBox* explicitSector_{};
    QLineEdit* lightFlags_{};
    QCheckBox* lightRgbFollowsWhite_{};
    QPlainTextEdit* arrayPreview_{};
    QLabel* arrayStatus_{};
};

} // namespace sed::ui::query
