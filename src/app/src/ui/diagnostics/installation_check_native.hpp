#pragma once

#include <QString>

namespace sed::ui::diagnostics
{
int runInstallationCheckController(int& argc, char** argv, const QString& reportDirectory);
int runInstallationCheckChild(int& argc, char** argv, const QString& reportDirectory);
}
