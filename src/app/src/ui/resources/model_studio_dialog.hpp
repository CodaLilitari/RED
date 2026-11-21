#pragma once

#include "sed/editor/native_model_session.hpp"

#include <QDialog>
#include <functional>
#include <memory>
#include <optional>

namespace sed::assets { class ResourceLocator; }

namespace sed::ui::resources
{

/** Numeric, source-preserving model authoring with its own history.
 * Immutable snapshots may be inspected independently of the open Level. */
class ModelStudioDialog final : public QDialog
{
public:
    struct Context
    {
        std::shared_ptr<const assets::ResourceLocator> resources;
        // GUI thread only. Never retained or invoked by a worker.
        std::function<bool()> isCurrent;
    };

    struct State
    {
        std::optional<assets::NativeModelDocument> committed;
        std::optional<assets::NativeModelDocument> displayed;
        std::optional<editor::NativeModelSelection> selection;
        editor::NativeModelRevision revision;
        bool dirty{}, busy{}, pendingInput{}, canUndo{}, canRedo{};
    };

    struct WorkHooksForTests
    {
        // Executed on the worker at deterministic preparation boundaries.
        std::function<void()> beforePreview;
        std::function<void()> afterStage;
    };

    explicit ModelStudioDialog(Context context, QWidget* parent = nullptr);
    ~ModelStudioDialog() override;
    State state() const;
    void setWorkHooksForTests(WorkHooksForTests hooks);
    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::resources
