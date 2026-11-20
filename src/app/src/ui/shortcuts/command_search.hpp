#pragma once
#include <QString>
#include <functional>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

namespace sed::ui
{
using CommandDisabledReason = std::function<QString(const QAction*)>;
void addCommandSearch(QMenuBar* bar, QMenu* edit, QWidget* owner, CommandDisabledReason reason);
} // namespace sed::ui
