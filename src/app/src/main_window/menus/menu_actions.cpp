#include "main_window/common.hpp"
#include "ui/diagnostics/installation_check_dialog.hpp"

#include "ui/components/about_dialog.hpp"
#include "ui/resources/resource_authoring_editor.hpp"
#include "ui/resources/model_lighting_dialog.hpp"
#include "ui/resources/model_import_dialog.hpp"
#include "ui/resources/package_authoring_dialog.hpp"
#include "ui/resources/material_authoring_dialog.hpp"
#include "ui/resources/dependency_inspector_dialog.hpp"
#include "ui/project/engine_capture_dialog.hpp"
#include "ui/shortcuts/key_sequence_display.hpp"
#include "plugins/jed_plugin_manager.hpp"
#include "sed/plugins/jed_plugin_abi.hpp"

#include <QMessageBox>

namespace sed
{

void MainWindow::setupMenus()
{
    auto* fileMenu = ui::shortcuts::addKeySequenceDisplayMenu(menuBar(), "&File");
    auto* editMenu = ui::shortcuts::addKeySequenceDisplayMenu(menuBar(), "&Edit");
    auto* mapMenu = ui::shortcuts::addKeySequenceDisplayMenu(menuBar(), "&Map");
    auto* viewMenu = ui::shortcuts::addKeySequenceDisplayMenu(menuBar(), "&View");
    auto* toolsMenu = ui::shortcuts::addKeySequenceDisplayMenu(menuBar(), "&Tools");
    auto* pluginsMenu = ui::shortcuts::addKeySequenceDisplayMenu(menuBar(), "&Plugins");
    pluginsMenu->setObjectName(QStringLiteral("plugins"));
    auto* helpMenu = ui::shortcuts::addKeySequenceDisplayMenu(menuBar(), "&Help");

    // Step 1: the File menu owns project/open/save/resource-root workflows.
    setupFileMenu(fileMenu);

    // Step 2: the Edit menu core owns undo/redo/delete and global edit commands.
    setupEditCoreActions(editMenu);

    // Step 3: the Map menu owns level-wide tools; the campaign editor is appended to Edit.
    setupMapMenu(mapMenu, editMenu);

    // Step 4: the Edit entity submenus own sector/surface/thing/light/cog commands.
    setupEditEntityMenus(editMenu);

    auto* resourcesMenu = ui::shortcuts::addKeySequenceDisplayMenu(editMenu, "&Resources");
    resourcesMenu->setObjectName("edit.resources");
    auto addLegacyResourceAuthoringAction = [this, resourcesMenu](
        const QString& title,
        const char* objectName,
        const char* toolTip,
        const ui::resources::ResourceAuthoringKind kind)
    {
        auto* action = resourcesMenu->addAction(title);
        action->setObjectName(QString::fromLatin1(objectName));
        action->setToolTip(QString::fromLatin1(toolTip));
        connect(action, &QAction::triggered, this, [this, kind]()
            {
                openResourceEditor(kind, 0);
            });
    };
    addLegacyResourceAuthoringAction(
        "&Sprite (.spr)...",
        "edit.resources.sprite_spr",
        "Create or edit a SPR sprite resource with an isolated 3D preview",
        ui::resources::ResourceAuthoringKind::sprite);
    addLegacyResourceAuthoringAction(
        "&Sound Class (.snd)...",
        "edit.resources.sound_class_snd",
        "Create or edit ordered SND sound-class playback rows",
        ui::resources::ResourceAuthoringKind::soundClass);
    addLegacyResourceAuthoringAction(
        "&String Table (.uni)...",
        "edit.resources.string_table_uni",
        "Create or edit a UNI COG string table",
        ui::resources::ResourceAuthoringKind::stringTable);

    const auto addResourceEditorAction = [this, toolsMenu](
        const QString& label,
        const char* objectName,
        const ui::resources::ResourceAuthoringKind kind)
    {
        QAction* const action = toolsMenu->addAction(label);
        action->setIcon(ui::makeViewportIcon(kind == ui::resources::ResourceAuthoringKind::sprite
            ? ui::ViewportIcon::resourceSprite : kind == ui::resources::ResourceAuthoringKind::soundClass
                ? ui::ViewportIcon::sound : ui::ViewportIcon::actionWhitespace));
        action->setObjectName(QString::fromLatin1(objectName));
        action->setToolTip(QStringLiteral("Open %1")
            .arg(label));
        connect(action, &QAction::triggered, this, [this, kind]()
            {
                openResourceEditor(kind, -1);
            });
    };
    addResourceEditorAction(
        QStringLiteral("Sprite Editor"),
        "tools.resource_editors.sprite_spr.new",
        ui::resources::ResourceAuthoringKind::sprite);
    addResourceEditorAction(
        QStringLiteral("Sound Class Editor"),
        "tools.resource_editors.sound_class_snd.new",
        ui::resources::ResourceAuthoringKind::soundClass);
    addResourceEditorAction(
        QStringLiteral("UNI Editor"),
        "tools.resource_editors.string_table_uni.new",
        ui::resources::ResourceAuthoringKind::stringTable);

    if ( cogScriptEditorDock_ )
    {
        QAction* const action = cogScriptEditorDock_->toggleViewAction();
        action->setText(QStringLiteral("COG Script Editor"));
        action->setObjectName(QStringLiteral("tools.cog_script_editor"));
        action->setIcon(ui::makeViewportIcon(ui::ViewportIcon::outlinerCogs));
        action->setToolTip(QStringLiteral("Open COG Script Editor"));
        toolsMenu->addAction(action);
    }

    // Phase 4 wiring: COG preflight/bindings, workbench, ARCH inspector.
    // All four call the shared core prepare/apply steps (no private path).
    auto addWiringAction = [this, toolsMenu](
        const QString& label, const char* objectName, auto&& fn)
    {
        QAction* const action = toolsMenu->addAction(label);
        action->setObjectName(QString::fromLatin1(objectName));
        connect(action, &QAction::triggered, this, fn);
    };
    addWiringAction(QStringLiteral("COG Preflight (selected COG)"),
        "tools.cog_preflight", [this]() { showCogPreflightForSelectedCog(); });
    addWiringAction(QStringLiteral("COG Bindings (selected COG)"),
        "tools.cog_bindings", [this]() { showPlacedCogBindingsForSelectedCog(); });
    addWiringAction(QStringLiteral("Workbench"),
        "tools.workbench", [this]() { showWorkbenchDialog(); });
    addWiringAction(QStringLiteral("ARCH Inspector"),
        "tools.arch_inspector", [this]() { showArchInspectorDialog(); });
    addWiringAction(QStringLiteral("Import Model…"), "tools.model_import", [this]()
    {
        ui::resources::ModelImportDialog dialog({}, this);
        dialog.exec();
    });
    addWiringAction(QStringLiteral("Model Lighting"), "tools.model_lighting", [this]()
    {
        const auto lifetime = documentLifetimeGeneration_;
        const auto contextGeneration = resourceEditorDocumentContextGeneration_;
        const auto resourceIdentity = resources_.previewCacheIdentity();
        const auto project = document_.level().kind;
        const QPointer<MainWindow> owner(this);
        ui::resources::ModelLightingDialog dialog({project,
            std::make_shared<const assets::ResourceLocator>(resources_), document_.level().masterCMP,
            [owner, lifetime, contextGeneration, resourceIdentity, project]()
            {
                return owner && owner->documentLifetimeGeneration_ == lifetime
                    && owner->resourceEditorDocumentContextGeneration_ == contextGeneration
                    && owner->resources_.previewCacheIdentity() == resourceIdentity
                    && owner->document_.level().kind == project;
            }}, this);
        dialog.exec();
    });

    addWiringAction(QStringLiteral("Material Authoring"), "tools.material_authoring", [this]()
    {
        try
        {
            const auto lifetime = documentLifetimeGeneration_;
            const auto generation = resourceEditorDocumentContextGeneration_;
            const auto identity = resources_.previewCacheIdentity();
            const auto project = document_.level().kind;
            const auto paletteName = document_.level().masterCMP;
            const auto palette = resources_.tryReadBinary(paletteName, 1024 * 1024);
            const QPointer<MainWindow> owner(this);
            ui::resources::MaterialAuthoringDialog::Context context;
            context.initialDirectory = QString::fromStdWString(currentPath_.parent_path().wstring());
            context.paletteName = QString::fromStdString(paletteName);
            if ( palette )
            {
                context.paletteBytes = QByteArray(reinterpret_cast<const char*>(palette->data()),
                    static_cast<qsizetype>(palette->size()));
            }
            context.isCurrent = [owner, lifetime, generation, identity, project, paletteName, palette]()
            {
                if ( !owner || owner->documentLifetimeGeneration_ != lifetime
                    || owner->resourceEditorDocumentContextGeneration_ != generation
                    || owner->resources_.previewCacheIdentity() != identity
                    || owner->document_.level().kind != project
                    || owner->document_.level().masterCMP != paletteName )
                {
                    return false;
                }
                try
                {
                    return !palette || owner->resources_.tryReadBinary(paletteName, 1024 * 1024) == palette;
                }
                catch ( const std::exception& )
                {
                    return false;
                }
            };
            ui::resources::MaterialAuthoringDialog dialog(std::move(context), this);
            dialog.exec();
        }
        catch ( const std::exception& error )
        {
            QMessageBox::warning(this, tr("Material Authoring"), QString::fromUtf8(error.what()));
        }
    });

    addWiringAction(QStringLiteral("Dependencies…"), "tools.dependencies", [this]()
    {
        const auto lifetime = documentLifetimeGeneration_;
        const auto generation = resourceEditorDocumentContextGeneration_;
        const auto identity = resources_.previewCacheIdentity();
        const auto project = document_.level().kind;
        const QPointer<MainWindow> owner(this);
        ui::resources::DependencyInspectorDialog::Context context;
        context.initialDirectory = QString::fromStdWString(currentPath_.parent_path().wstring());
        context.isCurrent = [owner, lifetime, generation, identity, project]()
        {
            return owner && owner->documentLifetimeGeneration_ == lifetime
                && owner->resourceEditorDocumentContextGeneration_ == generation
                && owner->resources_.previewCacheIdentity() == identity
                && owner->document_.level().kind == project;
        };
        ui::resources::DependencyInspectorDialog dialog(std::move(context), this);
        dialog.exec();
    });

    addWiringAction(QStringLiteral("Engine Capture…"), "tools.engine_capture", [this]()
    {
        ui::project::EngineCaptureDialog::Context context;
        context.initialDirectory = QString::fromStdWString(currentPath_.parent_path().wstring());
        ui::project::EngineCaptureDialog dialog(std::move(context), this);
        dialog.exec();
    });

    addWiringAction(QStringLiteral("Archive and Package Authoring…"), "tools.package_authoring", [this]()
    {
        const auto lifetime = documentLifetimeGeneration_;
        const auto generation = resourceEditorDocumentContextGeneration_;
        const auto identity = resources_.previewCacheIdentity();
        const auto project = document_.level().kind;
        const QPointer<MainWindow> owner(this);
        ui::resources::PackageAuthoringDialog::Context context;
        context.initialDirectory = QString::fromStdWString(currentPath_.parent_path().wstring());
        context.isCurrent = [owner, lifetime, generation, identity, project]()
        {
            return owner && owner->documentLifetimeGeneration_ == lifetime
                && owner->resourceEditorDocumentContextGeneration_ == generation
                && owner->resources_.previewCacheIdentity() == identity
                && owner->document_.level().kind == project;
        };
        ui::resources::PackageAuthoringDialog dialog(std::move(context), this);
        dialog.exec();
    });

    // Step 5: the View menu owns viewport render modes, dock windows, and viewport shortcuts.
    setupViewMenu(viewMenu);

    // F-14 (adapted): the Help menu points at the sed-update tool. The
    // editor never downloads or replaces itself in place.
    auto* updateAction = helpMenu->addAction("Check for &Updates (sed-update)...");
    updateAction->setObjectName(QStringLiteral("help.check_for_updates"));
    connect(updateAction, &QAction::triggered, this, [this]()
        {
            QMessageBox::information(this, QStringLiteral("Check for Updates"),
                QStringLiteral("Updates are staged beside this installation by the "
                    "sed-update tool and activated as a separate step; this editor "
                    "is never overwritten in place. The update feed is not "
                    "independently signed. See docs/port/UPDATER.md."));
        });

    // Step 6: Help owns application identity and project links.
    auto* installationAction = helpMenu->addAction("Check &Installation…");
    installationAction->setObjectName(QStringLiteral("help.check_installation"));
    connect(installationAction, &QAction::triggered, this, [this]()
    {
        ui::diagnostics::InstallationCheckDialog dialog({}, this);
        dialog.exec();
    });
    auto* aboutAction = helpMenu->addAction("&About RED");
    aboutAction->setObjectName(QStringLiteral("help.about_sed"));
    connect(aboutAction, &QAction::triggered, this, [this]()
        {
            ui::AboutDialog dialog(this);
            dialog.exec();
        });

    // Step 7: the Plugins menu runs legacy JED plug-ins from <RED folder>/plugins.
    if ( startupPurpose() == StartupPurpose::installationCheck )
    {
        return;
    }
    namespace jed = plugins::jed;
    app_plugins::PluginEditorServices pluginServices{};
    pluginServices.level = [this]() -> const Level& { return document_.level(); };
    pluginServices.selection = [this]() -> const editor::Selection& { return document_.selection(); };
    pluginServices.liveSelection = [this]() -> editor::Selection& { return document_.selection(); };
    pluginServices.selectionChanged = [this]()
    {
        if ( glView_ )
        {
            glView_->setHighlightedSelection(document_.selection());
        }
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
    };
    pluginServices.documentSignature = [this]() { return document_.undoSignature(); };
    pluginServices.commit = [this](const std::string& label, editor::DocumentState&& next) -> bool
    {
        if ( !documentEditingAllowed() )
        {
            logMessage(MessageSeverity::warning,
                QStringLiteral("A plug-in edit was discarded because the level can't be edited right now."));
            return false;
        }
        const bool applied = document_.applyUndoable(label, editor::DocumentDirty::All,
            [&](editor::DocumentState& state) -> bool
            {
                state = std::move(next);
                return true;
            });
        if ( !applied )
        {
            return false;
        }
        invalidateSurfaceSelectionContextForTopologyEdit();
        normalizeLayerVisualState();
        refreshThingPlacementLayerControl();
        if ( glView_ )
        {
            glView_->cancelAllStagedCleaveWorkspaces();
            glView_->notifyLevelGeometryChanged();
            glView_->notifyThingsChanged();
            glView_->notifyLightsChanged();
            glView_->setHighlightedSelection(document_.selection());
        }
        syncCogScriptWatches();
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
        scheduleRebuildChecks(editor::DocumentDirty::All);
        return true;
    };
    pluginServices.mapMode = [this]() -> std::int32_t
    {
        switch ( editMode_ )
        {
        case EditMode::surface:
            if ( surfaceSelectionMode_ == SurfaceSelectionMode::vertex )
            {
                return jed::MM_VX;
            }
            return surfaceSelectionMode_ == SurfaceSelectionMode::edge ? jed::MM_ED : jed::MM_SF;
        case EditMode::thing:
            return jed::MM_TH;
        case EditMode::light:
            return jed::MM_LT;
        case EditMode::frame:
            return jed::MM_FR;
        case EditMode::none:
        case EditMode::sector:
            break;
        }
        return jed::MM_SC;
    };
    pluginServices.setMapMode = [this](const std::int32_t mode)
    {
        switch ( mode )
        {
        case jed::MM_SC:
            setEditMode(EditMode::sector);
            break;
        case jed::MM_SF:
        case jed::MM_VX:
        case jed::MM_ED:
            setEditMode(EditMode::surface);
            setSurfaceSelectionMode(mode == jed::MM_VX ? SurfaceSelectionMode::vertex
                : mode == jed::MM_ED ? SurfaceSelectionMode::edge : SurfaceSelectionMode::face);
            break;
        case jed::MM_TH:
            setEditMode(EditMode::thing);
            break;
        case jed::MM_LT:
            setEditMode(EditMode::light);
            break;
        case jed::MM_FR:
            setEditMode(EditMode::frame);
            break;
        default:
            break;
        }
    };
    pluginServices.cursor = [this]() -> plugins::JedCursor
    {
        plugins::JedCursor cursor{};
        if ( glView_ )
        {
            cursor.sector = glView_->activeSurfaceSectorIndex() >= 0 && editMode_ == EditMode::surface
                ? glView_->activeSurfaceSectorIndex() : glView_->activeSectorIndex();
            cursor.surface = glView_->activeSurfaceIndex();
            cursor.thing = glView_->activeThingIndex();
            cursor.light = glView_->activeLightIndex();
        }
        return cursor;
    };
    pluginServices.setCursor = [this](const plugins::JedCursor& cursor)
    {
        if ( !glView_ )
        {
            return;
        }
        const Level& level = document_.level();
        const auto inRange = [](const std::int32_t index, const std::size_t size)
        {
            return index >= 0 && static_cast<std::size_t>(index) < size;
        };
        switch ( editMode_ )
        {
        case EditMode::sector:
            if ( inRange(cursor.sector, level.sectors.size()) )
            {
                glView_->setSelectedSector(cursor.sector);
            }
            break;
        case EditMode::surface:
            if ( inRange(cursor.sector, level.sectors.size())
                && inRange(cursor.surface, level.sectors[static_cast<std::size_t>(cursor.sector)].surfaces.size()) )
            {
                glView_->setSelectedSurface(cursor.sector, cursor.surface);
            }
            break;
        case EditMode::thing:
            if ( inRange(cursor.thing, level.things.size()) )
            {
                glView_->setSelectedThing(cursor.thing);
            }
            break;
        case EditMode::light:
            if ( inRange(cursor.light, level.lights.size()) )
            {
                glView_->setSelectedLight(cursor.light);
            }
            break;
        case EditMode::none:
        case EditMode::frame:
            break;
        }
        rebuildProperties();
        updateEditActions();
    };
    pluginServices.message = [this](const plugins::JedMessageType type, const QString& text)
    {
        const MessageSeverity severity = type == plugins::JedMessageType::Error ? MessageSeverity::error
            : type == plugins::JedMessageType::Warning ? MessageSeverity::warning : MessageSeverity::info;
        logMessage(severity, text);
    };
    pluginServices.readGameFile = [this](const std::string& name)
    {
        return resources_.tryReadBinary(name, app_plugins::JedPluginManager::kMaximumPluginFileBytes);
    };
    pluginServices.cogScriptSymbols = [this](const std::string& name) -> std::optional<std::vector<CogValue>>
    {
        assets::CogSymbolLoadResult script = assets::loadCogSymbolsWithDefaultsResult(resources_, name, false);
        if ( !script.loaded() )
        {
            return std::nullopt;
        }
        return std::move(script.symbols);
    };
    pluginServices.jedString = [this](const std::int32_t what) -> QString
    {
        const auto directory = [](const QString& path)
        {
            return path.isEmpty() ? path : QDir::toNativeSeparators(QDir::cleanPath(path) + QLatin1Char('/'));
        };
        switch ( what )
        {
        case jed::js_ProjectDir:
            return currentPath_.empty() ? QString{}
                : directory(QString::fromStdWString(currentPath_.parent_path().wstring()));
        case jed::js_JEDDir:
            return directory(QCoreApplication::applicationDirPath());
        case jed::js_GameDir:
            return resources_.roots().empty() ? QString{}
                : directory(QString::fromStdWString(resources_.roots().front().wstring()));
        case jed::js_LevelFile:
            return QDir::toNativeSeparators(QString::fromStdWString(currentPath_.wstring()));
        default:
            return QString{};
        }
    };
    pluginServices.windowHandle = [this](const std::int32_t which) -> std::intptr_t
    {
        return which == jed::jw_Main ? static_cast<std::intptr_t>(winId()) : 0;
    };
    pluginServices.isLayerVisible = [this](const std::int32_t layer)
    {
        return layer < 0 || static_cast<std::size_t>(layer) >= layerVisible_.size()
            || layerVisible_[static_cast<std::size_t>(layer)];
    };
    auto* pluginManager = new app_plugins::JedPluginManager(std::move(pluginServices), this);
    pluginManager->setPluginDirectory(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins")));
    {
        QSettings settings = settingsStore();
        pluginManager->setRunOutOfProcess(settings.value(QStringLiteral("plugins/run_out_of_process"), false).toBool());
    }
    pluginManager->populateMenu(pluginsMenu);
}

} // namespace sed
