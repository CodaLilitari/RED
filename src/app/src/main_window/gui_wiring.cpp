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
#include <map>
#include <set>

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

std::shared_ptr<const GlView::CogMapShape> cogMapShape(const Level& level, const level::CogBindingTarget& target)
{
    using Kind = level::CogReferenceKind;
    if ( target.status != level::CogBindingTargetStatus::level || !target.index ) return {};
    auto shape = std::make_shared<GlView::CogMapShape>();
    if ( target.kind == Kind::thing )
    {
        const auto& thing = level.things.at(static_cast<std::size_t>(*target.index));
        shape->low = shape->high = shape->center = {thing.x, thing.y, thing.z};
        return shape;
    }
    const auto sectorIndex = target.kind == Kind::surface ? target.sectorIndex : target.index;
    if ( !sectorIndex || *sectorIndex < 0 || static_cast<std::size_t>(*sectorIndex) >= level.sectors.size() ) return {};
    const auto& sector = level.sectors[*sectorIndex];
    std::set<std::pair<int, int>> edges;
    const auto collect = [&](const Surface& face)
    {
        for ( std::size_t i = 0; i < face.vertices.size(); ++i )
        {
            const int a = face.vertices[i], b = face.vertices[(i + 1) % face.vertices.size()];
            if ( a >= 0 && b >= 0 && static_cast<std::size_t>(std::max(a, b)) < sector.vertices.size() )
            {
                edges.insert(std::minmax(a, b));
            }
        }
    };
    if ( target.kind == Kind::surface )
    {
        if ( !target.surfaceIndex || *target.surfaceIndex < 0
            || static_cast<std::size_t>(*target.surfaceIndex) >= sector.surfaces.size() ) return {};
        collect(sector.surfaces[*target.surfaceIndex]);
    }
    else
    {
        for ( const auto& face : sector.surfaces ) collect(face);
    }
    if ( edges.empty() ) return {};
    for ( const auto& [a, b] : edges )
    {
        for ( const auto index : {a, b} )
        {
            const auto& vertex = sector.vertices[index];
            const TVector point{vertex.x, vertex.y, vertex.z};
            if ( shape->edges.empty() ) shape->low = shape->high = point;
            shape->low = {std::min(shape->low.x, point.x), std::min(shape->low.y, point.y), std::min(shape->low.z, point.z)};
            shape->high = {std::max(shape->high.x, point.x), std::max(shape->high.y, point.y), std::max(shape->high.z, point.z)};
            shape->edges.push_back(point);
        }
    }
    shape->center = (shape->low + shape->high).scaled(0.5);
    return shape;
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

    const auto lifetime = documentLifetimeGeneration_;
    const auto history = document_.nextUndoRecordId();
    const auto context = resourceEditorDocumentContextGeneration_;
    const auto current = [this, lifetime, history, context, cogIndex, id = cog.id, name = cog.name]
    {
        const auto& cogs = document_.level().cogs;
        return documentLifetimeGeneration_ == lifetime
            && resourceEditorDocumentContextGeneration_ == context && document_.nextUndoRecordId() == history
            && firstSelectedCog(document_.selection()) == cogIndex
            && static_cast<std::size_t>(cogIndex) < cogs.size() && cogs[cogIndex].id == id && cogs[cogIndex].name == name;
    };
    ui::cog::PlacedCogBindingsDialog dialog(description, rebind, this);
    if ( dialog.exec() != ui::cog::PlacedCogBindingsDialog::ShowMap || !glView_ || !current() ) return;
    if ( cogMapPanel_ ) cogMapPanel_->close();
    const auto bindings = ui::cog::placedCogMapSlots(description);
    std::vector<GlView::CogMapMarker> markers;
    std::map<std::pair<level::CogReferenceKind, int>, std::shared_ptr<const GlView::CogMapShape>> shapes;
    for ( const auto& binding : bindings )
    {
        std::shared_ptr<const GlView::CogMapShape> shape;
        if ( binding.target.status == level::CogBindingTargetStatus::level && binding.target.index )
        {
            const auto [it, inserted] = shapes.try_emplace({binding.target.kind, *binding.target.index});
            if ( inserted ) it->second = cogMapShape(document_.level(), binding.target);
            shape = it->second;
        }
        markers.push_back({QString::fromStdString(binding.name), std::move(shape)});
    }
    const auto frame = [this, markers](const int row)
    {
        bool haveBounds = false;
        QVector3D low, high;
        for ( std::size_t i = 0; i < markers.size(); ++i )
        {
            const auto& shape = markers[i].shape;
            if ( !shape || (row >= 0 && static_cast<int>(i) != row) ) continue;
            const auto vector = [](const TVector& p)
            {
                return QVector3D(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
            };
            const QVector3D a = vector(shape->low), b = vector(shape->high);
            if ( !haveBounds )
            {
                low = a;
                high = b;
                haveBounds = true;
            }
            for ( int axis = 0; axis < 3; ++axis )
            {
                low[axis] = std::min(low[axis], a[axis]);
                high[axis] = std::max(high[axis], b[axis]);
            }
        }
        if ( haveBounds )
        {
            const QVector3D pad(0.1f, 0.1f, 0.1f);
            frameBoundsWithNavigationPrefs(low - pad, high + pad, false);
        }
    };
    glView_->setCogMapMarkers(std::move(markers));
    auto* panel = new ui::cog::PlacedCogMapDialog(cogIndex, QString::fromStdString(description.scriptName),
        bindings, [this](int index) { glView_->highlightCogMapMarker(index); }, frame, this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setObjectName("placedCogMapInspector");
    cogMapPanel_ = panel;
    cogMapStillCurrent_ = current;
    connect(panel, &QDialog::finished, this, [this, panel]
    {
        if ( cogMapPanel_ == panel )
        {
            cogMapPanel_ = nullptr;
            cogMapStillCurrent_ = {};
            glView_->setCogMapMarkers({});
        }
    });
    panel->move(glView_->mapToGlobal(QPoint(10, 10)));
    frame(-1);
    panel->show();
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
