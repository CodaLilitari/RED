#pragma once
#include "ui/project/resource_family_browser.hpp"
#include <filesystem>
#include <functional>
#include <QByteArray>

class QMenu;
class QWidget;

namespace sed::ui
{
struct PlaytestEditor
{
    std::function<std::filesystem::path()> source;
    std::function<QByteArray()> revision;
    std::function<bool(const std::filesystem::path&)> exportMap;
    std::function<QString()> lightingSummary;
};
void addProjectCockpit(QMenu* menu, QWidget* parent, std::function<std::filesystem::path()> campaignDirectory,
    PlaytestEditor editor = {}, FamilyActions familyActions = {});
}
