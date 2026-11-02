#include "main_window/common.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/level/cog_bindings.hpp"
#include "ui/cog/cog_gui_wiring_utils.hpp"
#include "ui/cog/cog_preflight_dialog.hpp"
#include "ui/cog/placed_cog_bindings_dialog.hpp"
#include "ui/lighting/mots_arch_lighting_dialog.hpp"
#include "ui/query/workbench_dialog.hpp"

#include <QMessageBox>

namespace sed
{
namespace
{

std::optional<assets::CogSchema> resolveCogSchema(
    const assets::ResourceLocator& resources, const std::string_view name)
{
    const std::optional<std::string> text =
        resources.tryReadTextWithExt(name, ".cog");
    if (!text.has_value())
    {
        return std::nullopt;
    }
    return assets::parseCogSchema(name, *text);
}

std::int32_t firstSelectedCog(const editor::Selection& selection)
{
    if (selection.cogs.empty())
    {
        return -1;
    }
    return *selection.cogs.begin();
}

} // namespace

void MainWindow::showCogPreflightForSelectedCog()
{
    const Level& level = document_.level();
    const std::int32_t cogIndex = firstSelectedCog(document_.selection());
    if (cogIndex < 0 || static_cast<std::size_t>(cogIndex) >= level.cogs.size())
    {
        if (statusBar())
        {
            statusBar()->showMessage(
                QStringLiteral("Select a placed COG first."), 4000);
        }
        return;
    }
    const Cog& cog = level.cogs[static_cast<std::size_t>(cogIndex)];
    const std::optional<std::string> text =
        resources_.tryReadTextWithExt(cog.name, ".cog");
    if (!text.has_value())
    {
        QMessageBox::information(this, QStringLiteral("COG preflight"),
            QStringLiteral("Script '%1' could not be resolved; "
                           "preflight needs captured script text.")
                .arg(QString::fromStdString(cog.name)));
        return;
    }
    ui::cog::CogPreflightDialog dialog(
        QString::fromStdString(cog.name), QString::fromStdString(*text),
        level.kind, this);
    dialog.exec();
}

void MainWindow::showPlacedCogBindingsForSelectedCog()
{
    const Level& level = document_.level();
    const std::int32_t cogIndex = firstSelectedCog(document_.selection());
    if (cogIndex < 0 || static_cast<std::size_t>(cogIndex) >= level.cogs.size())
    {
        if (statusBar())
        {
            statusBar()->showMessage(
                QStringLiteral("Select a placed COG first."), 4000);
        }
        return;
    }
    const Cog& cog = level.cogs[static_cast<std::size_t>(cogIndex)];
    const std::optional<assets::CogSchema> schema =
        resolveCogSchema(resources_, cog.name);
    const level::CogBindingDescription description = level::describeBindings(
        level, cogIndex, schema.has_value() ? &*schema : nullptr);

    // The dialog never edits values itself: every rebind runs the core
    // prepare step first and applies as one undo step only on success.
    auto rebind = [this, cogIndex, schema](
        const std::int32_t parameterIndex,
        const std::int32_t targetIndex)
        -> std::optional<level::CogMutationFailure>
    {
        const Level& current = document_.level();
        if (cogIndex < 0
            || static_cast<std::size_t>(cogIndex) >= current.cogs.size()
            || !schema.has_value()
            || parameterIndex < 0
            || static_cast<std::size_t>(parameterIndex)
                >= schema->levelValues().size())
        {
            return level::CogMutationFailure{
                "incompleteCogSchema",
                "The captured schema is unavailable for rebinding." };
        }
        const assets::CogSymbol* symbol =
            schema->levelValues()[static_cast<std::size_t>(parameterIndex)];
        const level::PreparedRebindCogReference prepared =
            level::prepareRebindCogReference(
                current, cogIndex, parameterIndex, symbol->name,
                symbol->kind, targetIndex, &*schema, std::nullopt);
        if (!prepared.ok())
        {
            return prepared.failure;
        }
        const bool applied = document_.applyUndoable(
            "COG: Reference changed", editor::DocumentDirty::Cogs,
            [prepared](editor::DocumentState& state) -> bool
            {
                return prepared.apply(state.level);
            });
        if (!applied)
        {
            return level::CogMutationFailure{
                "staleCogBinding",
                "The placed COG changed before the rebind could apply." };
        }
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
        return std::nullopt;
    };

    ui::cog::PlacedCogBindingsDialog dialog(description, rebind, this);
    dialog.exec();
}

void MainWindow::showWorkbenchDialog()
{
    std::vector<SurfaceRef> surfaces{};
    surfaces.reserve(document_.selection().surfaces.size());
    for (const SurfaceRef& ref : document_.selection().surfaces)
    {
        surfaces.push_back(ref);
    }
    level::CogSchemaResolver resolver =
        [this](const std::string_view name)
            -> std::optional<assets::CogSchema>
        {
            return resolveCogSchema(resources_, name);
        };
    ui::query::WorkbenchDialog::Handlers handlers{};
    handlers.applyBatchUv = [this](const level::PreparedBatchUv& prepared)
    {
        const bool applied = document_.applyUndoable(
            "Workbench: Batch UV", editor::DocumentDirty::Structure,
            [prepared](editor::DocumentState& state) -> bool
            {
                return prepared.apply(state.level);
            });
        if (applied)
        {
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        }
        return applied;
    };
    handlers.applyThingArray = [this](const level::PreparedThingArray& prepared)
    {
        const bool applied = document_.applyUndoable(
            "Workbench: Thing array", editor::DocumentDirty::Structure,
            [prepared](editor::DocumentState& state) -> bool
            {
                return prepared.apply(state.level);
            });
        if (applied)
        {
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        }
        return applied;
    };
    handlers.applyLightArray = [this](const level::PreparedLightArray& prepared)
    {
        const bool applied = document_.applyUndoable(
            "Workbench: Light array", editor::DocumentDirty::Structure,
            [prepared](editor::DocumentState& state) -> bool
            {
                return prepared.apply(state.level);
            });
        if (applied)
        {
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        }
        return applied;
    };
    ui::query::WorkbenchDialog dialog(
        document_.level(), surfaces, resolver, handlers, this);
    dialog.exec();
}

void MainWindow::showArchInspectorDialog()
{
    const assets::Templates* templatesDb =
        templates_.size() > 0 ? &templates_ : nullptr;
    ui::lighting::MotsArchLightingDialog dialog(
        document_.level(), templatesDb, &resources_, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const ui::lighting::MotsArchLightingPlan& plan = dialog.plan();
    if (!plan.hasPendingEdit)
    {
        return;
    }
    const std::vector<ui::lighting::MotsArchLightingTarget> currentTargets =
        ui::lighting::collectMotsArchLightingTargets(
            document_.level(), templatesDb, &resources_);
    const ui::lighting::MotsArchLightingPlanResult validation =
        ui::lighting::validateMotsArchLightingPlanResources(
            plan, currentTargets);
    if (validation.status == ui::lighting::MotsArchLightingPlanStatus::Rejected)
    {
        QMessageBox::warning(this, QStringLiteral("ARCH inspector"),
            QString::fromStdString(validation.message));
        return;
    }
    const std::vector<level::MotsArchLightingTopology> topologies =
        ui::lighting::selectedMotsArchLightingTopologies(plan);
    level::MotsArchLightingEditResult editResult{};
    const bool applied = document_.applyUndoable(
        "ARCH: Inspector edit", editor::DocumentDirty::All,
        [&](editor::DocumentState& state) -> bool
        {
            editResult = level::applyMotsArchLightingEdit(
                state.level, plan.selectedThingIndices, topologies,
                plan.edit, templatesDb);
            return editResult.status
                == level::MotsArchLightingEditStatus::Applied;
        });
    if (!applied && !editResult.message.empty())
    {
        QMessageBox::warning(this, QStringLiteral("ARCH inspector"),
            QString::fromStdString(editResult.message));
        return;
    }
    if (applied)
    {
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
    }
}

} // namespace sed
