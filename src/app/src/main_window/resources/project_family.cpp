#include "main_window/common.hpp"
#include "ui/project/project_cockpit.hpp"
#include "ui/episode/episode_editor.hpp"
#include "resource_families.hpp"
#include "ui/resources/resource_picker.hpp"
#include "ui/episode/gameplay_publication.hpp"
#include "sed/level/geometry.hpp"
#include "sed/text.hpp"
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

namespace sed
{
void MainWindow::setupProjectCockpit(QMenu *toolsMenu)
{
    ui::FamilyActions familyActions;
    familyActions.preview = [](QWidget *parent, ui::FamilyIndex index, const std::string &key)
    {
        using Kind = ui::ResourcePickerKind;
        static const std::map<std::string, Kind> kinds{
            {"Model", Kind::model3do},  {"Material", Kind::material},  {"Animation", Kind::key},
            {"Puppet", Kind::pup},      {"Script", Kind::cog},         {"Sound", Kind::wav},
            {"Sound class", Kind::snd}, {"Template", Kind::template_}, {"AI", Kind::ai},
            {"Sprite", Kind::spr},      {"Palette", Kind::cmp},        {"Particles", Kind::par}};
        const auto &row = index->rows.at(key);
        const auto kind = kinds.find(row.kind);
        if ( kind == kinds.end() )
        {
            throw std::runtime_error("Select a typed resource or a template in this library.");
        }
        ui::ResourcePickerOptions options{};
        options.kind = kind->second;
        options.previewProjectType = index->sources.target.game == "jk" ? ProjectType::JKDF2 : ProjectType::MOTS;
        options.initialName = row.name;
        options.title = "Project preview: " + row.name;
        std::vector<ThingTemplate> overrides;
        for ( const auto &dependency : project::resourceFamily(*index, key) )
        {
            if ( const auto &tpl = index->rows.at(dependency).authoringTemplate )
            {
                overrides.push_back(*tpl);
            }
        }
        options.templateOverrides = &overrides;
        auto resources = std::shared_ptr<const assets::ResourceLocator>(index, &index->previewResources);
        (void)ui::pickResource(parent, std::move(resources), options);
    };
    familyActions.insert = [this](QWidget *parent, ui::FamilyIndex index, const std::string &key,
                                  const std::filesystem::path &profile, const std::filesystem::path &stock)
    {
        namespace fs = std::filesystem;
        const auto projectDir = profile.parent_path();
        const auto lifetime = documentLifetimeGeneration_;
        const auto serial = recoverySemanticMutationSerial_;
        const auto selection = document_.selection();
        const auto checkTarget = [&]
        {
            if ( !documentEditingAllowed() || transformPreview_.active || currentPath_.empty() ||
                 lifetime != documentLifetimeGeneration_ || serial != recoverySemanticMutationSerial_ ||
                 document_.selection().sectors != selection.sectors || selection.sectors.size() != 1 ||
                 selectionTouchesNonEditableLayer(selection) )
            {
                throw std::runtime_error("Select one editable sector in the open campaign level.");
            }
            if ( !fs::equivalent(ui::episode::projectDirectoryForLevel(currentPath_), projectDir) ||
                 (index->sources.target.game == "jk" ? ProjectType::JKDF2 : ProjectType::MOTS) !=
                     document_.level().kind )
            {
                throw std::runtime_error(
                    "The open level and the selected campaign must match the same project and game.");
            }
            const bool inCampaign = std::ranges::any_of(index->sources.authoringFiles,
                                                        [&](const auto &allowed)
                                                        {
                                                            std::error_code ec;
                                                            return fs::equivalent(currentPath_, allowed, ec);
                                                        });
            if ( !inCampaign )
            {
                throw std::runtime_error("Declare the open level as this campaign’s native document or export.");
            }
            const auto sector = *selection.sectors.begin();
            if ( sector < 0 || static_cast<std::size_t>(sector) >= document_.level().sectors.size() )
            {
                throw std::runtime_error("The selected sector is no longer present.");
            }
            const auto position = level::sectorCalcCenter(document_.level().sectors[sector]);
            if ( !document_.level().canAllocateIds(1) ||
                 !level::sectorContainsPoint(document_.level().sectors[sector], position.x, position.y,
                                             position.z) )
            {
                throw std::runtime_error("The selected sector has no valid center for this placement.");
            }
            return std::pair{sector, position};
        };
        (void)checkTarget();
        bool accepted = false;
        const auto requested =
            QInputDialog::getText(parent, tr("Insert resource family"),
                                  tr("Template name (reuse an identical definition or choose a new name)"),
                                  QLineEdit::Normal, QString::fromStdString(index->rows.at(key).name), &accepted);
        if ( !accepted )
        {
            return;
        }
        (void)checkTarget();
        const auto prepare = [&]
        {
            return project::prepareFamily(*index, key, document_.level(), resources_, requested.toStdString(),
                                          &templates_);
        };
        const auto plan = prepare();
        if ( !plan.provisions.empty() && std::ranges::none_of(resources_.roots(),
                                                              [&](const auto &path)
                                                              {
                                                                  std::error_code ec;
                                                                  return fs::equivalent(path, projectDir, ec);
                                                              }) )
        {
            throw std::runtime_error(
                "Add this campaign directory to the active level’s resource roots before provisioning assets.");
        }
        QStringList summary;
        summary << tr("Place %1 at the center of sector %2.").arg(requested).arg(*selection.sectors.begin());
        summary << tr("Existing resources with identical bytes will be reused.");
        for ( const auto &[path, bytes] : plan.provisions )
        {
            summary << tr("Create %1").arg(QString::fromStdU16String(path.generic_u16string()));
        }
        for ( const auto &tpl : plan.templates )
        {
            summary << tr("Bind template %1").arg(QString::fromStdString(tpl.name));
        }
        summary << tr("Undo removes the placement and new level templates. Project asset files stay available for "
                      "other levels.");
        if ( QMessageBox::question(parent, tr("Insert resource family"), summary.join('\n')) != QMessageBox::Yes )
        {
            return;
        }
        const auto [sector, position] = checkTarget();
        if ( project::captureAssetSources(profile, stock).revision != index->sources.revision )
        {
            throw std::runtime_error(
                "The project mounts or level exports changed. Inspect the resource family again.");
        }
        const auto checked = prepare();
        if ( checked.provisions != plan.provisions )
        {
            throw std::runtime_error("The active level resource winners changed. Review the new insertion.");
        }
        std::vector<ui::episode::GameplayFileChange> files;
        for ( const auto &[path, bytes] : checked.provisions )
        {
            files.push_back({path, {}, bytes});
        }
        const auto dirty = editor::DocumentDirty::Things | editor::DocumentDirty::Structure;
        const auto applied = document_.applyUndoable(
            "Thing: Inserted resource family", dirty,
            [&](editor::DocumentState &state)
            {
                if ( !project::bindFamily(state.level, checked, sector, position) )
                {
                    return false;
                }
                state.selection.clear();
                state.selection.things.insert(static_cast<std::int32_t>(state.level.things.size() - 1));
                if ( !files.empty() )
                {
                    ui::episode::publishGameplayFiles(projectDir, recoveryDirectory(), files);
                }
                if ( !project::prepareFamily(*index, key, state.level, resources_, checked.name)
                          .provisions.empty() )
                {
                    throw std::runtime_error("The provisioned files do not resolve as the reviewed resources.");
                }
                return true;
            });
        if ( !applied )
        {
            throw std::runtime_error("Could not bind the reviewed family to the active sector.");
        }
        publishResourceLocatorSnapshot();
        loadGlobalTemplates();
        if ( glView_ )
        {
            glView_->setHighlightedSelection(document_.selection());
        }
        scheduleRebuildChecks(dirty);
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
    };
    ui::addProjectCockpit(
        toolsMenu, this, [this] { return ui::episode::projectDirectoryForLevel(currentPath_); },
        std::move(familyActions));
}
} // namespace sed
