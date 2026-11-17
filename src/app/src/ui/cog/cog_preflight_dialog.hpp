#pragma once

#include <QString>
#include <QDialog>

#include "sed/level.hpp"

class QCheckBox;
class QComboBox;
class QPlainTextEdit;

namespace sed::ui::cog
{

/**
 * Read-only check of one captured script: the COG compiler's verdict and
 * findings (`compileCog`) for the chosen game, then the lexical preflight
 * inventory (`runCogPreflight`). Optionally lists the compiled code. The
 * dialog never edits or runs the script.
 */
class CogPreflightDialog final : public QDialog
{
public:
    CogPreflightDialog(
        const QString& scriptName,
        const QString& scriptText,
        ProjectType project,
        QWidget* parent = nullptr);

private:
    void refreshFindings();

    QString scriptName_{};
    QString scriptText_{};
    QComboBox* targetCombo_{};
    QCheckBox* listingCheck_{};
    QPlainTextEdit* findingsView_{};
};

} // namespace sed::ui::cog
