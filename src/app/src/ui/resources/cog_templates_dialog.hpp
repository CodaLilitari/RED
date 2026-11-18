#pragma once

#include <QDialog>

#include <filesystem>
#include <optional>

class QListWidget;
class QPlainTextEdit;

namespace sed::ui::resources
{

/**
 * COG Templates: RED's library of vehicle and enemy controllers. Shows each
 * set's notes (scripts, bindings, template rows, files to supply, credits) and
 * copies a set into the level's project without overwriting anything.
 */
class CogTemplatesDialog final : public QDialog
{
public:
    CogTemplatesDialog(std::optional<std::filesystem::path> library, std::filesystem::path projectDirectory, QWidget* parent = nullptr);

    void selectSet(int row);
    /** Copies the selected set into the project; returns what happened. */
    QString copySelected();
    [[nodiscard]] QString notesText() const;

private:
    std::optional<std::filesystem::path> library_;
    std::filesystem::path project_;
    QListWidget* list_{};
    QPlainTextEdit* notes_{};
};

} // namespace sed::ui::resources
