#pragma once

#include <QString>
#include <QDialog>

#include "sed/level.hpp"

class QComboBox;
class QPlainTextEdit;

namespace sed::ui::cog
{

/**
 * Read-only lexical-preflight view for one captured script. Findings come
 * from the core measured-registry preflight (`runCogPreflight`) and always
 * carry the not-a-compiler qualifier; the dialog never edits the script.
 * Runtime-unverified in the sandbox (GUI suites do not run here).
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
    QPlainTextEdit* findingsView_{};
};

} // namespace sed::ui::cog
