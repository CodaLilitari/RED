#include "main_window/common.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/level/cog_bindings.hpp"
#include "ui/cog/cog_gui_wiring_utils.hpp"
#include "ui/cog/cog_preflight_dialog.hpp"
#include "ui/cog/placed_cog_bindings_dialog.hpp"
#include "ui/lighting/mots_arch_lighting_dialog.hpp"
#include "ui/layers/layer_color_utils.hpp"
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
    const auto lifetime = documentLifetimeGeneration_;
    const auto contextGeneration = resourceEditorDocumentContextGeneration_;
    const auto resourceIdentity = resources_.previewCacheIdentity();
    const auto project = document_.level().kind;
    const auto editingAvailable = [this, lifetime, contextGeneration, resourceIdentity, project]()
    {
        return documentEditingAllowed()
            && documentLifetimeGeneration_ == lifetime
            && resourceEditorDocumentContextGeneration_ == contextGeneration
            && resources_.previewCacheIdentity() == resourceIdentity
            && document_.level().kind == project;
    };
    const auto sectorEditable = [this](const std::int32_t sectorIndex)
    {
        const auto& sectors = document_.level().sectors;
        return sectorIndex >= 0 && static_cast<std::size_t>(sectorIndex) < sectors.size()
            && ui::layers::isLayerEditable(layerVisible_, layerLocked_,
                sectors[static_cast<std::size_t>(sectorIndex)].layer);
    };
    std::vector<SurfaceRef> surfaces{};
    surfaces.reserve(document_.selection().surfaces.size());
    for ( const SurfaceRef& ref : document_.selection().surfaces )
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
    handlers.applyBatchUv = [this, editingAvailable, sectorEditable](const level::PreparedBatchUv& prepared)
    {
        if ( !editingAvailable() )
        {
            return false;
        }
        std::vector<SurfaceRef> changedSurfaces;
        changedSurfaces.reserve(prepared.surfaces.size());
        for ( const auto& surface : prepared.surfaces )
        {
            if ( !sectorEditable(surface.ref.sector) )
            {
                return false;
            }
            changedSurfaces.push_back(surface.ref);
        }
        constexpr auto dirty = editor::DocumentDirty::LevelGeometry;
        const bool applied = document_.applyUndoable(
            "Workbench: Batch UV", dirty,
            [prepared](editor::DocumentState& state) -> bool
            {
                return prepared.apply(state.level);
            });
        if ( applied )
        {
            if ( glView_ )
            {
                glView_->notifySurfaceUvsChanged(changedSurfaces);
            }
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            scheduleRebuildChecks(dirty);
        }
        return applied;
    };
    handlers.applyThingArray = [this, editingAvailable, sectorEditable](const level::PreparedThingArray& prepared)
    {
        if ( !editingAvailable()
            || !ui::layers::isLayerEditable(layerVisible_, layerLocked_, prepared.layer) )
        {
            return false;
        }
        for ( const auto sectorIndex : prepared.sectorIndices )
        {
            if ( !sectorEditable(sectorIndex) )
            {
                return false;
            }
        }
        constexpr auto dirty = editor::DocumentDirty::Structure | editor::DocumentDirty::Things;
        const bool applied = document_.applyUndoable(
            "Workbench: Thing array", dirty,
            [prepared](editor::DocumentState& state) -> bool
            {
                return prepared.apply(state.level);
            });
        if ( applied )
        {
            if ( glView_ )
            {
                glView_->notifyThingsChanged();
            }
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            scheduleRebuildChecks(dirty);
        }
        return applied;
    };
    handlers.applyLightArray = [this, editingAvailable](const level::PreparedLightArray& prepared)
    {
        if ( !editingAvailable()
            || !ui::layers::isLayerEditable(layerVisible_, layerLocked_, prepared.layer) )
        {
            return false;
        }
        constexpr auto dirty = editor::DocumentDirty::Structure | editor::DocumentDirty::Lights;
        const bool applied = document_.applyUndoable(
            "Workbench: Light array", dirty,
            [prepared](editor::DocumentState& state) -> bool
            {
                return prepared.apply(state.level);
            });
        if ( applied )
        {
            if ( glView_ )
            {
                glView_->notifyLightsChanged();
            }
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            scheduleRebuildChecks(dirty);
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
