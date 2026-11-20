#pragma once
#include <filesystem>
#include <functional>

class QMenu;
class QWidget;

namespace sed::ui
{
void addProjectCockpit(QMenu* menu, QWidget* parent, std::function<std::filesystem::path()> campaignDirectory);
}
