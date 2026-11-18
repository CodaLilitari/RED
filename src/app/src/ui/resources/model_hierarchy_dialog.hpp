#pragma once

#include "sed/assets/model_hierarchy.hpp"

#include <QDialog>

#include <array>
#include <filesystem>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;

namespace sed::ui::resources
{

/**
 * 3DO Hierarchy: opens a 3DO, shows its node tree, and edits a node's name,
 * parent, position, rotation and pivot; adds and removes nodes; saves the
 * result as a new 3DO. It also starts a KEY from the model, and checks a KEY
 * against it, recomputing the KEY's deltas into a new file when they are
 * stale. Every change is checked against the engine's rules first.
 */
class ModelHierarchyDialog final : public QDialog
{
public:
    explicit ModelHierarchyDialog(QWidget* parent = nullptr);

    /** Loads a 3DO without applying its hierarchy, so it saves back unchanged; false with a message on failure. */
    bool loadModel(const std::filesystem::path& path);
    [[nodiscard]] const assets::Model3do& model() const noexcept { return model_; }
    void selectNode(std::int32_t node);
    /** Applies the edit panel to the selected node; the message on refusal, empty on success. */
    QString applyNodeEdits();
    QString addChild(const QString& name);
    QString removeSelected();
    /** Writes the edited model; refuses an existing file. */
    QString saveAs(const std::filesystem::path& path) const;
    QString writeKeyFromModel(const std::filesystem::path& path, std::int32_t frames, double fps) const;
    /** Checks a KEY against the model; with `fixedPath`, writes it with recomputed deltas. */
    QString checkKey(const std::filesystem::path& keyPath, const std::optional<std::filesystem::path>& fixedPath = std::nullopt);
    [[nodiscard]] QString reportText() const;

private:
    void rebuild();
    void showNode(std::int32_t node);
    void report(const QString& text);

    assets::Model3do model_{};
    std::filesystem::path path_{};
    std::int32_t selected_{ -1 };
    QTreeWidget* tree_{};
    QLineEdit* name_{};
    QComboBox* parent_{};
    std::array<QDoubleSpinBox*, 9> transform_{};
    QPlainTextEdit* report_{};
};

} // namespace sed::ui::resources
