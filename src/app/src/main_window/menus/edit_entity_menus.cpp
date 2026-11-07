#include "main_window/common.hpp"
#include "ui/shortcuts/key_sequence_display.hpp"

#include "app_logging.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/material_dimensions.hpp"
#include "sed/level/component_rebuild.hpp"
#include "sed/level/door.hpp"
#include "sed/level/prefab.hpp"
#include "sed/level/recipe_requests.hpp"
#include "sed/level/recipes.hpp"
#include "sed/level/thing_frames.hpp"
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
#include <QFile>
#include <QFileDialog>
#include <QPlainTextEdit>
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

            const std::vector<SurfaceRef> selectedSurfaces(selection.surfaces.begin(), selection.surfaces.end());
            const std::vector<SurfaceRef> targets =
                ::sed::level::planarizeTargets(document_.level(), selectedSurfaces);
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

    QAction* const insetAction = new QAction("&Inset Surface...", this);
    insetAction->setObjectName("surface.inset");
    insetAction->setToolTip("Scale the selected surface toward its centroid; an adjoined surface insets both sides together");
    connect(insetAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Inset Surface");
            const editor::Selection& selection = document_.selection();
            if ( selection.surfaces.size() != 1u )
            {
                QMessageBox::information(this, title, "Select exactly one surface.");
                return;
            }
            const SurfaceRef target = *selection.surfaces.begin();
            bool accepted = false;
            const double factor = QInputDialog::getDouble(this, title, "Inset factor (0-1):",
                0.5, 0.01, 0.99, 2, &accepted);
            if ( !accepted )
            {
                return;
            }
            if ( const auto failure = level::validateInsetFactor(factor) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            const Level& current = document_.level();
            const bool adjoined = target.sector >= 0
                && static_cast<std::size_t>(target.sector) < current.sectors.size()
                && target.surface >= 0
                && static_cast<std::size_t>(target.surface)
                    < current.sectors[static_cast<std::size_t>(target.sector)].surfaces.size()
                && current.sectors[static_cast<std::size_t>(target.sector)]
                    .surfaces[static_cast<std::size_t>(target.surface)].adjoin.has_value();
            const bool applied = document_.applyUndoable("Surfaces: Inset",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    return adjoined ? level::levelInsetAdjoinPair(state.level, target, factor)
                                    : level::levelInsetSurface(state.level, target, factor);
                });
            if ( !applied )
            {
                QMessageBox::information(this, title,
                    "The surface could not be inset without breaking planarity or convexity.");
                return;
            }
            glView_->notifyLevelGeometryChanged();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Inset the selected surface");
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    surfaceMenu->addAction(insetAction);

    // Absorbs the legacy Flagman/SkyFlagger/Vinkel batch plug-ins: one
    // masked recipe over the selected surfaces (E-02), mirroring the
    // agent applyMaskedSurfaceRecipe operation.
    QAction* const maskedRecipeAction = new QAction("Masked &Recipe...", this);
    maskedRecipeAction->setObjectName("surface.masked_recipe");
    maskedRecipeAction->setToolTip(
        "Apply one masked material/flag recipe to the selected surfaces (slope band and flag masks)");
    connect(maskedRecipeAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Masked Surface Recipe");
            const editor::Selection& selection = document_.selection();
            if ( selection.surfaces.empty() )
            {
                QMessageBox::information(this, title, "Select one or more surfaces first.");
                return;
            }
            const std::vector<SurfaceRef> candidates(selection.surfaces.begin(), selection.surfaces.end());
            QDialog dialog(this);
            dialog.setObjectName("surface.masked_recipe.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            const auto slope = [&dialog](const char* name, const double value)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(0.0, 90.0);
                    box->setDecimals(1);
                    box->setSingleStep(5.0);
                    box->setValue(value);
                    return box;
                };
            const auto hex = [&dialog](const char* name)
                {
                    auto* const edit = new QLineEdit(&dialog);
                    edit->setObjectName(QString::fromLatin1(name));
                    edit->setPlaceholderText(QStringLiteral("hex, blank = none"));
                    return edit;
                };
            QDoubleSpinBox* const slopeMin = slope("slope_min", 0.0);
            QDoubleSpinBox* const slopeMax = slope("slope_max", 90.0);
            QLineEdit* const selSurfAny = hex("sel_surf_any");
            QLineEdit* const selSurfAll = hex("sel_surf_all");
            QLineEdit* const selSurfNone = hex("sel_surf_none");
            QLineEdit* const selFaceAny = hex("sel_face_any");
            QLineEdit* const selFaceAll = hex("sel_face_all");
            QLineEdit* const selFaceNone = hex("sel_face_none");
            QLineEdit* const selAdjAny = hex("sel_adj_any");
            QLineEdit* const selAdjAll = hex("sel_adj_all");
            QLineEdit* const selAdjNone = hex("sel_adj_none");
            auto* const material = new QLineEdit(&dialog);
            material->setObjectName("material");
            material->setPlaceholderText(QStringLiteral("blank = keep material"));
            QLineEdit* const surfSet = hex("surf_set");
            QLineEdit* const surfClear = hex("surf_clear");
            QLineEdit* const faceSet = hex("face_set");
            QLineEdit* const faceClear = hex("face_clear");
            QLineEdit* const adjSet = hex("adj_set");
            QLineEdit* const adjClear = hex("adj_clear");
            const auto copyCheck = [&dialog](const char* name, const QString& label)
                {
                    auto* const box = new QCheckBox(label, &dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    return box;
                };
            QCheckBox* const copyMaterial = copyCheck("copy_material", "Copy material");
            QCheckBox* const copyGeometry = copyCheck("copy_geometry", "Copy geometry mode");
            QCheckBox* const copyExtraLight = copyCheck("copy_extra_light", "Copy extra light");
            QCheckBox* const copySurfaceFlags = copyCheck("copy_surface_flags", "Copy surface flags");
            QCheckBox* const copyFaceFlags = copyCheck("copy_face_flags", "Copy face flags");
            QCheckBox* const copyAdjoinFlags = copyCheck("copy_adjoin_flags", "Copy adjoin flags");
            QCheckBox* const copyUvScale = copyCheck("copy_uv_scale", "Copy UV scale");
            form->addRow("Slope min (deg):", slopeMin);
            form->addRow("Slope max (deg):", slopeMax);
            form->addRow("Surface flags any/all/none:", selSurfAny);
            form->addRow("", selSurfAll);
            form->addRow("", selSurfNone);
            form->addRow("Face flags any/all/none:", selFaceAny);
            form->addRow("", selFaceAll);
            form->addRow("", selFaceNone);
            form->addRow("Adjoin flags any/all/none:", selAdjAny);
            form->addRow("", selAdjAll);
            form->addRow("", selAdjNone);
            form->addRow("Material override:", material);
            form->addRow("Set/clear surface flags:", surfSet);
            form->addRow("", surfClear);
            form->addRow("Set/clear face flags:", faceSet);
            form->addRow("", faceClear);
            form->addRow("Set/clear adjoin flags:", adjSet);
            form->addRow("", adjClear);
            form->addRow("Copy from first selected:", copyMaterial);
            form->addRow("", copyGeometry);
            form->addRow("", copyExtraLight);
            form->addRow("", copySurfaceFlags);
            form->addRow("", copyFaceFlags);
            form->addRow("", copyAdjoinFlags);
            form->addRow("", copyUvScale);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            const auto parseHex = [this, title](const QLineEdit* edit, std::optional<std::uint32_t>& out)
            {
                const QString text = edit->text().trimmed();
                if ( text.isEmpty() )
                {
                    out.reset();
                    return true;
                }
                bool ok = false;
                const std::uint32_t value = text.toUInt(&ok, 16);
                if ( !ok )
                {
                    QMessageBox::information(this, title,
                        QStringLiteral("'%1' is not a hexadecimal flag mask.").arg(text));
                    return false;
                }
                out = value;
                return true;
            };
            const auto maskOf = [&parseHex](const QLineEdit* any, const QLineEdit* all, const QLineEdit* none,
                                    std::optional<level::FlagMaskPredicate>& out)
            {
                std::optional<std::uint32_t> anyValue{};
                std::optional<std::uint32_t> allValue{};
                std::optional<std::uint32_t> noneValue{};
                if ( !parseHex(any, anyValue) || !parseHex(all, allValue) || !parseHex(none, noneValue) )
                {
                    return false;
                }
                if ( !anyValue && !allValue && !noneValue )
                {
                    out.reset();
                    return true;
                }
                out = level::FlagMaskPredicate{ anyValue, allValue, noneValue };
                return true;
            };
            const auto bitsOf = [&parseHex](const QLineEdit* edit, std::uint32_t& out)
            {
                std::optional<std::uint32_t> value{};
                if ( !parseHex(edit, value) )
                {
                    return false;
                }
                out = value.value_or(0u);
                return true;
            };
            level::MaskedSurfaceRecipe recipe{};
            recipe.minSlopeDegrees = slopeMin->value();
            recipe.maxSlopeDegrees = slopeMax->value();
            if ( !maskOf(selSurfAny, selSurfAll, selSurfNone, recipe.surfaceFlags)
                || !maskOf(selFaceAny, selFaceAll, selFaceNone, recipe.faceFlags)
                || !maskOf(selAdjAny, selAdjAll, selAdjNone, recipe.adjoinFlags)
                || !bitsOf(surfSet, recipe.setSurfaceFlags) || !bitsOf(surfClear, recipe.clearSurfaceFlags)
                || !bitsOf(faceSet, recipe.setFaceFlags) || !bitsOf(faceClear, recipe.clearFaceFlags)
                || !bitsOf(adjSet, recipe.setAdjoinFlags) || !bitsOf(adjClear, recipe.clearAdjoinFlags) )
            {
                return;
            }
            if ( !material->text().trimmed().isEmpty() )
            {
                recipe.material = material->text().trimmed().toStdString();
            }
            const auto copyBit = [](const QCheckBox* box, const level::MaskedCopyField field, std::uint32_t& bits)
            {
                if ( box->isChecked() )
                {
                    bits |= static_cast<std::uint32_t>(field);
                }
            };
            copyBit(copyMaterial, level::MaskedCopyField::Material, recipe.copyFields);
            copyBit(copyGeometry, level::MaskedCopyField::Geometry, recipe.copyFields);
            copyBit(copyExtraLight, level::MaskedCopyField::ExtraLight, recipe.copyFields);
            copyBit(copySurfaceFlags, level::MaskedCopyField::SurfaceFlags, recipe.copyFields);
            copyBit(copyFaceFlags, level::MaskedCopyField::FaceFlags, recipe.copyFields);
            copyBit(copyAdjoinFlags, level::MaskedCopyField::AdjoinFlags, recipe.copyFields);
            copyBit(copyUvScale, level::MaskedCopyField::UvScale, recipe.copyFields);
            if ( recipe.copyFields != 0u )
            {
                recipe.copyFrom = candidates.front();
            }
            if ( const auto failure = level::validateMaskedSurfaceRecipe(recipe) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            level::MaskedSurfaceResult result{};
            const bool applied = document_.applyUndoable("Surfaces: Masked recipe",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    result = level::levelApplyMaskedSurfaceRecipe(state.level, candidates, recipe);
                    return result.changed > 0u;
                });
            if ( !applied )
            {
                QMessageBox::information(this, title, "No selected surface matched the recipe, or nothing changed.");
                return;
            }
            glView_->notifyLevelGeometryChanged();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Applied masked recipe to %1 surface(s)").arg(result.changed);
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    surfaceMenu->addAction(maskedRecipeAction);

    QAction* const subdivideAction = new QAction("Sub&divide Surfaces", this);
    subdivideAction->setObjectName("surface.subdivide");
    subdivideAction->setToolTip("Fan each selected convex surface into triangles around a new center vertex");
    connect(subdivideAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Subdivide Surfaces");
            const editor::Selection& selection = document_.selection();
            if ( selection.surfaces.empty() )
            {
                QMessageBox::information(this, title, "Select one or more surfaces first.");
                return;
            }
            const std::vector<SurfaceRef> targets(selection.surfaces.begin(), selection.surfaces.end());
            std::size_t subdivided = 0;
            const bool applied = document_.applyUndoable("Surfaces: Subdivided",
                editor::DocumentDirty::LevelGeometry,
                [&](editor::DocumentState& state) -> bool
                {
                    subdivided = level::levelSubdivideSurfaces(state.level, targets);
                    if ( subdivided == 0u )
                    {
                        return false;
                    }
                    state.selection.surfaceVertices.clear();
                    return true;
                });
            const QString message = applied
                ? QStringLiteral("Subdivided %1 surface(s)").arg(subdivided)
                : QStringLiteral("No selected surface could be subdivided");
            if ( applied )
            {
                invalidateSurfaceSelectionContextForTopologyEdit();
                glView_->notifyLevelGeometryChanged();
                glView_->setHighlightedSelection(document_.selection());
                rebuildProperties();
                updateEditActions();
            }
            logMessage(applied ? MessageSeverity::info : MessageSeverity::warning, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    surfaceMenu->addAction(subdivideAction);

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
    QAction* const buildPathAction = new QAction("Build Thing Path...", this);
    buildPathAction->setObjectName("thing.build_path");
    buildPathAction->setToolTip("Replace the selected Thing's path frames with the listed waypoints (one per line: x y z pitch yaw roll)");
    connect(buildPathAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Build Thing Path");
            const editor::Selection& selection = document_.selection();
            if ( selection.things.size() != 1u )
            {
                QMessageBox::information(this, title, "Select exactly one Thing first.");
                return;
            }
            const std::int32_t thing = *selection.things.begin();
            QDialog dialog(this);
            dialog.setObjectName("thing.build_path.dialog");
            dialog.setWindowTitle(title);
            auto* const layout = new QVBoxLayout(&dialog);
            layout->addWidget(new QLabel(
                "One waypoint per line: x y z pitch yaw roll", &dialog));
            auto* const waypoints = new QPlainTextEdit(&dialog);
            waypoints->setObjectName("waypoints");
            if ( thing >= 0 && static_cast<std::size_t>(thing) < document_.level().things.size() )
            {
                const Thing& current = document_.level().things[static_cast<std::size_t>(thing)];
                waypoints->setPlainText(QStringLiteral("%1 %2 %3 %4 %5 %6")
                    .arg(current.x).arg(current.y).arg(current.z)
                    .arg(current.pch).arg(current.yaw).arg(current.rol));
            }
            layout->addWidget(waypoints);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            std::vector<FrameValue> frames;
            bool ok = true;
            const QStringList lines = waypoints->toPlainText().split('\n');
            for ( const QString& rawLine : lines )
            {
                const QString line = rawLine.trimmed();
                if ( line.isEmpty() )
                {
                    continue;
                }
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if ( parts.size() != 6 )
                {
                    ok = false;
                    break;
                }
                double values[6]{};
                for ( int i = 0; i < 6; ++i )
                {
                    bool partOk = false;
                    values[i] = parts[i].toDouble(&partOk);
                    if ( !partOk )
                    {
                        ok = false;
                        break;
                    }
                }
                if ( !ok )
                {
                    break;
                }
                frames.push_back(FrameValue{ values[0], values[1], values[2], values[3], values[4], values[5] });
            }
            if ( !ok || frames.empty() || frames.size() > level::kMaxThingFrames )
            {
                QMessageBox::information(this, title,
                    QStringLiteral("Enter 1 to %1 waypoints, each as six numbers: x y z pitch yaw roll.")
                        .arg(level::kMaxThingFrames));
                return;
            }
            if ( const auto prepared = level::prepareSetThingFrames(document_.level(), nullptr,
                    static_cast<std::size_t>(thing), frames);
                !prepared.ok() )
            {
                QMessageBox::information(this, title, QString::fromStdString(prepared.failure->message));
                return;
            }
            std::size_t written = 0;
            const bool applied = document_.applyUndoable("Things: Built path",
                editor::DocumentDirty::Things,
                [&](editor::DocumentState& state) -> bool
                {
                    const std::optional<std::size_t> result =
                        level::levelBuildThingPath(state.level, nullptr, thing, frames);
                    if ( !result )
                    {
                        return false;
                    }
                    written = *result;
                    return true;
                });
            if ( !applied )
            {
                QMessageBox::information(this, title, "The Thing path could not be built.");
                return;
            }
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Built a %1-frame Thing path").arg(written);
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    thingMenu->addAction(buildPathAction);

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

    QAction* const switchbackAction = new QAction("Build Switchback Stairs...", this);
    switchbackAction->setObjectName("sector.build_switchback_stairs");
    switchbackAction->setToolTip("Build two flights joined by a landing, starting at the grid origin");
    connect(switchbackAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Build Switchback Stairs");
            QDialog dialog(this);
            dialog.setObjectName("sector.build_switchback_stairs.dialog");
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
            QDoubleSpinBox* const landingRun = length("landing_run", 0.5);
            auto* const steps = new QSpinBox(&dialog);
            steps->setObjectName("steps");
            steps->setRange(1, static_cast<int>(level::kMaximumStairSteps / 2));
            steps->setValue(2);
            form->addRow("Direction:", direction);
            form->addRow("Width:", width);
            form->addRow("Step run:", run);
            form->addRow("Step rise:", rise);
            form->addRow("Headroom:", headroom);
            form->addRow("Landing run:", landingRun);
            form->addRow("Steps per flight:", steps);
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
            level::SwitchbackStairParameters parameters{};
            const QVector3D origin = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            parameters.origin = TVector::Create(origin.x(), origin.y(), origin.z());
            parameters.direction = kDirections[std::clamp(direction->currentIndex(), 0, 3)];
            parameters.width = width->value();
            parameters.run = run->value();
            parameters.rise = rise->value();
            parameters.headroom = headroom->value();
            parameters.landingRun = landingRun->value();
            parameters.stepsPerFlight = steps->value();
            if ( const auto failure = level::validateSwitchbackStairParameters(parameters) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Built switchback stairs",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    std::optional<std::vector<std::int32_t>> result =
                        level::levelBuildSwitchbackStairs(state.level, parameters);
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
            const QString message = QStringLiteral("Built %1 switchback stair sector(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(switchbackAction);

    QAction* const spiralAction = new QAction("Build Spiral Stairs...", this);
    spiralAction->setObjectName("sector.build_spiral_stairs");
    spiralAction->setToolTip("Build a spiral flight of wedge sectors around the grid origin");
    connect(spiralAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Build Spiral Stairs");
            QDialog dialog(this);
            dialog.setObjectName("sector.build_spiral_stairs.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
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
            const auto angle = [&dialog](const char* name, const double value, const double minimum,
                                 const double maximum)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(minimum, maximum);
                    box->setDecimals(2);
                    box->setSingleStep(5.0);
                    box->setValue(value);
                    return box;
                };
            QDoubleSpinBox* const innerRadius = length("inner_radius", 0.5);
            QDoubleSpinBox* const outerRadius = length("outer_radius", 1.5);
            QDoubleSpinBox* const startAngle = angle("start_angle", 0.0, -3600.0, 3600.0);
            QDoubleSpinBox* const stepAngle = angle("step_angle", 30.0, -179.0, 179.0);
            QDoubleSpinBox* const rise = length("rise", 0.125);
            QDoubleSpinBox* const headroom = length("headroom", 1.0);
            auto* const steps = new QSpinBox(&dialog);
            steps->setObjectName("steps");
            steps->setRange(1, static_cast<int>(level::kMaximumStairSteps));
            steps->setValue(4);
            form->addRow("Inner radius:", innerRadius);
            form->addRow("Outer radius:", outerRadius);
            form->addRow("Start angle (deg):", startAngle);
            form->addRow("Step angle (deg):", stepAngle);
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
            level::SpiralStairParameters parameters{};
            const QVector3D center = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            parameters.center = TVector::Create(center.x(), center.y(), center.z());
            parameters.innerRadius = innerRadius->value();
            parameters.outerRadius = outerRadius->value();
            parameters.startAngleDegrees = startAngle->value();
            parameters.stepAngleDegrees = stepAngle->value();
            parameters.rise = rise->value();
            parameters.headroom = headroom->value();
            parameters.steps = steps->value();
            if ( const auto failure = level::validateSpiralStairParameters(parameters) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Built spiral stairs",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    std::optional<std::vector<std::int32_t>> result =
                        level::levelBuildSpiralStairs(state.level, parameters);
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
            const QString message = QStringLiteral("Built %1 spiral stair sector(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(spiralAction);

    QAction* const domeAction = new QAction("Build Dome...", this);
    domeAction->setObjectName("sector.build_dome");
    domeAction->setToolTip("Build a stepped-hemisphere dome of adjoined wedge sectors around the grid origin");
    connect(domeAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Build Dome");
            QDialog dialog(this);
            dialog.setObjectName("sector.build_dome.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            const auto length = [&dialog](const char* name, const double value)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(0.001, 100.0);
                    box->setDecimals(4);
                    box->setSingleStep(0.05);
                    box->setValue(value);
                    return box;
                };
            QDoubleSpinBox* const radius = length("radius", 2.0);
            QDoubleSpinBox* const headroom = length("headroom", 1.0);
            auto* const segments = new QSpinBox(&dialog);
            segments->setObjectName("segments");
            segments->setRange(3, 64);
            segments->setValue(8);
            auto* const rings = new QSpinBox(&dialog);
            rings->setObjectName("rings");
            rings->setRange(1, 64);
            rings->setValue(4);
            form->addRow("Radius:", radius);
            form->addRow("Headroom:", headroom);
            form->addRow("Segments:", segments);
            form->addRow("Rings:", rings);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            level::DomeParameters parameters{};
            const QVector3D center = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            parameters.center = TVector::Create(center.x(), center.y(), center.z());
            parameters.radius = radius->value();
            parameters.headroom = headroom->value();
            parameters.segments = segments->value();
            parameters.rings = rings->value();
            if ( const auto failure = level::validateDomeParameters(parameters) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Built dome",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    std::optional<std::vector<std::int32_t>> result =
                        level::levelBuildDome(state.level, parameters);
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
                QMessageBox::information(this, title, "The dome could not be built with these parameters.");
                return;
            }
            invalidateSurfaceSelectionContextForTopologyEdit();
            glView_->notifyLevelGeometryChanged();
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Built %1 dome sector(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(domeAction);

    QAction* const sweptAction = new QAction("Build Swept Corridor...", this);
    sweptAction->setObjectName("sector.build_swept_corridor");
    sweptAction->setToolTip("Sweep a corridor along an arc around the grid origin (flat or rising)");
    connect(sweptAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Build Swept Corridor");
            QDialog dialog(this);
            dialog.setObjectName("sector.build_swept_corridor.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            const auto length = [&dialog](const char* name, const double value, const double minimum,
                                const double maximum)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(minimum, maximum);
                    box->setDecimals(4);
                    box->setSingleStep(0.05);
                    box->setValue(value);
                    return box;
                };
            QDoubleSpinBox* const innerRadius = length("inner_radius", 1.0, 0.001, 100.0);
            QDoubleSpinBox* const outerRadius = length("outer_radius", 2.0, 0.001, 100.0);
            QDoubleSpinBox* const startAngle = length("start_angle", 0.0, -3600.0, 3600.0);
            QDoubleSpinBox* const sweep = length("sweep", 90.0, -3600.0, 3600.0);
            auto* const segments = new QSpinBox(&dialog);
            segments->setObjectName("segments");
            segments->setRange(1, static_cast<int>(level::kMaximumStairSteps));
            segments->setValue(3);
            QDoubleSpinBox* const rise = length("rise", 0.0, -100.0, 100.0);
            QDoubleSpinBox* const headroom = length("headroom", 1.0, 0.001, 100.0);
            form->addRow("Inner radius:", innerRadius);
            form->addRow("Outer radius:", outerRadius);
            form->addRow("Start angle (deg):", startAngle);
            form->addRow("Sweep (deg):", sweep);
            form->addRow("Segments:", segments);
            form->addRow("Rise per segment:", rise);
            form->addRow("Headroom:", headroom);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            level::SweptCorridorParameters parameters{};
            const QVector3D center = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            parameters.center = TVector::Create(center.x(), center.y(), center.z());
            parameters.innerRadius = innerRadius->value();
            parameters.outerRadius = outerRadius->value();
            parameters.startAngleDegrees = startAngle->value();
            parameters.sweepDegrees = sweep->value();
            parameters.segments = segments->value();
            parameters.risePerSegment = rise->value();
            parameters.headroom = headroom->value();
            if ( const auto failure = level::validateSweptCorridorParameters(parameters) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Built swept corridor",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    std::optional<std::vector<std::int32_t>> result =
                        level::levelBuildSweptCorridor(state.level, parameters);
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
                QMessageBox::information(this, title, "The corridor could not be built with these parameters.");
                return;
            }
            invalidateSurfaceSelectionContextForTopologyEdit();
            glView_->notifyLevelGeometryChanged();
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Built %1 swept corridor sector(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(sweptAction);

    QAction* const reportAction = new QAction("Report Redundant Vertices", this);
    reportAction->setObjectName("sector.report_redundant_vertices");
    reportAction->setToolTip("List what Remove Redundant Vertices would change, without editing the level");
    connect(reportAction, &QAction::triggered, this, [this, selectedSectors]()
        {
            const std::vector<std::int32_t> sectors = selectedSectors();
            if ( sectors.empty() )
            {
                QMessageBox::information(this, "Report Redundant Vertices", "Select one or more sectors first.");
                return;
            }
            const std::vector<level::VertexCleanupCandidate> report =
                level::levelReportRedundantVertices(document_.level(), sectors);
            std::size_t total = 0;
            std::size_t skipped = 0;
            for ( const level::VertexCleanupCandidate& entry : report )
            {
                total += entry.removed();
                skipped += entry.wouldSkip ? 1u : 0u;
            }
            const QString message = QStringLiteral("Report: %1 redundant vertex candidate(s) in %2 sector(s)")
                .arg(total).arg(report.size())
                + (skipped > 0 ? QStringLiteral("; %1 sector(s) would be skipped").arg(skipped) : QString{});
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2600, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(reportAction);

    QAction* const terrainAction = new QAction("Build Heightmap Terrain...", this);
    terrainAction->setObjectName("sector.build_heightmap_terrain");
    terrainAction->setToolTip("Build a grid of adjoined box sectors from per-cell floor heights, starting at the grid origin");
    connect(terrainAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Build Heightmap Terrain");
            QDialog dialog(this);
            dialog.setObjectName("sector.build_heightmap_terrain.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            auto* const columns = new QSpinBox(&dialog);
            columns->setObjectName("columns");
            columns->setRange(1, 128);
            columns->setValue(2);
            auto* const rows = new QSpinBox(&dialog);
            rows->setObjectName("rows");
            rows->setRange(1, 128);
            rows->setValue(2);
            const auto length = [&dialog](const char* name, const double value)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(0.001, 100.0);
                    box->setDecimals(4);
                    box->setSingleStep(0.05);
                    box->setValue(value);
                    return box;
                };
            QDoubleSpinBox* const cellSize = length("cell_size", 1.0);
            QDoubleSpinBox* const headroom = length("headroom", 2.0);
            auto* const preset = new QComboBox(&dialog);
            preset->setObjectName("heights_preset");
            preset->addItems({ "Flat (height 0)", "Ramp (+0.25 per column)", "Step (0, then 0.5)" });
            auto* const heightsFile = new QLineEdit(&dialog);
            heightsFile->setObjectName("heights_file");
            heightsFile->setPlaceholderText("Optional: whitespace/comma heights file (overrides preset)");
            form->addRow("Columns:", columns);
            form->addRow("Rows:", rows);
            form->addRow("Cell size:", cellSize);
            form->addRow("Headroom:", headroom);
            form->addRow("Heights:", preset);
            form->addRow("Heights file:", heightsFile);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            level::TerrainParameters parameters{};
            const QVector3D origin = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            parameters.origin = TVector::Create(origin.x(), origin.y(), origin.z());
            parameters.cellSize = cellSize->value();
            parameters.headroom = headroom->value();
            parameters.columns = columns->value();
            parameters.rows = rows->value();
            const std::size_t cellCount = static_cast<std::size_t>(parameters.columns)
                * static_cast<std::size_t>(parameters.rows);
            parameters.heights.assign(cellCount, 0.0);
            const QString heightsPath = heightsFile->text().trimmed();
            if ( !heightsPath.isEmpty() )
            {
                QFile file(heightsPath);
                if ( !file.open(QIODevice::ReadOnly | QIODevice::Text) )
                {
                    QMessageBox::information(this, title, "The heights file could not be opened.");
                    return;
                }
                const QStringList tokens = QString::fromUtf8(file.readAll())
                    .split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
                std::vector<double> parsed;
                bool ok = true;
                for ( const QString& token : tokens )
                {
                    bool tokenOk = false;
                    const double value = token.toDouble(&tokenOk);
                    if ( !tokenOk )
                    {
                        ok = false;
                        break;
                    }
                    parsed.push_back(value);
                }
                if ( !ok || parsed.size() != cellCount )
                {
                    QMessageBox::information(this, title,
                        QStringLiteral("The heights file must contain exactly %1 numbers.").arg(cellCount));
                    return;
                }
                parameters.heights = std::move(parsed);
            }
            else if ( preset->currentIndex() == 1 )
            {
                for ( std::int32_t row = 0; row < parameters.rows; ++row )
                {
                    for ( std::int32_t col = 0; col < parameters.columns; ++col )
                    {
                        parameters.heights[static_cast<std::size_t>(row * parameters.columns + col)] = col * 0.25;
                    }
                }
            }
            else if ( preset->currentIndex() == 2 )
            {
                for ( std::int32_t row = 0; row < parameters.rows; ++row )
                {
                    for ( std::int32_t col = 0; col < parameters.columns; ++col )
                    {
                        parameters.heights[static_cast<std::size_t>(row * parameters.columns + col)] =
                            col * 2 >= parameters.columns ? 0.5 : 0.0;
                    }
                }
            }
            if ( const auto failure = level::validateTerrainParameters(parameters) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Built heightmap terrain",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    std::optional<std::vector<std::int32_t>> result =
                        level::levelBuildHeightmapTerrain(state.level, parameters);
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
                QMessageBox::information(this, title, "The terrain could not be built with these parameters.");
                return;
            }
            invalidateSurfaceSelectionContextForTopologyEdit();
            glView_->notifyLevelGeometryChanged();
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Built %1 terrain sector(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(terrainAction);

    // E-14 model output: the same heightmap exported as a 3DO model
    // (the 3DO Heightmap Generator plug-in), without touching the level.
    QAction* const terrainModelAction = new QAction("Export Heightmap Terrain Model (3DO)...", this);
    terrainModelAction->setObjectName("sector.export_heightmap_terrain_model");
    terrainModelAction->setToolTip("Export a heightmap grid as a single-mesh 3DO terrain model");
    connect(terrainModelAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Export Heightmap Terrain Model");
            QDialog dialog(this);
            dialog.setObjectName("sector.export_heightmap_terrain_model.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            auto* const columns = new QSpinBox(&dialog);
            columns->setObjectName("columns");
            columns->setRange(1, 128);
            columns->setValue(2);
            auto* const rows = new QSpinBox(&dialog);
            rows->setObjectName("rows");
            rows->setRange(1, 128);
            rows->setValue(2);
            const auto length = [&dialog](const char* name, const double value)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(0.001, 100.0);
                    box->setDecimals(4);
                    box->setSingleStep(0.05);
                    box->setValue(value);
                    return box;
                };
            QDoubleSpinBox* const cellSize = length("cell_size", 1.0);
            auto* const preset = new QComboBox(&dialog);
            preset->setObjectName("heights_preset");
            preset->addItems({ "Flat (height 0)", "Ramp (+0.25 per column)", "Step (0, then 0.5)" });
            auto* const heightsFile = new QLineEdit(&dialog);
            heightsFile->setObjectName("heights_file");
            heightsFile->setPlaceholderText("Optional: whitespace/comma heights file (overrides preset)");
            auto* const material = new QLineEdit(&dialog);
            material->setObjectName("material");
            material->setPlaceholderText("blank = dflt.mat");
            form->addRow("Columns:", columns);
            form->addRow("Rows:", rows);
            form->addRow("Cell size:", cellSize);
            form->addRow("Heights:", preset);
            form->addRow("Heights file:", heightsFile);
            form->addRow("Material:", material);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            level::TerrainParameters parameters{};
            const QVector3D origin = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            parameters.origin = TVector::Create(origin.x(), origin.y(), origin.z());
            parameters.cellSize = cellSize->value();
            parameters.headroom = 1.0;
            parameters.columns = columns->value();
            parameters.rows = rows->value();
            const std::size_t cellCount = static_cast<std::size_t>(parameters.columns)
                * static_cast<std::size_t>(parameters.rows);
            parameters.heights.assign(cellCount, 0.0);
            const QString heightsPath = heightsFile->text().trimmed();
            if ( !heightsPath.isEmpty() )
            {
                QFile file(heightsPath);
                if ( !file.open(QIODevice::ReadOnly | QIODevice::Text) )
                {
                    QMessageBox::information(this, title, "The heights file could not be opened.");
                    return;
                }
                const QStringList tokens = QString::fromUtf8(file.readAll())
                    .split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
                std::vector<double> parsed;
                bool ok = true;
                for ( const QString& token : tokens )
                {
                    bool tokenOk = false;
                    const double value = token.toDouble(&tokenOk);
                    if ( !tokenOk )
                    {
                        ok = false;
                        break;
                    }
                    parsed.push_back(value);
                }
                if ( !ok || parsed.size() != cellCount )
                {
                    QMessageBox::information(this, title,
                        QStringLiteral("The heights file must contain exactly %1 numbers.").arg(cellCount));
                    return;
                }
                parameters.heights = std::move(parsed);
            }
            else if ( preset->currentIndex() == 1 )
            {
                for ( std::int32_t row = 0; row < parameters.rows; ++row )
                {
                    for ( std::int32_t col = 0; col < parameters.columns; ++col )
                    {
                        parameters.heights[static_cast<std::size_t>(row * parameters.columns + col)] = col * 0.25;
                    }
                }
            }
            else if ( preset->currentIndex() == 2 )
            {
                for ( std::int32_t row = 0; row < parameters.rows; ++row )
                {
                    for ( std::int32_t col = 0; col < parameters.columns; ++col )
                    {
                        parameters.heights[static_cast<std::size_t>(row * parameters.columns + col)] =
                            col * 2 >= parameters.columns ? 0.5 : 0.0;
                    }
                }
            }
            if ( const auto failure = level::validateTerrainParameters(parameters) )
            {
                QMessageBox::information(this, title, QString::fromStdString(failure->message));
                return;
            }
            const std::optional<assets::Model3do> model = level::levelBuildHeightmapModel(
                parameters, material->text().trimmed().toStdString());
            if ( !model )
            {
                QMessageBox::information(this, title,
                    "The terrain model could not be built; check the material name (no spaces).");
                return;
            }
            std::string text;
            try
            {
                text = assets::saveTo3DO(*model);
            }
            catch ( const std::runtime_error& error )
            {
                QMessageBox::information(this, title, QString::fromStdString(error.what()));
                return;
            }
            const QString filename = QFileDialog::getSaveFileName(
                this, title,
                fileDialogStartLocation(FileDialogHistoryGroup::export_),
                "3DO model (*.3do);;All files (*)");
            if ( filename.isEmpty() )
            {
                return;
            }
            QSaveFile file(filename);
            if ( !file.open(QIODevice::WriteOnly | QIODevice::Text)
                || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())
                || !file.commit() )
            {
                QMessageBox::information(this, title, "The terrain model could not be written.");
                return;
            }
            const QString message = QStringLiteral("Exported terrain model (%1 vertices, %2 faces)")
                .arg(model->meshes[0].vertices.size()).arg(model->meshes[0].faces.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(terrainModelAction);

    // E-03 geometry arrays: repeat the selected sectors as prefab copies
    // over a placement layout (Awesome Arrays / ThingPack geometry side).
    QAction* const sectorArrayAction = new QAction("Build Sector Array...", this);
    sectorArrayAction->setObjectName("sector.build_sector_array");
    sectorArrayAction->setToolTip("Repeat the selected sectors in a linear, grid or radial array");
    connect(sectorArrayAction, &QAction::triggered, this, [this, selectedSectors]()
        {
            const QString title = QStringLiteral("Build Sector Array");
            const std::vector<std::int32_t> sectors = selectedSectors();
            if ( sectors.empty() )
            {
                QMessageBox::information(this, title, "Select one or more sectors first.");
                return;
            }
            QDialog dialog(this);
            dialog.setObjectName("sector.build_sector_array.dialog");
            dialog.setWindowTitle(title);
            auto* const form = new QFormLayout(&dialog);
            auto* const kind = new QComboBox(&dialog);
            kind->setObjectName("kind");
            kind->addItems({ "Linear", "Grid", "Radial" });
            const auto count = [&dialog](const char* name, const int value, const int maximum)
                {
                    auto* const box = new QSpinBox(&dialog);
                    box->setObjectName(QString::fromLatin1(name));
                    box->setRange(1, maximum);
                    box->setValue(value);
                    return box;
                };
            const auto component = [&dialog](const QString& name, const double value)
                {
                    auto* const box = new QDoubleSpinBox(&dialog);
                    box->setObjectName(name);
                    box->setRange(-1000.0, 1000.0);
                    box->setDecimals(4);
                    box->setSingleStep(0.25);
                    box->setValue(value);
                    return box;
                };
            QSpinBox* const countBox = count("count", 3, static_cast<int>(256));
            QSpinBox* const countU = count("count_u", 2, static_cast<int>(256));
            QSpinBox* const countV = count("count_v", 2, static_cast<int>(256));
            QDoubleSpinBox* const stepX = component("step_x", 3.0);
            QDoubleSpinBox* const stepY = component("step_y", 0.0);
            QDoubleSpinBox* const stepZ = component("step_z", 0.0);
            QDoubleSpinBox* const stepUX = component("step_u_x", 3.0);
            QDoubleSpinBox* const stepUY = component("step_u_y", 0.0);
            QDoubleSpinBox* const stepUZ = component("step_u_z", 0.0);
            QDoubleSpinBox* const stepVX = component("step_v_x", 0.0);
            QDoubleSpinBox* const stepVY = component("step_v_y", 3.0);
            QDoubleSpinBox* const stepVZ = component("step_v_z", 0.0);
            QDoubleSpinBox* const radius = component("radius", 5.0);
            radius->setRange(0.001, 1000.0);
            QDoubleSpinBox* const startDegrees = component("start_degrees", 0.0);
            startDegrees->setRange(-3600.0, 3600.0);
            QDoubleSpinBox* const sweepDegrees = component("sweep_degrees", 360.0);
            sweepDegrees->setRange(-3600.0, 3600.0);
            form->addRow("Layout:", kind);
            form->addRow("Count (linear/radial):", countBox);
            form->addRow("Step X/Y/Z:", stepX);
            form->addRow("", stepY);
            form->addRow("", stepZ);
            form->addRow("Grid columns/rows:", countU);
            form->addRow("", countV);
            form->addRow("Grid step U X/Y/Z:", stepUX);
            form->addRow("", stepUY);
            form->addRow("", stepUZ);
            form->addRow("Grid step V X/Y/Z:", stepVX);
            form->addRow("", stepVY);
            form->addRow("", stepVZ);
            form->addRow("Radial radius:", radius);
            form->addRow("Radial start (deg):", startDegrees);
            form->addRow("Radial sweep (deg):", sweepDegrees);
            auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if ( dialog.exec() != QDialog::Accepted )
            {
                return;
            }
            level::PlacementLayout layout{};
            layout.kind = kind->currentIndex() == 1 ? level::PlacementLayoutKind::grid
                : kind->currentIndex() == 2 ? level::PlacementLayoutKind::radial
                : level::PlacementLayoutKind::linear;
            const QVector3D origin = glView_ ? glView_->gridTransformOrigin() : QVector3D{};
            layout.origin = TVector::Create(origin.x(), origin.y(), origin.z());
            layout.step = TVector::Create(stepX->value(), stepY->value(), stepZ->value());
            layout.stepU = TVector::Create(stepUX->value(), stepUY->value(), stepUZ->value());
            layout.stepV = TVector::Create(stepVX->value(), stepVY->value(), stepVZ->value());
            layout.count = countBox->value();
            layout.countU = countU->value();
            layout.countV = countV->value();
            layout.radius = radius->value();
            layout.startDegrees = startDegrees->value();
            layout.sweepDegrees = sweepDegrees->value();
            if ( const auto positions = level::computePlacementPositions(layout); !positions.ok() )
            {
                QMessageBox::information(this, title, QString::fromStdString(positions.failure->message));
                return;
            }
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Built sector array",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    std::optional<std::vector<std::int32_t>> result =
                        level::levelBuildSectorArray(state.level, sectors, layout);
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
                QMessageBox::information(this, title, "The sector array could not be built with these parameters.");
                return;
            }
            invalidateSurfaceSelectionContextForTopologyEdit();
            glView_->notifyLevelGeometryChanged();
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Built %1 arrayed sector(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(sectorArrayAction);

    QAction* const savePrefabAction = new QAction("Save Sectors as Prefab...", this);
    savePrefabAction->setObjectName("sector.save_prefab");
    savePrefabAction->setToolTip("Save the selected sectors as a versioned, hash-bound prefab file");
    connect(savePrefabAction, &QAction::triggered, this, [this, selectedSectors]()
        {
            const QString title = QStringLiteral("Save Sectors as Prefab");
            const std::vector<std::int32_t> sectors = selectedSectors();
            if ( sectors.empty() )
            {
                QMessageBox::information(this, title, "Select one or more sectors first.");
                return;
            }
            const std::optional<level::Prefab> prefab =
                level::makePrefab(document_.level(), sectors, sectors.front());
            if ( !prefab )
            {
                QMessageBox::information(this, title, "The selected sectors could not be captured as a prefab.");
                return;
            }
            const std::optional<std::string> text = level::savePrefabText(*prefab);
            if ( !text )
            {
                QMessageBox::information(this, title, "The prefab could not be serialized.");
                return;
            }
            const QString filename = QFileDialog::getSaveFileName(
                this, title,
                fileDialogStartLocation(FileDialogHistoryGroup::export_),
                "RED prefab (*.prefab);;All files (*)");
            if ( filename.isEmpty() )
            {
                return;
            }
            rememberFileDialogSelection(FileDialogHistoryGroup::export_, filename);
            QSaveFile output(filename);
            if ( !output.open(QIODevice::WriteOnly | QIODevice::Text)
                || output.write(text->data(), static_cast<qint64>(text->size()))
                    != static_cast<qint64>(text->size())
                || !output.commit() )
            {
                QMessageBox::information(this, title, "The prefab file could not be written.");
                return;
            }
            const QString message = QStringLiteral("Saved prefab with %1 sector(s), %2 boundary face(s)")
                .arg(prefab->payload.sectors.size()).arg(prefab->attachments.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(savePrefabAction);

    QAction* const insertPrefabAction = new QAction("Insert Prefab...", this);
    insertPrefabAction->setObjectName("sector.insert_prefab");
    insertPrefabAction->setToolTip("Insert a prefab file at its authored coordinates as one undo step");
    connect(insertPrefabAction, &QAction::triggered, this, [this]()
        {
            const QString title = QStringLiteral("Insert Prefab");
            const QString filename = QFileDialog::getOpenFileName(
                this, title,
                fileDialogStartLocation(FileDialogHistoryGroup::import_),
                "RED prefab (*.prefab);;All files (*)");
            if ( filename.isEmpty() )
            {
                return;
            }
            rememberFileDialogSelection(FileDialogHistoryGroup::import_, filename);
            QFile input(filename);
            if ( !input.open(QIODevice::ReadOnly | QIODevice::Text) )
            {
                QMessageBox::information(this, title, "The prefab file could not be opened.");
                return;
            }
            const std::optional<level::Prefab> prefab =
                level::loadPrefabFromText(QString::fromUtf8(input.readAll()).toStdString());
            if ( !prefab )
            {
                QMessageBox::information(this, title,
                    "The file is not a valid RED prefab (bad format, version, project, or content hash).");
                return;
            }
            std::vector<std::int32_t> created{};
            const bool applied = document_.applyUndoable("Sectors: Inserted prefab",
                editor::DocumentDirty::LevelGeometry | editor::DocumentDirty::Structure,
                [&](editor::DocumentState& state) -> bool
                {
                    const std::optional<level::EntityPasteResult> result =
                        level::insertPrefab(state.level, *prefab);
                    if ( !result )
                    {
                        return false;
                    }
                    created = result->indices;
                    state.selection.clear();
                    state.selection.sectors.insert(created.begin(), created.end());
                    return true;
                });
            if ( !applied )
            {
                QMessageBox::information(this, title,
                    "The prefab could not be inserted (its project differs from this level).");
                return;
            }
            invalidateSurfaceSelectionContextForTopologyEdit();
            glView_->notifyLevelGeometryChanged();
            glView_->setHighlightedSelection(document_.selection());
            rebuildOutliner();
            rebuildProperties();
            updateEditActions();
            const QString message = QStringLiteral("Inserted %1 prefab sector(s)").arg(created.size());
            logMessage(MessageSeverity::info, message);
            glView_->showToast(message, 2400, GlView::ToastPlacement::BottomCenter);
        });
    sectorMenu->addAction(insertPrefabAction);
}

} // namespace sed
