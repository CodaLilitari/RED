#pragma once

#include "project_cockpit.hpp"
#include <QString>

class QLabel;

namespace sed::ui
{
void beginProjectPlaytest(QWidget* owner, QLabel* status, const std::filesystem::path& profile,
    const std::filesystem::path& stockRoot, const QString& scenarioName, const QString& machineRequest,
    const PlaytestEditor& editor);
}
