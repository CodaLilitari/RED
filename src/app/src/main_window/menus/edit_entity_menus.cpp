#include "main_window/common.hpp"
#include "ui/shortcuts/key_sequence_display.hpp"

#include "app_logging.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/material_dimensions.hpp"
#include "sed/level/component_rebuild.hpp"
#include "sed/level/door.hpp"
#include "sed/level/recipe_requests.hpp"
#include "sed/level/recipes.hpp"
#include "sed/level/surface_flag_repair.hpp"
#include "sed/text.hpp"
#include "thing/thing_resource_utils.hpp"
#include "ui/checks/level_check_utils.hpp"
#include "ui/sector/sector_import_failure_dialog.hpp"
#include "ui/sector/sector_import_utils.hpp"

#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <unordered_map>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QClipboard>
#include <QSaveFile>

namespace sed
{
namespace
{

QString flattenedImportLogField(QString value, const qsizetype maximumLength)
{
    for (qsizetype index = 0; index < value.size(); ++index)
    {
        const ushort code = value.at(index).unicode();
        if (code < 0x20u || code == 0x7fu)
        {
            value[index] = QLatin1Char(' ');
        }
    }
    value = value.trimmed();
    if (value.size() > maximumLength)
    {
        value.truncate(maximumLength - 3);
        value += QStringLiteral("...");
    }
    return value;
}

void showSectorExportFailure(
    QWidget* parent,
    const QString& filename,
    const QString& reason)
{
    qCWarning(logCore).noquote()
        << QStringLiteral("Sector export failed: %1: %2")
               .arg(flattenedImportLogField(filename, 2048),
                   flattenedImportLogField(reason, 4096));

    QMessageBox dialog(
        QMessageBox::Warning,
        QStringLiteral("Export Sector(s) failed"),
        reason,
        QMessageBox::Ok,
        parent);
    dialog.setObjectName(QStringLiteral("sectorExportFailureDialog"));
    dialog.setProperty(kMessageBoxLoggingSkipProperty, true);
    (void)dialog.exec();
}

} // namespace

void ui::sector::showSectorImportFailure(
    QWidget* parent,
    const QString& filename,
    const QString& summary,
    const QString& details)
{
    const QString logFilename = flattenedImportLogField(filename, 2048);
    const QString logSummary = flattenedImportLogField(summary, 4096);
    QString record = QStringLiteral("Sector import failed: %1: %2")
        .arg(logFilename, logSummary);
    if (!details.isEmpty())
    {
        QString detailLines = details;
        detailLines.replace(QStringLiteral("\r\n"), QStringLiteral(" | "));
        detailLines.replace(QLatin1Char('\r'), QStringLiteral(" | "));
        detailLines.replace(QLatin1Char('\n'), QStringLiteral(" | "));
        const QString logDetails =
            flattenedImportLogField(detailLines, 16384);
        record += QStringLiteral(" Details: %1").arg(logDetails);
    }
    qCWarning(logCore).noquote() << record;

    QMessageBox dialog(
        QMessageBox::Warning,
        QStringLiteral("Import Sector(s) failed"),
        summary,
        QMessageBox::Ok,
        parent);
    dialog.setObjectName(QStringLiteral("sectorImportFailureDialog"));
    dialog.setProperty(kMessageBoxLoggingSkipProperty, true);
    dialog.setInformativeText(
        QStringLiteral("Source: %1").arg(filename));
    if (!details.isEmpty())
    {
        dialog.setDetailedText(details);
    }
    (void)dialog.exec();
}

void MainWindow::setupEditEntityMenus(QMenu* editMenu)
{
    if (!editMenu)
    {
        return;
    }

    // Step 1: create the entity-specific Edit submenus.
    editMenu->addSeparator();

    auto* sectorMenu = ui::shortcuts::addKeySequenceDisplayMenu(editMenu, "S&ector");
    sectorMenu->setObjectName("edit.sector");

    auto* surfaceMenu = ui::shortcuts::addKeySequenceDisplayMenu(editMenu, "&Surface");
    surfaceMenu->setObjectName("edit.surface");

    auto* thingMenu = ui::shortcuts::addKeySequenceDisplayMenu(editMenu, "&Thing");
    thingMenu->setObjectName("edit.thing");

    auto* lightMenu = ui::shortcuts::addKeySequenceDisplayMenu(editMenu, "&Light");
    lightMenu->setObjectName("edit.light");

    auto* cogMenu = ui::shortcuts::addKeySequenceDisplayMenu(editMenu, "CO&G");
    cogMenu->setObjectName("edit.cog");

    // QAction cannot expose independent enabled state in several menus.  Keep
    // Delete's shortcut on one canonical action, while each entity menu gets
    // a shortcut-free proxy which calls the same defensively guarded slot.
    const auto makeDeleteMenuAction = [this](const char* objectName)
    {
        auto* action = new QAction(
            deleteAction_ ? deleteAction_->icon() : QIcon{}, "&Delete", this);
        action->setObjectName(QString::fromLatin1(objectName));
        action->setToolTip(deleteAction_ ? deleteAction_->toolTip()
                                         : QStringLiteral("Delete the selection"));
        connect(action, &QAction::triggered, this, &MainWindow::deleteSelection);
        return action;
    };
    sectorDeleteMenuAction_ = makeDeleteMenuAction("edit.sector.delete");
    surfaceDeleteMenuAction_ = makeDeleteMenuAction("edit.surface.delete");
    thingDeleteMenuAction_ = makeDeleteMenuAction("edit.thing.delete");
    lightDeleteMenuAction_ = makeDeleteMenuAction("edit.light.delete");
    cogDeleteMenuAction_ = makeDeleteMenuAction("edit.cog.delete");

    auto* newCogScriptAction = new QAction("New COG &Script...", this);
    newCogScriptAction->setObjectName("edit.new_cog_script");
    newCogScriptAction->setToolTip(
        "Open a new unsaved COG script draft and add its placed COG to the level");
    connect(newCogScriptAction, &QAction::triggered, this, [this]()
        {
            if (!documentEditingAllowed())
            {
                return;
            }
            createNewCogScript();
        });
    cogMenu->addAction(newCogScriptAction);
    cogMenu->addSeparator();

    auto* transformMenu = ui::shortcuts::addKeySequenceDisplayMenu(editMenu, "T&ransform");
    transformMenu->setObjectName("edit.transform");

    addCubeAction_ = new QAction("Add &Sector", this);
    addCubeAction_->setObjectName("edit.add_cube");
    addCubeAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::actionAddCube));
    addCubeAction_->setShortcut(QKeySequence(Qt::Key_A));
    addCubeAction_->setShortcutContext(Qt::WidgetShortcut);
    addCubeAction_->setAutoRepeat(false);
    addCubeAction_->setCheckable(true);
    addCubeAction_->setToolTip("Add a sector primitive or saved shape");
    connect(addCubeAction_, &QAction::triggered, this, [this]()
        {
            const PlacementMode newMode = (placementMode_ == PlacementMode::cube) ? PlacementMode::none : PlacementMode::cube;
            setPlacementMode(newMode);
        });
    sectorMenu->addAction(addCubeAction_);
    sectorMenu->addAction(sectorDeleteMenuAction_);
    sectorMenu->addSeparator();

    importSectorAction_ = new QAction("&Import Sector(s)...", this);
    importSectorAction_->setObjectName("edit.import_sector");
    importSectorAction_->setToolTip(
        "Import one or more convex sectors from a 3DO model or Wavefront OBJ file");
    connect(importSectorAction_, &QAction::triggered, this, [this]()
        {
            if (editMode_ != EditMode::sector
                || (glView_ && glView_->renderPipeline() == GlView::RenderPipeline::GamePreview))
            {
                return;
            }
            const QString filename = QFileDialog::getOpenFileName(
                this,
                "Import Sector(s)",
                fileDialogStartLocation(FileDialogHistoryGroup::import_),
                "Sector geometry (*.3do *.obj);;3DO models (*.3do);;Wavefront OBJ (*.obj)");
            if (filename.isEmpty())
            {
                return;
            }
            rememberFileDialogSelection(
                FileDialogHistoryGroup::import_, filename);

            const std::filesystem::path path(filename.toStdWString());
            const std::string extension =
                sed::text::lowerAscii(path.extension().string());

            QDialog options(this);
            options.setObjectName("sectorImportOptionsDialog");
            options.setWindowTitle("Import Sector(s)");
            options.setWindowFlag(Qt::WindowMinimizeButtonHint, false);
            options.setWindowFlag(Qt::WindowMaximizeButtonHint, false);
            auto* optionsLayout = new QVBoxLayout(&options);
            optionsLayout->addWidget(new QLabel(
                "Choose how the selected geometry should be imported.",
                &options));
            auto* splitMeshes = new QCheckBox(
                "Split objects/nodes into connected sectors", &options);
            splitMeshes->setObjectName("sectorImportSplitMeshes");
            splitMeshes->setChecked(true);
            splitMeshes->setToolTip(
                "Create one sector per non-empty OBJ object/group or 3DO hierarchy node, then adjoin matching faces.");
            optionsLayout->addWidget(splitMeshes);

            ui::sector::ObjSectorImportOptions objImportOptions{};
            const std::shared_ptr<const assets::ResourceLocator>
                importResources = resourceLocatorSnapshot_
                    ? resourceLocatorSnapshot_
                    : std::make_shared<const assets::ResourceLocator>(
                        resources_);
            auto textureSizeCache = std::make_shared<
                std::unordered_map<std::string,
                    std::optional<level::UvTextureDimensions>>>();
            const ui::sector::SectorTextureDimensionsResolver
                resolveTextureDimensions =
                [importResources, textureSizeCache](
                    const std::string_view material)
                    -> std::optional<level::UvTextureDimensions>
            {
                const std::string trimmed = sed::text::trim(material);
                if (trimmed.empty()
                    || sed::text::iequalsAscii(trimmed, "none")
                    || sed::text::iequalsAscii(trimmed, "transparent"))
                {
                    return std::nullopt;
                }
                const std::string key = sed::text::upperAscii(trimmed);
                if (const auto found = textureSizeCache->find(key);
                    found != textureSizeCache->end())
                {
                    return found->second;
                }

                const std::optional<level::UvTextureDimensions> size =
                    assets::resolveMaterialTextureDimensions(
                        *importResources, trimmed);
                textureSizeCache->emplace(key, size);
                return size;
            };
            objImportOptions.materialTextureSize =
                [resolveTextureDimensions](const std::string_view material)
                    -> std::optional<
                        ui::sector::ObjSectorImportOptions::TextureSize>
            {
                const std::optional<level::UvTextureDimensions> size =
                    resolveTextureDimensions(material);
                if (!size)
                {
                    return std::nullopt;
                }
                return ui::sector::ObjSectorImportOptions::TextureSize{
                    static_cast<float>(size->width),
                    static_cast<float>(size->height) };
            };
            auto* convertObjCoordinates = new QCheckBox(
                "Transform OBJ coordinate system to SED coordinate system", &options);
            convertObjCoordinates->setObjectName(
                "sectorImportConvertObjYUpToZUp");
            convertObjCoordinates->setChecked(
                objImportOptions.convertYUpToSithZUp);
            convertObjCoordinates->setToolTip(
                "Rotate conventional OBJ geometry into the SED/Sith coordinate system without changing UVs.");
            convertObjCoordinates->setVisible(extension == ".obj");
            optionsLayout->addWidget(convertObjCoordinates);

            auto* optionButtons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &options);
            optionButtons->setObjectName("sectorImportOptionsButtons");
            if (QPushButton* importButton = optionButtons->button(
                    QDialogButtonBox::Ok))
            {
                importButton->setText("Import");
            }
            connect(optionButtons, &QDialogButtonBox::accepted,
                &options, &QDialog::accept);
            connect(optionButtons, &QDialogButtonBox::rejected,
                &options, &QDialog::reject);
            optionsLayout->addWidget(optionButtons);
            if (options.exec() != QDialog::Accepted)
            {
                return;
            }
            objImportOptions.convertYUpToSithZUp =
                convertObjCoordinates->isChecked();

            ui::sector::ImportedSectorMeshResult loaded{};
            try
            {
                if (extension == ".3do")
                {
                    assets::Model3doLoadOptions importOptions{};
                    importOptions.applyHierarchyTransforms = false;
                    loaded = ui::sector::importedSectorMeshesFrom3do(
                        assets::loadFrom3DO(path, importOptions));
                }
                else if (extension == ".obj")
                {
                    std::ifstream input(path);
                    if (!input)
                    {
                        loaded.error = "Unable to open the selected OBJ file.";
                    }
                    else
                    {
                        loaded = ui::sector::loadObjSectorMeshes(
                            input, objImportOptions);
                    }
                }
                else
                {
                    loaded.error = "Only 3DO and Wavefront OBJ files are supported.";
                }
            }
            catch (const std::exception& ex)
            {
                loaded.error = ex.what();
            }

            if (!loaded)
            {
                const QString message = QString::fromStdString(loaded.error);
                ui::sector::showSectorImportFailure(
                    this, filename, message);
                return;
            }

            std::vector<std::int32_t> importedSectors{};
            std::string importError{};
            std::string importDetails{};
            const bool importMultipleSectors =
                splitMeshes->isChecked() && loaded.meshes.size() > 1;
            bool applied = false;
            try
            {
                applied = document_.applyUndoable(
                    importMultipleSectors ? "Sectors: Imported" : "Sector: Imported",
                    editor::DocumentDirty::Structure | editor::DocumentDirty::LevelGeometry,
                    [&](editor::DocumentState& state) -> bool
                    {
                        const ui::sector::ImportedSectorBuildResult built =
                            ui::sector::appendImportedSectors(
                                state.level,
                                loaded.meshes,
                                splitMeshes->isChecked(),
                                ui::sector::ImportedSectorBuildOptions{
                                    resolveTextureDimensions });
                        if (!built)
                        {
                            importError = built.error;
                            importDetails = built.details;
                            return false;
                        }
                        const auto checkerIssues = ui::checks::collectConsistencyCheckIssues(
                            state.level,
                            ui::checks::defaultCheckRuleSelection(state.level.kind));
                        const auto importedContains = [&built](const int sector) noexcept
                        {
                            return std::find(
                                built.sectors.begin(), built.sectors.end(), sector)
                                != built.sectors.end();
                        };
                        const auto invalidImported = std::find_if(
                            checkerIssues.begin(), checkerIssues.end(),
                            [&importedContains](const ui::checks::CheckIssue& issue)
                            {
                                if (issue.severity != ui::checks::CheckSeverity::error)
                                {
                                    return false;
                                }
                                if (issue.navKind
                                    && *issue.navKind == OutlinerKind::sector
                                    && issue.navIndex)
                                {
                                    return importedContains(*issue.navIndex);
                                }
                                return issue.navSector
                                    && importedContains(*issue.navSector);
                            });
                        if (invalidImported != checkerIssues.end())
                        {
                            importError = invalidImported->message.toStdString();
                            return false;
                        }
                        importedSectors = built.sectors;
                        state.selection.clear();
                        state.selection.sectors.insert(
                            importedSectors.begin(), importedSectors.end());
                        return true;
                    });
            }
            catch (const std::exception& ex)
            {
                importError = ex.what();
            }
            if (!applied)
            {
                const QString message = QString::fromStdString(
                    importError.empty()
                        ? std::string(
                            "The imported geometry is not a valid convex sector.")
                        : importError);
                ui::sector::showSectorImportFailure(
                    this,
                    filename,
                    message,
                    QString::fromStdString(importDetails));
                return;
            }

            invalidateSurfaceSelectionContextForTopologyEdit();

            qInfo().noquote()
                << "Imported" << static_cast<qulonglong>(importedSectors.size())
                << "Sector(s) from" << filename;

            if (glView_)
            {
                glView_->notifyLevelGeometryChanged();
                const std::int32_t active = importedSectors.empty() ? -1 : importedSectors.front();
                glView_->setSelectionState(active, -1, -1, -1, document_.selection());
            }
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            scheduleRebuildChecks(
                editor::DocumentDirty::Structure | editor::DocumentDirty::LevelGeometry);
        });
    sectorMenu->addAction(importSectorAction_);

    exportSectorAction_ = new QAction("&Export Sector(s)...", this);
    exportSectorAction_->setObjectName("edit.export_sectors");
    exportSectorAction_->setToolTip(
        "Export the selected sectors as Wavefront OBJ or 3DO polygon geometry");
    connect(exportSectorAction_, &QAction::triggered, this, [this]()
        {
            std::vector<std::int32_t> selected(
                document_.selection().sectors.begin(),
                document_.selection().sectors.end());
            std::sort(selected.begin(), selected.end());
            if (selected.empty())
            {
                return;
            }

            QString selectedFilter = "Wavefront OBJ (*.obj)";
            QString filename = QFileDialog::getSaveFileName(
                this,
                "Export Sector(s)",
                fileDialogStartLocation(FileDialogHistoryGroup::export_),
                "Wavefront OBJ (*.obj);;3DO model (*.3do)",
                &selectedFilter);
            if (filename.isEmpty())
            {
                return;
            }
            rememberFileDialogSelection(
                FileDialogHistoryGroup::export_, filename);

            std::filesystem::path path(filename.toStdWString());
            std::string extension = sed::text::lowerAscii(path.extension().string());
            if (extension.empty())
            {
                extension = selectedFilter.contains("3DO", Qt::CaseInsensitive)
                    ? ".3do" : ".obj";
                filename += QString::fromStdString(extension);
                path = std::filesystem::path(filename.toStdWString());
            }

            ui::sector::ExportedSectorTextResult exported{};
            try
            {
                const std::shared_ptr<const assets::ResourceLocator>
                    exportResources = resourceLocatorSnapshot_
                        ? resourceLocatorSnapshot_
                        : std::make_shared<const assets::ResourceLocator>(
                            resources_);
                auto exportTextureSizeCache = std::make_shared<
                    std::unordered_map<std::string,
                        std::optional<level::UvTextureDimensions>>>();
                const ui::sector::SectorExportOptions exportOptions{
                    [exportResources, exportTextureSizeCache](
                        const std::string_view material)
                        -> std::optional<level::UvTextureDimensions>
                    {
                        const std::string trimmed = text::trim(material);
                        const std::string key = text::upperAscii(trimmed);
                        if (const auto found =
                                exportTextureSizeCache->find(key);
                            found != exportTextureSizeCache->end())
                        {
                            return found->second;
                        }
                        const std::optional<level::UvTextureDimensions> size =
                            assets::resolveMaterialTextureDimensions(
                                *exportResources, trimmed);
                        exportTextureSizeCache->emplace(key, size);
                        return size;
                    } };
                if (extension == ".obj")
                {
                    exported = ui::sector::exportSectorsToObj(
                        document_.level(), selected, exportOptions);
                }
                else if (extension == ".3do")
                {
                    exported = ui::sector::exportSectorsTo3do(
                        document_.level(), selected, exportOptions);
                }
                else
                {
                    exported.error = "Choose either the .obj or .3do file extension.";
                }
            }
            catch (const std::exception& ex)
            {
                exported.error = ex.what();
            }
            if (!exported)
            {
                const QString message = QString::fromStdString(exported.error);
                showSectorExportFailure(this, filename, message);
                return;
            }

            QSaveFile output(filename);
            if (!output.open(QIODevice::WriteOnly)
                || output.write(exported.text.data(),
                       static_cast<qint64>(exported.text.size()))
                    != static_cast<qint64>(exported.text.size())
                || !output.commit())
            {
                showSectorExportFailure(
                    this,
                    filename,
                    QStringLiteral(
                        "The export file could not be written atomically."));
                return;
            }

            qInfo().noquote()
                << "Exported" << static_cast<qulonglong>(selected.size())
                << "Sector(s) to" << filename;
        });
    sectorMenu->addAction(exportSectorAction_);

    addSectorShapeMenu_ = new ui::shortcuts::KeySequenceDisplayMenu("Add Sector &Shape", this);
    addSectorShapeMenu_->setObjectName("edit.add_sector_shape");
    connect(addSectorShapeMenu_, &QMenu::aboutToShow, this, [this]()
        {
            rebuildAddSectorShapeMenu();
        });
    rebuildAddSectorShapeMenu();
    // The Add Sector action opens an options overlay with the same complete
    // shape selector. Keep this menu as the viewport tool-button drop-down,
    // but do not duplicate it as a second Edit > Sector command.

    templateEditorAction_ = new QAction("Template &Editor...", this);
    templateEditorAction_->setObjectName("edit.template_editor");
    connect(templateEditorAction_, &QAction::triggered, this, [this]()
        {
            showTemplateEditorDialog();
        });
    thingMenu->addAction(templateEditorAction_);
    thingMenu->addSeparator();

    addThingAction_ = new QAction("Show/Hide Thing &Placement Tool...", this);
    addThingAction_->setObjectName("edit.add_thing");
    addThingAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::actionAddThing));
    addThingAction_->setShortcut(QKeySequence(Qt::Key_A));
    addThingAction_->setShortcutContext(Qt::WidgetShortcut);
    addThingAction_->setAutoRepeat(false);
    addThingAction_->setCheckable(true);
    addThingAction_->setToolTip("Show or hide the Thing placement tool while Thing edit mode is active");
    connect(addThingAction_, &QAction::triggered, this, [this](const bool checked)
        {
            setThingPlacementPanelVisible(checked);
        });
    thingMenu->addAction(addThingAction_);

    // Legacy SED preview camera helpers.
    const auto cameraPyr = [this]()
        {
            const QVector3D forward = glView_->cameraForward().normalized();
            const double radToDeg = 180.0 / std::numbers::pi;
            TVector pyr{};
            pyr.pitch = std::asin(std::clamp(static_cast<double>(forward.z()), -1.0, 1.0)) * radToDeg;
            pyr.yaw = std::atan2(-static_cast<double>(forward.x()), static_cast<double>(forward.y())) * radToDeg;
            if ( pyr.yaw < 0.0 )
            {
                pyr.yaw += 360.0;
            }
            pyr.roll = 0.0;
            return pyr;
        };
    const auto cameraPosition = [this]()
        {
            const QVector3D eye = glView_->cameraEye();
            return TVector{ static_cast<double>(eye.x()), static_cast<double>(eye.y()), static_cast<double>(eye.z()) };
        };

    QAction* const setThingToCameraAction = new QAction("Move Things to &Camera", this);
    setThingToCameraAction->setObjectName("thing.set_to_camera");
    setThingToCameraAction->setToolTip(
        "Move the selected Things so the active Thing sits at the camera position");
    connect(setThingToCameraAction, &QAction::triggered, this, [this, cameraPosition]()
        {
            const editor::Selection& selection = document_.selection();
            if ( !glView_ || selection.things.empty() )
            {
                QMessageBox::information(this, "Move Things to Camera", "Select a Thing first.");
                return;
            }
            std::vector<std::int32_t> things(selection.things.begin(), selection.things.end());
            std::sort(things.begin(), things.end());
            std::int32_t active = glView_->activeThingIndex();
            if ( !selection.things.contains(active) )
            {
                active = things.front();
            }
            const Level& level = document_.level();
            if ( active < 0 || static_cast<std::size_t>(active) >= level.things.size() )
            {
                return;
            }
            const Thing& anchor = level.things[static_cast<std::size_t>(active)];
            const TVector eye = cameraPosition();
            const double dx = eye.x - anchor.x;
            const double dy = eye.y - anchor.y;
            const double dz = eye.z - anchor.z;
            if ( dx == 0.0 && dy == 0.0 && dz == 0.0 )
            {
                return;
            }
            const bool applied = document_.applyUndoable(
                things.size() == 1 ? "Thing: Moved to camera" : "Things: Moved to camera",
                editor::DocumentDirty::Things,
                [&](editor::DocumentState& state) -> bool
                {
                    return ::sed::level::levelTranslateThings(state.level, things, dx, dy, dz, true);
                });
            if ( !applied )
            {
                QMessageBox::warning(this, "Move Things to Camera",
                    "The selected Things have no finite representable translation.");
                return;
            }
            glView_->notifyThingsChanged();
            glView_->setHighlightedSelection(document_.selection());
            rebuildProperties();
            updateEditActions();
        });
    thingMenu->addAction(setThingToCameraAction);

    QAction* const insertThingAtCameraAction = new QAction("&Insert Thing at Camera", this);
    insertThingAtCameraAction->setObjectName("thing.insert_at_camera");
    insertThingAtCameraAction->setToolTip(
        "Add the current placement template at the camera position, facing along the camera");
    connect(insertThingAtCameraAction, &QAction::triggered, this, [this, cameraPosition, cameraPyr]()
        {
            const std::string templateName = currentThingTemplateName();
            if ( !glView_ || templateName.empty() )
            {
                QMessageBox::information(this, "Insert Thing at Camera",
                    "Choose a template in the Thing placement tool first.");
                return;
            }
            const TVector eye = cameraPosition();
            const std::int32_t sector = glView_->findContainingSector(eye, -1);
            if ( sector < 0 )
            {
                glView_->showToast(QStringLiteral("Can't place thing outside level geometry"), 2600,
                    GlView::ToastPlacement::BottomCenter, GlView::ToastGroup::Error);
                return;
            }
            if ( !document_.level().canAllocateIds(1) )
            {
                glView_->showToast(QStringLiteral("Thing placement failed: entity ID space exhausted"), 3000,
                    GlView::ToastPlacement::BottomCenter, GlView::ToastGroup::Error);
                return;
            }
            ensureThingPlacementTemplateCache(templateName);
            const TVector pyr = cameraPyr();
            Thing placed{};
            placed.name = templateName;
            placed.templateName = templateName;
            if ( thingPlacementSnapTemplateBoxCache_ )
            {
                placed.bbox = *thingPlacementSnapTemplateBoxCache_;
            }
            placed.x = eye.x;
            placed.y = eye.y;
            placed.z = eye.z;
            placed.pch = pyr.pitch;
            placed.yaw = pyr.yaw;
            placed.rol = 0.0;
            placed.sector = sector;

            document_.pushThingTailUndo("Thing: Added at camera",
                editor::DocumentDirty::Structure | editor::DocumentDirty::Things);
            editor::DocumentState& state = document_.current();
            Thing& thing = state.level.newThing();
            const std::int32_t allocatedId = thing.id;
            const std::int32_t destinationNum = thing.num;
            thing = std::move(placed);
            thing.id = allocatedId;
            thing.num = destinationNum;
            const std::int32_t index = static_cast<std::int32_t>(state.level.things.size() - 1);
            state.selection.clear();
            state.selection.things.insert(index);
            (void)document_.touchLatestUndoRecord();

            glView_->notifyThingsAppended(static_cast<std::size_t>(index));
            glView_->setSelectionState(-1, -1, index, -1, document_.selection());
            appendThingsToOutliner(static_cast<std::size_t>(index));
            rebuildProperties();
            updateEditActions();
            refreshThingChecks({ index }, true);
        });
    thingMenu->addAction(insertThingAtCameraAction);

    QAction* const copyCameraFrameAction = new QAction("Copy Camera as &Frame", this);
    copyCameraFrameAction->setObjectName("thing.copy_camera_as_frame");
    copyCameraFrameAction->setToolTip(
        "Copy the camera position and orientation as a Thing frame value: (x/y/z:pitch/yaw/roll)");
    connect(copyCameraFrameAction, &QAction::triggered, this, [this, cameraPosition, cameraPyr]()
        {
            if ( !glView_ )
            {
                return;
            }
            const TVector eye = cameraPosition();
            const TVector pyr = cameraPyr();
            QApplication::clipboard()->setText(QString::asprintf("(%.5f/%.5f/%.5f:%.5f/%.5f/%.5f)",
                eye.x, eye.y, eye.z, pyr.pitch, pyr.yaw, pyr.roll));
            glView_->showToast(QStringLiteral("Camera copied as frame"), 1800,
                GlView::ToastPlacement::BottomCenter);
        });
    thingMenu->addAction(copyCameraFrameAction);

    QAction* const makeDoorAction = new QAction("Make &Door", this);
    makeDoorAction->setObjectName("thing.make_door");
    makeDoorAction->setToolTip(
        "Turn the active Thing into a two-frame sliding door driven by 00_door.cog");
    connect(makeDoorAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Make Door");
            const editor::Selection& selection = document_.selection();
            std::int32_t active = glView_ ? glView_->activeThingIndex() : -1;
            if ( !selection.things.contains(active) && !selection.things.empty() )
            {
                active = *std::min_element(selection.things.begin(), selection.things.end());
            }
            const Level& level = document_.level();
            if ( editMode_ != EditMode::thing || active < 0
                || static_cast<std::size_t>(active) >= level.things.size() )
            {
                QMessageBox::information(this, title, "Select a Thing in Thing mode first.");
                return;
            }
            const double height = level::makeDoorHeight(
                level, &templates_, static_cast<std::size_t>(active));
            constexpr const char* kDoorScript = "00_door.cog";
            const assets::CogSymbolLoadResult script =
                assets::loadCogSymbolsWithDefaultsResult(resources_, kDoorScript, false);
            if ( !script.loaded() )
            {
                QMessageBox::information(this, title,
                    QStringLiteral("The door script %1 is not available in the project resources.")
                        .arg(QString::fromLatin1(kDoorScript)));
                return;
            }

            level::MakeDoorError error = level::MakeDoorError::None;
            const bool applied = document_.applyUndoable(
                "Thing: Made door",
                editor::DocumentDirty::Things | editor::DocumentDirty::Cogs | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    error = level::levelMakeDoor(state.level, static_cast<std::size_t>(active), height,
                        kDoorScript, script.symbols).error;
                    return error == level::MakeDoorError::None;
                });
            if ( !applied )
            {
                QString message = QStringLiteral("The Thing could not be made into a door.");
                switch ( error )
                {
                case level::MakeDoorError::AlreadyDoor:
                    message = QStringLiteral("The Thing appears to be a door already.");
                    break;
                case level::MakeDoorError::FlatBoundingBox:
                    message = QStringLiteral("The Thing doesn't have a usable bounding box height.");
                    break;
                case level::MakeDoorError::IdSpaceExhausted:
                    message = QStringLiteral("Cannot add the door COG: entity ID space exhausted.");
                    break;
                case level::MakeDoorError::InvalidThing:
                case level::MakeDoorError::None:
                    break;
                }
                QMessageBox::information(this, title, message);
                return;
            }
            if ( glView_ )
            {
                glView_->notifyThingsChanged();
                glView_->setHighlightedSelection(document_.selection());
            }
            syncCogScriptWatches();
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        });
    thingMenu->addAction(makeDoorAction);

    addLightAction_ = new QAction("Add &Light", this);
    addLightAction_->setObjectName("edit.add_light");
    addLightAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::actionAddLight));
    addLightAction_->setShortcut(QKeySequence(Qt::Key_A));
    addLightAction_->setShortcutContext(Qt::WidgetShortcut);
    addLightAction_->setAutoRepeat(false);
    addLightAction_->setCheckable(true);
    connect(addLightAction_, &QAction::triggered, this, [this]()
        {
            const PlacementMode newMode = (placementMode_ == PlacementMode::light) ? PlacementMode::none : PlacementMode::light;
            setPlacementMode(newMode);
    });
    lightMenu->addAction(addLightAction_);
    lightMenu->addAction(recalcLightingAction_);

    autoRecalculateLightingAction_ = new QAction("&Auto Recalculate Lighting", this);
    autoRecalculateLightingAction_->setObjectName("edit.auto_recalculate_lighting");
    autoRecalculateLightingAction_->setCheckable(true);
    autoRecalculateLightingAction_->setChecked(
        settingsStore().value("lighting/auto_recalculate", false).toBool());
    autoRecalculateLightingAction_->setToolTip(
        "Automatically recalculate the affected baked-lighting area after a light changes. "
        "Changes are debounced and calculated in the background.");
    autoRecalculateLightingAction_->setStatusTip(autoRecalculateLightingAction_->toolTip());
    connect(autoRecalculateLightingAction_, &QAction::toggled, this, [this](const bool checked)
        {
            setLightBakeAutoEnabled(checked);
        });
    lightMenu->addAction(autoRecalculateLightingAction_);
    lightMenu->addSeparator();
    lightMenu->addAction(lightDeleteMenuAction_);

    thingMenu->addAction(thingDeleteMenuAction_);

    addCogAction_ = new QAction("Add &COG...", this);
    addCogAction_->setObjectName("edit.add_cog");
    addCogAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    connect(addCogAction_, &QAction::triggered, this, [this]()
        {
            auto pickExtractionRoot = [&]() -> std::optional<std::filesystem::path>
            {
                if (!resourceRoots_.empty())
                {
                    // Prefer a Resource/ root when present (especially for IJIM, where Resource/ is the canonical disk root).
                    for (const std::filesystem::path& root : resourceRoots_)
                    {
                        if (root.filename() == "Resource")
                        {
                            return root;
                        }
                    }
                    return resourceRoots_.front();
                }
                if (!resources_.roots().empty())
                {
                    for (const std::filesystem::path& root : resources_.roots())
                    {
                        if (root.filename() == "Resource")
                        {
                            return root;
                        }
                    }
                    return resources_.roots().front();
                }
                return std::nullopt;
            };

            const auto picked = ui::pickCogScript(this, resources_, pickExtractionRoot());
            if (!picked || picked->scriptName.empty())
            {
                return;
            }

            const std::string scriptName = picked->scriptName;

            if (scriptName.empty())
            {
                return;
            }
            if ( !document_.level().canAllocateIds(1) )
            {
                constexpr const char* message =
                    "Cannot add COG: entity ID space exhausted";
                if ( glView_ )
                {
                    glView_->showToast(
                        message,
                        3000,
                        GlView::ToastPlacement::BottomCenter,
                        GlView::ToastGroup::Error);
                }
                if ( statusBar() )
                {
                    statusBar()->showMessage(message, 4000);
                }
                return;
            }

            const std::vector<CogValue> values = ::sed::assets::loadCogSymbolsWithDefaults(resources_, scriptName, false);
            std::optional<std::int32_t> newIndex{};

            (void)document_.applyUndoable(
                "COG: Added",
                editor::DocumentDirty::Cogs | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    Cog& cog = state.level.newCog();
                    cog.name = scriptName;
                    cog.values = values;
                    newIndex = cog.num;

                    state.selection.clear();
                    state.selection.cogs.insert(cog.num);
                    return true;
                });

            if (newIndex && outlinerTabs_)
            {
                outlinerTabs_->setCurrentIndex(2);
            }

            if (glView_)
            {
                glView_->setSelection(-1, -1, -1, -1);
                glView_->setHighlightedSelection(document_.selection());
            }

            syncCogScriptWatches();
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        });
    cogMenu->addAction(addCogAction_);

    duplicateCogAction_ = new QAction("&Duplicate COG", this);
    duplicateCogAction_->setObjectName("edit.duplicate_cog");
    duplicateCogAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    connect(duplicateCogAction_, &QAction::triggered, this, [this]()
        {
            std::vector<std::int32_t> sources{};
            sources.reserve(document_.selection().cogs.size());
            for (const std::int32_t idx : document_.selection().cogs)
            {
                sources.push_back(idx);
            }
            std::sort(sources.begin(), sources.end());
            sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
            std::erase_if(
                sources,
                [this](const std::int32_t index)
                {
                    return index < 0
                        || static_cast<std::size_t>(index)
                            >= document_.level().cogs.size();
                });

            if (sources.empty())
            {
                return;
            }
            if ( !document_.level().canAllocateIds(sources.size()) )
            {
                constexpr const char* message =
                    "Cannot duplicate COGs: entity ID space exhausted";
                if ( glView_ )
                {
                    glView_->showToast(
                        message,
                        3000,
                        GlView::ToastPlacement::BottomCenter,
                        GlView::ToastGroup::Error);
                }
                if ( statusBar() )
                {
                    statusBar()->showMessage(message, 4000);
                }
                return;
            }

            std::vector<std::int32_t> newIndices{};
            const bool applied = document_.applyUndoable(
                ui::undo::action("COG", sources.size(), "Duplicated"),
                editor::DocumentDirty::Cogs | editor::DocumentDirty::Structure,
                [this, &sources, &newIndices](editor::DocumentState& state) -> bool
                {
                    newIndices.clear();
                    for (const std::int32_t sourceIndex : sources)
                    {
                        if (sourceIndex < 0 || static_cast<std::size_t>(sourceIndex) >= state.level.cogs.size())
                        {
                            continue;
                        }

                        const Cog& src = state.level.cogs[static_cast<std::size_t>(sourceIndex)];
                        // newCog() may reallocate the COG vector. Snapshot the
                        // source identity and payload before growing it so
                        // this loop never dereferences an invalid reference.
                        const std::string sourceName = src.name;
                        const std::vector<CogValue> sourceValues = src.values;
                        // Duplicate the authored payload exactly. Reloading a
                        // script here would discard unresolved/raw parameters
                        // and can change the result of a prior undo.
                        std::vector<CogValue> values = sourceValues;

                        Cog& dst = state.level.newCog();
                        dst.name = sourceName;
                        dst.values = std::move(values);
                        newIndices.push_back(dst.num);
                    }

                    if (newIndices.empty())
                    {
                        return false;
                    }

                    state.selection.clear();
                    for (const std::int32_t idx : newIndices)
                    {
                        state.selection.cogs.insert(idx);
                    }
                    return true;
                });

            if (!applied)
            {
                return;
            }

            if (outlinerTabs_)
            {
                outlinerTabs_->setCurrentIndex(2);
            }

            if (glView_)
            {
                glView_->setSelection(-1, -1, -1, -1);
                glView_->setHighlightedSelection(document_.selection());
            }

            syncCogScriptWatches();
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        });
    cogMenu->addAction(duplicateCogAction_);
    cogMenu->addSeparator();
    cogMenu->addAction(cogDeleteMenuAction_);

    extrudeSurfaceAction_ = new QAction("E&xtrude", this);
    extrudeSurfaceAction_->setObjectName("surface.extrude");
    extrudeSurfaceAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::actionExtrude));
    extrudeSurfaceAction_->setCheckable(true);
    extrudeSurfaceAction_->setToolTip("Extrude surface");
    connect(extrudeSurfaceAction_, &QAction::triggered, this, [this](const bool checked)
        {
            if (checked)
            {
                beginSurfaceExtrudePreview();
                return;
            }

            if (surfaceExtrudePreviewActive_)
            {
                cancelSurfaceExtrudePreview();
            }
        });
    surfaceMenu->addAction(extrudeSurfaceAction_);

    makeAdjoinAction_ = new QAction("&Adjoin", this);
    makeAdjoinAction_->setObjectName("surface.make_adjoin");
    makeAdjoinAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::actionAdjoin));
    makeAdjoinAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    makeAdjoinAction_->setToolTip("Adjoin surfaces");
    connect(makeAdjoinAction_, &QAction::triggered, this, [this]()
        {
            if (document_.selection().surfaces.empty())
            {
                QMessageBox::information(this, "Adjoin surfaces", "Select a surface first.");
                return;
            }

            std::vector<SurfaceRef> selectedSurfaces{};
            selectedSurfaces.reserve(document_.selection().surfaces.size());
            for (const SurfaceRef ref : document_.selection().surfaces)
            {
                selectedSurfaces.push_back(ref);
            }
            std::sort(selectedSurfaces.begin(), selectedSurfaces.end(), [](const SurfaceRef& a, const SurfaceRef& b)
                {
                    if (a.sector != b.sector)
                    {
                        return a.sector < b.sector;
                    }
                    return a.surface < b.surface;
                });

            const bool tryConnectTwoSelected =
                selectedSurfaces.size() == 2
                && document_.selection().sectors.empty()
                && document_.selection().things.empty()
                && document_.selection().lights.empty()
                && document_.selection().cogs.empty()
                && selectedSurfaces[0].sector != selectedSurfaces[1].sector;

            const SurfaceRef surfaceRef = selectedSurfaces.front();

            const bool applied = document_.applyUndoable(
                "Surfaces: Adjoined",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    if (tryConnectTwoSelected)
                    {
                        if (::sed::level::levelConnectSurfaces(state.level, selectedSurfaces[0], selectedSurfaces[1]))
                        {
                            state.selection.clear();
                            return true;
                        }
                    }

                    if (!::sed::level::levelMakeAdjoin(state.level, surfaceRef, true))
                    {
                        return false;
                    }

                    state.selection.clear();
                    return true;
                });

            if (!applied)
            {
                QMessageBox::warning(
                    this,
                    "Adjoin surfaces failed",
                    "No compatible surface was found, or the surfaces could not be auto-cut into an adjoin.");
                return;
            }

            invalidateSurfaceSelectionContextForTopologyEdit();

            glView_->notifyLevelGeometryChanged();
            glView_->setSelection(-1, -1, -1, -1);
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        });
    surfaceMenu->addAction(makeAdjoinAction_);

    unAdjoinAction_ = new QAction("Un&adjoin", this);
    unAdjoinAction_->setObjectName("surface.unadjoin");
    unAdjoinAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::actionUnadjoin));
    unAdjoinAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
    unAdjoinAction_->setToolTip("Unadjoin surfaces");
    connect(unAdjoinAction_, &QAction::triggered, this, [this]()
        {
            if (document_.selection().surfaces.empty())
            {
                QMessageBox::information(this, "Unadjoin surfaces", "Select a surface first.");
                return;
            }

            const SurfaceRef surfaceRef = *document_.selection().surfaces.begin();

            const bool applied = document_.applyUndoable(
                "Surface: Unadjoined",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    if (!::sed::level::levelUnadjoinSurface(state.level, surfaceRef))
                    {
                        return false;
                    }

                    state.selection.clear();
                    return true;
                });

            if (!applied)
            {
                QMessageBox::warning(this, "Unadjoin surfaces failed", "Surface is not adjoined or operation not permitted.");
                return;
            }

            glView_->notifyLevelGeometryChanged();
            glView_->setSelection(-1, -1, -1, -1);
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        });
    surfaceMenu->addAction(unAdjoinAction_);

    connectSurfacesAction_ = new QAction("Connect &Surfaces", this);
    connectSurfacesAction_->setObjectName("surface.connect");
    connectSurfacesAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::toolConnect));
    connectSurfacesAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connectSurfacesAction_->setToolTip("Connect surfaces");
    connect(connectSurfacesAction_, &QAction::triggered, this, [this]()
        {
            if (document_.selection().surfaces.size() != 2 || !document_.selection().sectors.empty()
                || !document_.selection().things.empty() || !document_.selection().lights.empty() || !document_.selection().cogs.empty())
            {
                QMessageBox::information(this, "Connect surfaces", "Select exactly two surfaces first.");
                return;
            }

            std::vector<SurfaceRef> surfaces{};
            surfaces.reserve(2);
            for (const SurfaceRef ref : document_.selection().surfaces)
            {
                surfaces.push_back(ref);
            }

            std::sort(surfaces.begin(), surfaces.end(), [](const SurfaceRef& a, const SurfaceRef& b)
                {
                    if (a.sector != b.sector)
                    {
                        return a.sector < b.sector;
                    }
                    return a.surface < b.surface;
                });

            if (surfaces.size() != 2 || surfaces[0].sector == surfaces[1].sector)
            {
                QMessageBox::information(this, "Connect surfaces", "Select two surfaces in different sectors.");
                return;
            }

            const bool applied = document_.applyUndoable(
                "Surfaces: Connected",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    if (!::sed::level::levelConnectSurfaces(state.level, surfaces[0], surfaces[1]))
                    {
                        return false;
                    }

                    state.selection.clear();
                    return true;
                });

            if (!applied)
            {
                QMessageBox::warning(this, "Connect surfaces failed", "Surfaces are not compatible or do not overlap.");
                return;
            }

            invalidateSurfaceSelectionContextForTopologyEdit();

            glView_->notifyLevelGeometryChanged();
            glView_->setSelection(-1, -1, -1, -1);
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        });
    surfaceMenu->addAction(connectSurfacesAction_);

    planarizeSurfaceAction_ = new QAction("&Planarize", this);
    planarizeSurfaceAction_->setObjectName("surface.planarize");
    planarizeSurfaceAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    planarizeSurfaceAction_->setToolTip("Move the selected surfaces' vertices onto each surface's plane");
    connect(planarizeSurfaceAction_, &QAction::triggered, this, [this]()
        {
            const editor::Selection& selection = document_.selection();
            if (selection.surfaces.empty())
            {
                QMessageBox::information(this, "Planarize", "Select a surface first.");
                return;
            }

            const std::vector<SurfaceRef> targets =
                ::sed::level::planarizeTargets(document_.level(), selection.surfaces);
            if (targets.empty())
            {
                if (glView_)
                {
                    glView_->showToast(
                        selection.surfaces.size() == 1
                            ? QStringLiteral("Surface is already planar")
                            : QStringLiteral("Selected surfaces are already planar"),
                        2200,
                        GlView::ToastPlacement::BottomCenter);
                }
                return;
            }

            std::size_t stillNonPlanar = 0;
            const bool applied = document_.applyUndoable(
                targets.size() == 1 ? "Surface: Planarized" : "Surfaces: Planarized",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    const ::sed::level::PlanarizeResult result =
                        ::sed::level::levelPlanarizeSurfaces(state.level, targets);
                    stillNonPlanar = result.stillNonPlanar;
                    return result.changed;
                });
            if (!applied)
            {
                QMessageBox::warning(this, "Planarize", "The selected surface could not be planarized.");
                return;
            }

            glView_->notifyLevelGeometryChanged();
            rebuildProperties();
            updateEditActions();

            const QString message = stillNonPlanar == 0
                ? (targets.size() == 1
                    ? QStringLiteral("Surface planarized")
                    : QStringLiteral("%1 surfaces planarized").arg(targets.size()))
                : QStringLiteral("%1 of %2 surfaces weren't fully planarized")
                    .arg(stillNonPlanar).arg(targets.size());
            logMessage(stillNonPlanar == 0 ? MessageSeverity::info : MessageSeverity::warning, message);
            glView_->showToast(message, 2600, GlView::ToastPlacement::BottomCenter,
                stillNonPlanar == 0 ? GlView::ToastGroup::General : GlView::ToastGroup::Error);
        });
    surfaceMenu->addAction(planarizeSurfaceAction_);

    // Legacy SED Recovery commands: sector selections repair every surface of
    // the selected sectors; surface selections repair just those surfaces.
    const auto flagRepairTargets = [this]()
        {
            const editor::Selection& selection = document_.selection();
            const std::vector<std::int32_t> sectors(selection.sectors.begin(), selection.sectors.end());
            const std::vector<SurfaceRef> surfaces(selection.surfaces.begin(), selection.surfaces.end());
            return ::sed::level::surfaceFlagRepairTargets(document_.level(), sectors, surfaces);
        };
    const auto runFlagRepair = [this, flagRepairTargets](const QString& title, const char* undoLabel,
        const QString& noChangeMessage, const QString& changedMessage,
        std::size_t (*repair)(Level&, std::span<const SurfaceRef>) noexcept)
        {
            const std::vector<SurfaceRef> targets = flagRepairTargets();
            if ( targets.empty() )
            {
                QMessageBox::information(this, title, "Select a sector or surface first.");
                return;
            }
            std::size_t changed = 0;
            (void)document_.applyUndoable(undoLabel, editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    changed = repair(state.level, targets);
                    return changed > 0;
                });
            const QString message = changed == 0 ? noChangeMessage : changedMessage.arg(changed);
            if ( changed > 0 )
            {
                glView_->notifyLevelGeometryChanged();
                rebuildProperties();
                updateEditActions();
                logMessage(MessageSeverity::info, message);
            }
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        };

    QAction* const fixSolidAction = new QAction("Fix &Solid Surfaces", this);
    fixSolidAction->setObjectName("surface.fix_solid");
    fixSolidAction->setToolTip(
        "Make unadjoined surfaces textured and collidable, and recompute their floor flag");
    connect(fixSolidAction, &QAction::triggered, this, [runFlagRepair]()
        {
            runFlagRepair(QStringLiteral("Fix Solid Surfaces"), "Surfaces: Fixed solid flags",
                QStringLiteral("Selected surfaces are already solid"),
                QStringLiteral("Fixed %1 solid surface(s)"), &::sed::level::levelFixSolidSurfaces);
        });
    surfaceMenu->addAction(fixSolidAction);

    QAction* const fixPassableAction = new QAction("Fix Passable &Adjoins", this);
    fixPassableAction->setObjectName("surface.fix_passable_adjoins");
    fixPassableAction->setToolTip(
        "Make adjoined surfaces invisible and passable, clearing AI and player movement blocks");
    connect(fixPassableAction, &QAction::triggered, this, [runFlagRepair]()
        {
            runFlagRepair(QStringLiteral("Fix Passable Adjoins"), "Adjoins: Fixed passable flags",
                QStringLiteral("Selected adjoins are already passable"),
                QStringLiteral("Fixed %1 passable adjoin(s)"), &::sed::level::levelFixPassableAdjoins);
        });
    surfaceMenu->addAction(fixPassableAction);

    // Absorbs the legacy JED Adjoin Jiffy plug-in: the active surface stays put
    // and the other selected surface is fitted onto it, then both are adjoined.
    QAction* const fitAdjoinAction = new QAction("Fit and Ad&join", this);
    fitAdjoinAction->setObjectName("surface.fit_and_adjoin");
    fitAdjoinAction->setToolTip(
        "Move the corners of one selected surface onto the active surface and adjoin them");
    connect(fitAdjoinAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Fit and Adjoin");
            const editor::Selection& selection = document_.selection();
            if ( selection.surfaces.size() != 2u )
            {
                QMessageBox::information(this, title, "Select exactly two surfaces in different sectors.");
                return;
            }
            std::vector<SurfaceRef> pair(selection.surfaces.begin(), selection.surfaces.end());
            std::sort(pair.begin(), pair.end(), [](const SurfaceRef& a, const SurfaceRef& b)
                {
                    return a.sector != b.sector ? a.sector < b.sector : a.surface < b.surface;
                });
            const std::optional<SurfaceRef> active = glView_ ? glView_->selectedSurfaceRef() : std::nullopt;
            if ( active && *active == pair[1] )
            {
                std::swap(pair[0], pair[1]);
            }
            const bool applied = document_.applyUndoable("Surfaces: Fit and adjoin",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    return level::levelFitAndAdjoinSurfaces(state.level, pair[0], pair[1]);
                });
            if ( !applied )
            {
                QMessageBox::information(this, title,
                    "The surfaces could not be fitted. They must be unadjoined, in different sectors, have the "
                    "same number of corners, and the fitted sector must stay convex without moving other adjoins.");
                return;
            }
            glView_->notifyLevelGeometryChanged();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Fitted and adjoined two surfaces");
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    surfaceMenu->addAction(fitAdjoinAction);

    // Absorbs the legacy JED Shatter plug-in using the retail breaking-glass
    // script contract (see docs/user_guide.md, Sector recipes).
    QAction* const glassAction = new QAction("Make Breakable &Glass...", this);
    glassAction->setObjectName("surface.make_breakable_glass");
    glassAction->setToolTip(
        "Turn the selected adjoined surface and its other side into a pane that shatters when damaged");
    connect(glassAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Make Breakable Glass");
            const editor::Selection& selection = document_.selection();
            if ( selection.surfaces.size() != 1u )
            {
                QMessageBox::information(this, title, "Select exactly one adjoined surface.");
                return;
            }
            const SurfaceRef glass = *selection.surfaces.begin();
            constexpr const char* kGlassScript = "00_breakingglass.cog";
            const assets::CogSymbolLoadResult script =
                assets::loadCogSymbolsWithDefaultsResult(resources_, kGlassScript, false);
            if ( !script.loaded() )
            {
                QMessageBox::information(this, title,
                    QStringLiteral("The glass script %1 is not available in the project resources.")
                        .arg(QString::fromLatin1(kGlassScript)));
                return;
            }
            const level::BreakableGlassSettings defaults =
                level::breakableGlassDefaults(document_.level(), glass, script.symbols);
            QDialog dialog(this);
            dialog.setObjectName("surface.make_breakable_glass.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            auto* const material = new QLineEdit(QString::fromStdString(defaults.material), &dialog);
            material->setObjectName("material");
            material->setPlaceholderText("Keep current material");
            auto* const density = new QSpinBox(&dialog);
            density->setObjectName("density");
            density->setRange(0, level::kMaxBreakableGlassDensity);
            density->setValue(defaults.density.value_or(level::kDefaultBreakableGlassDensity));
            auto* const spawnPoints = new QSpinBox(&dialog);
            spawnPoints->setObjectName("spawnpoints");
            spawnPoints->setRange(0, level::kMaxBreakableGlassSpawnPoints);
            spawnPoints->setValue(defaults.spawnPoints.value_or(level::kDefaultBreakableGlassSpawnPoints));
            form->addRow("Glass material:", material);
            form->addRow("Shards per point:", density);
            form->addRow("Spawn points per corner:", spawnPoints);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            level::BreakableGlassSettings settings{};
            settings.material = material->text().trimmed().toStdString();
            settings.density = density->value();
            settings.spawnPoints = spawnPoints->value();
            if ( const auto failure = level::validateBreakableGlassSettings(settings) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            level::BreakableGlassError error = level::BreakableGlassError::None;
            const bool applied = document_.applyUndoable("Surfaces: Made breakable glass",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Cogs | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    error = level::levelMakeBreakableGlass(state.level, glass, kGlassScript, script.symbols, settings)
                        .error;
                    return error == level::BreakableGlassError::None;
                });
            if ( !applied )
            {
                QString message = QStringLiteral("The surface could not be made into breakable glass.");
                switch ( error )
                {
                case level::BreakableGlassError::NotAdjoined:
                    message = QStringLiteral("The surface must be adjoined to another sector on both sides.");
                    break;
                case level::BreakableGlassError::MissingSymbols:
                    message = QStringLiteral("The glass script does not declare two surface symbols.");
                    break;
                case level::BreakableGlassError::AlreadyGlass:
                    message = QStringLiteral("This pane already uses the glass script.");
                    break;
                case level::BreakableGlassError::IdSpaceExhausted:
                    message = QStringLiteral("Cannot add the glass COG: entity ID space exhausted.");
                    break;
                case level::BreakableGlassError::InvalidSettings:
                    message = QStringLiteral(
                        "Breakable glass density must be within 0..64 and spawn points within 0..16.");
                    break;
                case level::BreakableGlassError::InvalidSurface:
                case level::BreakableGlassError::None:
                    break;
                }
                QMessageBox::information(this, title, message);
                return;
            }
            glView_->notifyLevelGeometryChanged();
            syncCogScriptWatches();
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Made a breakable glass pane");
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    surfaceMenu->addAction(glassAction);

    // Legacy SED Alt+B: vertices rebuild a missing face, faces split off a sector.
    QAction* const buildAction = new QAction("&Build Surface or Sector", this);
    buildAction->setObjectName("surface.build");
    buildAction->setShortcut(QKeySequence(QStringLiteral("Alt+B")));
    buildAction->setToolTip(
        "Vertex mode: build a surface from the selected vertices. Face mode: move the selected faces into a new sector");
    connect(buildAction, &QAction::triggered, this, [this]()
        {
            if ( !glView_ || editMode_ != EditMode::surface )
            {
                return;
            }
            const bool vertexMode = glView_->surfaceSubMode() == GlView::SurfaceSubMode::Vertex;
            const QString title = vertexMode ? QStringLiteral("Build Surface") : QStringLiteral("Build Sector");
            const editor::Selection& selection = document_.selection();
            const Level& currentLevel = document_.level();

            std::int32_t sectorIndex = -1;
            std::vector<std::int32_t> vertices{};
            std::vector<SurfaceRef> faces{};
            bool mixedSectors = false;
            if ( vertexMode )
            {
                for ( const editor::SurfaceVertexRef& ref : selection.surfaceVertices )
                {
                    const SurfaceRef& owner = ref.surface;
                    if ( owner.sector < 0 || static_cast<std::size_t>(owner.sector) >= currentLevel.sectors.size() )
                    {
                        continue;
                    }
                    const Sector& sector = currentLevel.sectors[static_cast<std::size_t>(owner.sector)];
                    if ( owner.surface < 0 || static_cast<std::size_t>(owner.surface) >= sector.surfaces.size() )
                    {
                        continue;
                    }
                    const std::vector<std::int32_t>& loop = sector.surfaces[static_cast<std::size_t>(owner.surface)].vertices;
                    if ( ref.localVertex < 0 || static_cast<std::size_t>(ref.localVertex) >= loop.size() )
                    {
                        continue;
                    }
                    mixedSectors = mixedSectors || (sectorIndex >= 0 && sectorIndex != owner.sector);
                    sectorIndex = owner.sector;
                    vertices.push_back(loop[static_cast<std::size_t>(ref.localVertex)]);
                }
            }
            else
            {
                faces.assign(selection.surfaces.begin(), selection.surfaces.end());
            }
            if ( mixedSectors )
            {
                QMessageBox::information(this, title, "Select vertices from one sector only.");
                return;
            }

            ::sed::level::ComponentRebuildError error = ::sed::level::ComponentRebuildError::None;
            SurfaceRef builtSurface{ -1, -1 };
            std::int32_t builtSector = -1;
            const bool applied = document_.applyUndoable(
                vertexMode ? "Surfaces: Built surface" : "Sectors: Built sector",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    if ( vertexMode )
                    {
                        const ::sed::level::BuildSurfaceResult result =
                            ::sed::level::levelBuildSurfaceFromVertices(state.level, sectorIndex, vertices);
                        error = result.error;
                        builtSurface = result.surface;
                        return error == ::sed::level::ComponentRebuildError::None;
                    }
                    const ::sed::level::BuildSectorResult result =
                        ::sed::level::levelBuildSectorFromSurfaces(state.level, faces);
                    error = result.error;
                    builtSector = result.sector;
                    if ( error != ::sed::level::ComponentRebuildError::None )
                    {
                        return false;
                    }
                    state.selection.clear();
                    const Sector& sector = state.level.sectors[static_cast<std::size_t>(builtSector)];
                    for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
                    {
                        state.selection.surfaces.insert(SurfaceRef{ builtSector, static_cast<std::int32_t>(i) });
                    }
                    return true;
                });
            if ( !applied )
            {
                using ::sed::level::ComponentRebuildError;
                QString reason{};
                switch ( error )
                {
                    case ComponentRebuildError::TooFewElements:
                        reason = vertexMode ? QStringLiteral("Select at least three vertices of one sector.")
                                            : QStringLiteral("Select at least four surfaces of one sector.");
                        break;
                    case ComponentRebuildError::MixedSectors:
                        reason = QStringLiteral("Select surfaces from one sector only.");
                        break;
                    case ComponentRebuildError::Degenerate:
                        reason = QStringLiteral("The selected vertices are collinear and cannot form a surface.");
                        break;
                    case ComponentRebuildError::DuplicateSurface:
                        reason = QStringLiteral("A surface with exactly these vertices already exists.");
                        break;
                    case ComponentRebuildError::AllocationFailure:
                        reason = QStringLiteral("The level could not be updated.");
                        break;
                    case ComponentRebuildError::InvalidReference:
                    case ComponentRebuildError::None:
                        reason = QStringLiteral("The selection is no longer valid.");
                        break;
                }
                QMessageBox::information(this, title, reason);
                return;
            }

            invalidateSurfaceSelectionContextForTopologyEdit();
            glView_->cancelAllStagedCleaveWorkspaces();
            glView_->notifyLevelGeometryChanged();
            if ( !vertexMode )
            {
                glView_->setSelectionState(builtSector, -1, -1, -1, document_.selection());
            }
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            scheduleRebuildChecks(editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure);
            const QString message = vertexMode
                ? QStringLiteral("Built surface %1 in sector %2").arg(builtSurface.surface).arg(builtSurface.sector)
                : QStringLiteral("Built sector %1").arg(builtSector);
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    surfaceMenu->addAction(buildAction);

    calculateSurfaceAngleAction_ = new QAction("Calculate &Angle", this);
    calculateSurfaceAngleAction_->setObjectName("surface.calculate_angle");
    calculateSurfaceAngleAction_->setToolTip(
        "Measure the angle between two selected surfaces and align the grid to their shared edge");
    connect(calculateSurfaceAngleAction_, &QAction::triggered, this, [this]()
        {
            const editor::Selection& selection = document_.selection();
            if (selection.surfaces.size() != 2 || !selection.sectors.empty() || !selection.things.empty()
                || !selection.lights.empty() || !selection.cogs.empty())
            {
                QMessageBox::information(this, "Calculate Angle", "Select exactly two surfaces first.");
                return;
            }

            std::vector<SurfaceRef> surfaces(selection.surfaces.begin(), selection.surfaces.end());
            std::sort(surfaces.begin(), surfaces.end(), [](const SurfaceRef& a, const SurfaceRef& b)
                {
                    return a.sector != b.sector ? a.sector < b.sector : a.surface < b.surface;
                });
            // The active surface is the reference plane, as in legacy SED.
            if (const std::optional<SurfaceRef> active = activeSelectedSurfaceRef();
                active && active->sector == surfaces[1].sector && active->surface == surfaces[1].surface)
            {
                std::swap(surfaces[0], surfaces[1]);
            }

            const std::optional<::sed::level::SurfaceAngleGrid> grid =
                ::sed::level::levelSurfaceAngleGrid(document_.level(), surfaces[0], surfaces[1]);
            if (!grid)
            {
                QMessageBox::information(this, "Calculate Angle",
                    "The surfaces are already coplanar (or parallel), so there is no shared edge to rotate around.");
                return;
            }

            const auto toQt = [](const TVector& v)
            {
                return QVector3D(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
            };
            const bool aligned = glView_
                && glView_->alignGridToBasis(toQt(grid->origin), toQt(grid->xAxis), toQt(grid->normal));
            preparedGridRotationDegrees_ = grid->angleDegrees;

            const QString message = aligned
                ? QStringLiteral("Angle between surfaces: %1\u00B0. Grid aligned to their shared edge; "
                                 "Rotate is preset to the Grid axis.")
                    .arg(grid->angleDegrees, 0, 'f', 3)
                : QStringLiteral("Angle between surfaces: %1\u00B0").arg(grid->angleDegrees, 0, 'f', 3);
            logMessage(MessageSeverity::info, message);
            if (glView_)
            {
                glView_->showToast(message, 4000, GlView::ToastPlacement::BottomCenter);
            }
        });
    surfaceMenu->addAction(calculateSurfaceAngleAction_);

    setupUvEditorSurfaceActions(surfaceMenu);
    surfaceMenu->addSeparator();
    surfaceMenu->addAction(surfaceDeleteMenuAction_);
    surfaceMenu->addAction(forceDeleteSurfaceVertexAction_);

    connectSectorsAction_ = new QAction("Join Se&ctors", this);
    connectSectorsAction_->setObjectName("sector.connect");
    connectSectorsAction_->setIcon(ui::makeViewportIcon(ui::ViewportIcon::toolConnect));
    connectSectorsAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    connectSectorsAction_->setToolTip("Join exactly two selected sectors");
    connect(connectSectorsAction_, &QAction::triggered, this, [this]()
        {
            if (document_.selection().sectors.size() != 2 || !document_.selection().surfaces.empty()
                || !document_.selection().things.empty() || !document_.selection().lights.empty() || !document_.selection().cogs.empty())
            {
                QMessageBox::information(this, "Join sectors", "Select exactly two sectors first.");
                return;
            }

            auto it = document_.selection().sectors.begin();
            const std::int32_t a = *it++;
            const std::int32_t b = *it;

            const std::int32_t keep = std::min(a, b);
            const std::int32_t remove = std::max(a, b);

            const bool applied = document_.applyUndoable(
                "Sectors: Joined",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    if (!::sed::level::levelConnectSectors(state.level, keep, remove))
                    {
                        return false;
                    }

                    state.selection.clear();
                    return true;
                });

            if (!applied)
            {
                qDebug().noquote()
                    << "Direct two-Sector join rejected for Sector indices"
                    << keep << "and" << remove
                    << ": no compatible overlap/adjoin-safe result";
                QMessageBox::warning(this, "Join sectors failed", "Sectors could not be joined.");
                return;
            }

            invalidateSurfaceSelectionContextForTopologyEdit();

            glView_->notifyLevelGeometryChanged();
            glView_->setSelection(-1, -1, -1, -1);
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
        });
    sectorMenu->addAction(connectSectorsAction_);
    setupSectorRecipeActions(sectorMenu);
    // Step 2: delegate the transform submenu once the entity submenus exist.
    setupTransformMenu(transformMenu);
}

// Native replacements for legacy JED plug-ins (Vertex Assassin, Light
// Fantastic, Stair Crazy, Water Works). Each command is one undo step.
void MainWindow::setupSectorRecipeActions(QMenu* sectorMenu)
{
    const auto selectedSectors = [this]()
        {
            const editor::Selection& selection = document_.selection();
            std::vector<std::int32_t> sectors(selection.sectors.begin(), selection.sectors.end());
            std::sort(sectors.begin(), sectors.end());
            return sectors;
        };
    sectorMenu->addSeparator();

    QAction* const cleanupAction = new QAction("Remove Redundant &Vertices", this);
    cleanupAction->setObjectName("sector.remove_redundant_vertices");
    cleanupAction->setToolTip(
        "Merge duplicate vertices and remove unused and collinear vertices from the selected sectors");
    connect(cleanupAction, &QAction::triggered, this, [this, selectedSectors]()
        {
            const std::vector<std::int32_t> sectors = selectedSectors();
            if ( sectors.empty() )
            {
                QMessageBox::information(this, "Remove Redundant Vertices", "Select one or more sectors first.");
                return;
            }
            level::VertexCleanupResult result{};
            const bool applied = document_.applyUndoable("Sectors: Removed redundant vertices",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    result = level::levelRemoveRedundantVertices(state.level, sectors);
                    if ( result.removed() == 0 )
                    {
                        return false;
                    }
                    state.selection.surfaceVertices.clear();
                    return true;
                });
            QString message = applied
                ? QStringLiteral("Removed %1 vertices (%2 duplicate, %3 unused, %4 collinear)")
                    .arg(result.removed()).arg(result.mergedDuplicates).arg(result.removedUnused)
                    .arg(result.removedCollinear)
                : QStringLiteral("No redundant vertices found");
            if ( result.skippedSectors > 0 )
            {
                message += QStringLiteral("; %1 sector(s) skipped because merging would collapse a surface")
                    .arg(result.skippedSectors);
            }
            if ( applied )
            {
                invalidateSurfaceSelectionContextForTopologyEdit();
                glView_->notifyLevelGeometryChanged();
                glView_->setHighlightedSelection(document_.selection());
                rebuildProperties();
                updateEditActions();
            }
            logMessage(result.skippedSectors > 0 ? MessageSeverity::warning : MessageSeverity::info, message);
            glView_->showToast(message, 2600, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(cleanupAction);

    QAction* const lightsAction = new QAction("Place Sector &Lights...", this);
    lightsAction->setObjectName("sector.place_lights");
    lightsAction->setToolTip("Add one light at the center of each selected sector, sized to reach its walls");
    connect(lightsAction, &QAction::triggered, this, [this, selectedSectors]()
        {
            const QString title = QStringLiteral("Place Sector Lights");
            const std::vector<std::int32_t> sectors = selectedSectors();
            if ( sectors.empty() )
            {
                QMessageBox::information(this, title, "Select one or more sectors first.");
                return;
            }
            bool accepted = false;
            const double intensity = QInputDialog::getDouble(this, title, "Light intensity:",
                level::kDefaultSectorLightIntensity, level::kMinSectorLightIntensity,
                level::kMaxSectorLightIntensity, 2, &accepted);
            if ( !accepted )
            {
                return;
            }
            std::size_t placed = 0;
            const bool applied = document_.applyUndoable("Lights: Placed sector lights",
                editor::DocumentDirty::Lights | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    placed = level::levelPlaceSectorLights(state.level, sectors, intensity).size();
                    return placed > 0;
                });
            const QString message = applied
                ? QStringLiteral("Placed %1 sector light(s)").arg(placed)
                : QStringLiteral("No lights placed: the selected sectors have no interior center");
            if ( applied )
            {
                glView_->notifyLightsChanged();
                rebuildOutliner();
                rebuildProperties();
                updateEditActions();
            }
            logMessage(applied ? MessageSeverity::info : MessageSeverity::warning, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(lightsAction);

    QAction* const underwaterAction = new QAction("Make &Underwater...", this);
    underwaterAction->setObjectName("sector.make_underwater");
    underwaterAction->setToolTip("Flag the selected sectors as underwater and set their tint and ambient sound");
    connect(underwaterAction, &QAction::triggered, this, [this, selectedSectors]()
        {
            const QString title = QStringLiteral("Make Underwater");
            const std::vector<std::int32_t> sectors = selectedSectors();
            if ( sectors.empty() )
            {
                QMessageBox::information(this, title, "Select one or more sectors first.");
                return;
            }
            QDialog dialog(this);
            dialog.setObjectName("sector.make_underwater.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            const auto channel = [&dialog](const char* name, const double value)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(0.0, 1.0);
                    box->setDecimals(3);
                    box->setSingleStep(0.05);
                    box->setValue(value);
                    return box;
                };
            QDoubleSpinBox* const red = channel("tint_red", 0.0);
            QDoubleSpinBox* const green = channel("tint_green", 0.3);
            QDoubleSpinBox* const blue = channel("tint_blue", 0.5);
            auto* const sound = new QLineEdit(&dialog);
            sound->setObjectName("sound");
            QDoubleSpinBox* const volume = channel("sound_volume", 1.0);
            form->addRow("Tint red:", red);
            form->addRow("Tint green:", green);
            form->addRow("Tint blue:", blue);
            form->addRow("Ambient sound:", sound);
            form->addRow("Sound volume:", volume);
            auto* const waterSurfaces = new QCheckBox("Make adjoins to dry sectors water surfaces", &dialog);
            waterSurfaces->setObjectName("water_surfaces");
            waterSurfaces->setChecked(true);
            waterSurfaces->setToolTip(
                "Both sides become visible, passable, translucent and non-solid so swimmers can surface");
            auto* const waterMaterial = new QLineEdit(&dialog);
            waterMaterial->setObjectName("water_material");
            waterMaterial->setPlaceholderText("Keep current material");
            form->addRow(waterSurfaces);
            form->addRow("Water surface material:", waterMaterial);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            level::UnderwaterSettings settings{};
            settings.tint = ColorF(static_cast<float>(red->value()), static_cast<float>(green->value()),
                static_cast<float>(blue->value()));
            settings.sound = sound->text().trimmed().toStdString();
            settings.soundVolume = volume->value();
            settings.configureWaterSurfaces = waterSurfaces->isChecked();
            settings.waterSurfaceMaterial = waterMaterial->text().trimmed().toStdString();
            level::UnderwaterResult changed{};
            const bool applied = document_.applyUndoable("Sectors: Made underwater",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    changed = level::levelMakeUnderwaterSectors(state.level, sectors, settings);
                    return changed.changed() > 0;
                });
            const QString message = applied
                ? QStringLiteral("Made %1 sector(s) underwater; updated %2 water surface side(s)")
                    .arg(changed.sectors).arg(changed.boundarySurfaces)
                : QStringLiteral("Selected sectors are already underwater with these settings");
            if ( applied )
            {
                glView_->notifyLevelGeometryChanged();
                rebuildProperties();
                updateEditActions();
                logMessage(MessageSeverity::info, message);
            }
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(underwaterAction);

    QAction* const stairsAction = new QAction("Build &Stairs...", this);
    stairsAction->setObjectName("sector.build_stairs");
    stairsAction->setToolTip("Build a flight of adjoined box sectors starting at the grid origin");
    connect(stairsAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Build Stairs");
            QDialog dialog(this);
            dialog.setObjectName("sector.build_stairs.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            auto* const direction = new QComboBox(&dialog);
            direction->setObjectName("direction");
            direction->addItems({ "+X", "-X", "+Y", "-Y" });
            const auto length = [&dialog](const char* name, const double value)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(0.001, 100.0);
                    box->setDecimals(4);
                    box->setSingleStep(0.025);
                    box->setValue(value);
                    return box;
                };
            QDoubleSpinBox* const width = length("width", 1.0);
            QDoubleSpinBox* const run = length("run", 0.25);
            QDoubleSpinBox* const rise = length("rise", 0.125);
            QDoubleSpinBox* const headroom = length("headroom", 1.0);
            auto* const steps = new QSpinBox(&dialog);
            steps->setObjectName("steps");
            steps->setRange(1, static_cast<int>(level::kMaximumStairSteps));
            steps->setValue(4);
            form->addRow("Direction:", direction);
            form->addRow("Width:", width);
            form->addRow("Step run:", run);
            form->addRow("Step rise:", rise);
            form->addRow("Headroom:", headroom);
            form->addRow("Steps:", steps);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            constexpr level::StairDirection kDirections[] = { level::StairDirection::PositiveX,
                level::StairDirection::NegativeX, level::StairDirection::PositiveY,
                level::StairDirection::NegativeY };
            level::StairParameters parameters{};
            const QVector3D origin = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            parameters.origin = TVector::Create(origin.x(), origin.y(), origin.z());
            parameters.direction = kDirections[std::clamp(direction->currentIndex(), 0, 3)];
            parameters.width = width->value();
            parameters.run = run->value();
            parameters.rise = rise->value();
            parameters.headroom = headroom->value();
            parameters.steps = steps->value();
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Built stairs",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    std::optional<std::vector<std::int32_t>> result = level::levelBuildStairs(state.level, parameters);
                    if ( !result )
                    {
                        return false;
                    }
                    created = std::move(*result);
                    state.selection.clear();
                    state.selection.sectors.insert(created.begin(), created.end());
                    return true;
                });
            if ( !applied )
            {
                QMessageBox::information(this, title, "The stairs could not be built with these parameters.");
                return;
            }
            invalidateSurfaceSelectionContextForTopologyEdit();
            glView_->notifyLevelGeometryChanged();
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Built %1 stair step(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(stairsAction);
}

} // namespace sed
