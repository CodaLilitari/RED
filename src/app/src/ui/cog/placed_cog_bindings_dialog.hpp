#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <QDialog>
#include <QString>

#include "sed/level/cog_bindings.hpp"
#include "ui/cog/cog_gui_wiring_utils.hpp"

class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace sed::ui::cog
{

/**
 * Placed-COG bindings view. Typed rows appear only for a complete,
 * count-exact schema; otherwise values stay positional under the explicit
 * fallback label and rebind controls stay disabled. Rebinds go through the
 * handler, which runs the core prepare step: a refusal is shown with its
 * stable code and changes nothing. Runtime-unverified in the sandbox.
 */
class PlacedCogBindingsDialog final : public QDialog
{
public:
    static constexpr int ShowMap = QDialog::Accepted + 1;

    /**
     * Runs core `prepareRebindCogReference` for one row and applies it as
     * one undo step only when preparation succeeds. Returns the refusal
     * (code + message) without mutating anything on failure.
     */
    using RebindHandler = std::function<std::optional<level::CogMutationFailure>(
        std::int32_t parameterIndex, std::int32_t targetIndex)>;

    PlacedCogBindingsDialog(
        const level::CogBindingDescription& description,
        RebindHandler rebindHandler,
        QWidget* parent = nullptr);

private:
    void refreshRows();

    level::CogBindingDescription description_{};
    RebindHandler rebindHandler_{};
    QTableWidget* table_{};
    QLabel* noteLabel_{};
    QLabel* statusLabel_{};
    QSpinBox* targetSpin_{};
    QPushButton* rebindButton_{};
};

class PlacedCogMapDialog final : public QDialog
{
public:
    PlacedCogMapDialog(int instance, const QString& script, const std::vector<PlacedCogMapSlot>& bindings,
        std::function<void(int)> highlight, std::function<void(int)> frame, QWidget* parent);
};

} // namespace sed::ui::cog
