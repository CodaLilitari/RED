#pragma once
#include "sed/assets/cog_runtime.hpp"
#include <functional>

class QDialog;
class QWidget;
namespace sed::ui::assistants
{
using TraceHighlight = std::function<void(int, int)>;
QDialog *createCogTraceReview(QWidget *parent, std::vector<assets::CogMessageBreakpoint> events, const std::string &script,
                              const std::vector<std::string> &unsimulated, TraceHighlight highlight);
} // namespace sed::ui::assistants
