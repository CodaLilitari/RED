#pragma once

#include <QFileDialog>

namespace sed::app_tests
{
// A visible QFileDialog deliberately ignores selectFile() while its filename
// editor owns focus. Native window managers normally give that editor focus.
inline void selectTestDialogFile(QFileDialog& dialog, const QString& path)
{
    if ( QWidget* const focus = dialog.focusWidget() )
    {
        focus->clearFocus();
    }
    dialog.selectFile(path);
}
} // namespace sed::app_tests
