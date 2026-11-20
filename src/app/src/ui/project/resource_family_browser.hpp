#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class QWidget;
namespace sed::project
{
struct ResourceFamilyIndex;
}
namespace sed::ui
{
using FamilyIndex = std::shared_ptr<const ::sed::project::ResourceFamilyIndex>;
struct FamilyActions
{
    std::function<void(QWidget *, FamilyIndex, const std::string &)> preview;
    std::function<void(QWidget *, FamilyIndex, const std::string &, const std::filesystem::path &,
                       const std::filesystem::path &)>
        insert;
};
QWidget *createResourceFamilyBrowser(
    QWidget *parent, std::function<std::pair<std::filesystem::path, std::filesystem::path>()> selectedPaths,
    FamilyActions actions);
} // namespace sed::ui
