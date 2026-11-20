#include "main_window/common.hpp"
#include "render/viewport_color_theme.hpp"
#include "ui/recovery/document_recovery_utils.hpp"
#include "ui/shortcuts/workspace_selection.hpp"
#include "sed/level/geometry.hpp"
#include "ui/navigation/outliner_types.hpp"
#include <QInputDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QMap>
#include <QDockWidget>

namespace sed
{
void MainWindow::setupWorkspaceActions(QMenu* edit, QMenu* view)
{
    auto* preset = view->addAction(tr("JED / NJED Workspace"));
    preset->setObjectName("view.jed_workspace");
    connect(preset, &QAction::triggered, this, [this]()
    {
        if ( !glView_ ) return;
        auto settings = settingsStore();
        const auto palette = render::originalSedViewportColorPalette();
        settings.setValue(render::kViewportColorThemeSetting, render::kViewportColorThemeOriginalSed);
        render::saveViewportColorPalette(settings, QStringLiteral("viewport/colors"), palette);
        settings.beginGroup("keymap/shortcuts");
        settings.setValue("viewport.view_align_to_grid", QStringList{"F2"});
        settings.setValue("viewport.grid_align_to_view", QStringList{"F3", "Num+0"});
        settings.endGroup();
        settings.sync();
        loadViewportPreferences();
        initializeKeymapBindings();
        glView_->setRenderPipeline(GlView::RenderPipeline::Editor);
        glView_->setCameraProjectionType(app::ViewportCamera::ProjectionType::Orthographic);
        glView_->orbitViewTop();
        setEditMode(EditMode::sector);
        if ( outlinerDock_ )
        {
            addDockWidget(Qt::LeftDockWidgetArea, outlinerDock_);
            outlinerDock_->show();
        }
        if ( auto* checks = findChild<QDockWidget*>("checksDock") )
        {
            addDockWidget(Qt::BottomDockWidgetArea, checks);
            checks->show();
        }
        glView_->setFocus();
        statusBar()->showMessage(tr("JED workspace: top view, sector selection, F2 View to Grid, F3 Grid to View."), 7000);
    });
    auto* sets = view->addMenu(tr("Selection Sets"));
    sets->setObjectName("view.selection_sets");
    auto* store = sets->addAction(tr("Save Selection As..."));
    store->setObjectName("selection.store_set");
    connect(store, &QAction::triggered, this, [this]()
    {
        const auto scope = ui::inspectSelection(document_.level(), document_.selection());
        if ( scope.identity.isEmpty() ) { statusBar()->showMessage(tr("Select geometry or objects first."),5000); return; }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Selection"), tr("Name"), QLineEdit::Normal, {}, &ok).trimmed();
        if ( !ok || name.isEmpty() ) return;
        auto records = property("red.selectionSets").toMap();
        if ( name.size() > 120 || (!records.contains(name) && records.size() >= 64) )
        { statusBar()->showMessage(tr("Selection sets allow 64 names of up to 120 characters."),5000); return; }
        records.insert(name, QVariantList{scope.identity, ui::recovery::serializeSelection(document_.selection())});
        if ( QCborValue::fromVariant(records).toCbor().size() > 2*1024*1024 )
        { statusBar()->showMessage(tr("The saved selection sets exceed the workspace size limit."),5000); return; }
        setProperty("red.selectionSets", records);
        scheduleRecoverySnapshot();
        updateEditActions();
    });
    auto* recall = sets->addMenu(tr("Recall Selection"));
    recall->setObjectName("selection.recall_set");
    connect(recall, &QMenu::aboutToShow, this, [this, recall]()
    {
        recall->clear();
        const auto records = property("red.selectionSets").toMap();
        for (auto it = records.begin(); it != records.end(); ++it)
        {
            auto* action = recall->addAction(it.key());
            const auto info = it.value().toList();
            connect(action, &QAction::triggered, this, [this, info]()
            {
                if ( info.size()!=2 ) return;
                const auto saved = ui::recovery::deserializeSelection(info[1].toByteArray());
                if ( !saved || ui::inspectSelection(document_.level(), *saved).identity != info[0].toByteArray() )
                { statusBar()->showMessage(tr("The saved selection no longer identifies the same geometry or objects."),6000); return; }
                document_.selection() = *saved;
                glView_->setSelectionState(-1,-1,-1,-1,document_.selection());
                syncOutlinerSelectionOnly();
                rebuildProperties();
                updateEditActions();
            });
        }
        if ( records.isEmpty() ) recall->addAction(tr("Save a selection first"))->setEnabled(false);
    });
    auto* focus = sets->addAction(tr("Focus Selection"));
    focus->setObjectName("selection.focus");
    connect(focus, &QAction::triggered, this, [this]()
    {
        const auto s=ui::inspectSelection(document_.level(),document_.selection());
        if ( s.hasBounds ) frameBoundsWithNavigationPrefs(s.minimum,s.maximum,false);
        else statusBar()->showMessage(tr("The selection has no world position."),5000);
    });
    auto* isolate = sets->addAction(tr("Isolate Selected Sector Geometry"));
    isolate->setObjectName("selection.isolate");
    connect(isolate, &QAction::triggered, this, [this]()
    {
        const auto scope=ui::inspectSelection(document_.level(),document_.selection());
        if ( scope.sectorIds.empty() )
        { statusBar()->showMessage(tr("Select a sector, surface or placed Thing."),5000); return; }
        glView_->setIsolatedSectorIds(scope.sectorIds);
        if ( scope.hasBounds ) frameBoundsWithNavigationPrefs(scope.minimum,scope.maximum,false);
    });
    auto* show = sets->addAction(tr("Show All Sector Geometry"));
    show->setObjectName("selection.clear_isolation");
    connect(show, &QAction::triggered, this, [this](){ glView_->setIsolatedSectorIds({}); });

    auto* repair = edit->addAction(tr("Preview Adjoin Repair..."));
    repair->setObjectName("edit.preview_adjoin_repair");
    if ( auto* checks = findChild<QDockWidget*>("checksDock") )
    {
        auto* button = new QPushButton(tr("Preview Adjoin Repair..."), checks->widget());
        button->setObjectName("checks.previewAdjoinRepair");
        checks->widget()->layout()->addWidget(button);
        connect(button, &QPushButton::clicked, repair, &QAction::trigger);
    }
    connect(repair, &QAction::triggered, this, [this]()
    {
        if (!documentEditingAllowed()) return;
        auto* issue = consistencyChecks_ ? consistencyChecks_->currentItem() : nullptr;
        SurfaceRef source;
        if ( issue && static_cast<OutlinerKind>(issue->data(0,kOutlinerKindRole).toInt()) == OutlinerKind::surface )
        {
            source.sector = issue->data(0,kOutlinerSectorRole).toInt();
            const auto face = issue->data(0,kOutlinerSurfaceRole);
            source.surface = face.isValid() ? face.toInt() : issue->data(0,kOutlinerIndexRole).toInt();
        }
        else if ( document_.selection().surfaces.size() == 1 ) source = *document_.selection().surfaces.begin();
        const auto dest = ui::missingAdjoinBacklink(document_.level(),source);
        if (!dest)
        {
            statusBar()->showMessage(tr("Select a surface with a missing return link and an unclaimed matching opposite face."),7000);
            return;
        }
        if (isLayerLocked(document_.level().sectors[source.sector].layer) ||
            isLayerLocked(document_.level().sectors[dest->sector].layer)) return;
        editor::Selection target; target.surfaces.insert(source);
        const auto scope = ui::inspectSelection(document_.level(),target);
        if (scope.hasBounds) frameBoundsWithNavigationPrefs(scope.minimum,scope.maximum,false);
        const auto epoch = documentLifetimeGeneration_;
        const auto signature = document_.undoSignature();
        const auto confirm = QMessageBox::question(this,tr("Preview Adjoin Repair"),
            tr("Restore the return link from sector %1 face %2 to sector %3 face %4? Both sides keep their flags, materials, texture coordinates and lighting.")
                .arg(dest->sector).arg(dest->surface).arg(source.sector).arg(source.surface));
        if (confirm != QMessageBox::Yes || epoch != documentLifetimeGeneration_ ||
            signature != document_.undoSignature() || !documentEditingAllowed() ||
            ui::missingAdjoinBacklink(document_.level(),source) != dest ||
            isLayerLocked(document_.level().sectors[source.sector].layer) ||
            isLayerLocked(document_.level().sectors[dest->sector].layer)) return;
        if (document_.applyUndoable("Surface: Repaired adjoin return link",editor::DocumentDirty::LevelGeometry,
            [&](editor::DocumentState& state){ return level::levelSysAdjoinSurfaces(state.level,source,*dest,false); }))
        {
            glView_->notifyLevelGeometryChanged();
            scheduleRebuildChecks(editor::DocumentDirty::LevelGeometry);
            rebuildProperties();
            updateEditActions();
        }
    });

    auto* repeat = edit->addAction(tr("Repeat Last Operation"));
    repeat->setObjectName("edit.repeat_last_operation");
    repeat->setEnabled(false);
    connect(repeat, &QAction::triggered, this, [this]()
    {
        if ( property("red.repeatLifetime").toULongLong() != documentLifetimeGeneration_ ||
             property("red.repeatSignature").toULongLong() != document_.undoSignature() ) return;
        auto* original = findChild<QAction*>(property("red.repeatAction").toString());
        if ( !original || !original->isEnabled() ) return;
        original->setProperty("red.replayArguments", property("red.repeatArguments"));
        original->trigger();
        original->setProperty("red.replayArguments",{});
    });
}

void MainWindow::rememberRepeatOperation(QAction* action, QVariantMap arguments)
{
    if ( !action ) return;
    setProperty("red.repeatLifetime", QVariant::fromValue<qulonglong>(documentLifetimeGeneration_));
    setProperty("red.repeatSignature", QVariant::fromValue<qulonglong>(document_.undoSignature()));
    setProperty("red.repeatAction", action->objectName());
    setProperty("red.repeatArguments", arguments);
    if ( auto* repeat = findChild<QAction*>("edit.repeat_last_operation") )
    {
        repeat->setText(tr("Repeat %1").arg(QString(action->text()).remove('&').remove("...")));
        repeat->setEnabled(true);
    }
}
} // namespace sed
