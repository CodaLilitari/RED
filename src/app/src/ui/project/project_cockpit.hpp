#pragma once
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
};
void addProjectCockpit(QMenu* menu, QWidget* parent, std::function<std::filesystem::path()> campaignDirectory,
    PlaytestEditor editor = {});
}
