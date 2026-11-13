#pragma once

#include <functional>
#include <memory>

#include <QDialog>
#include <QString>

namespace sed::ui::resources
{

/** Read-only inspection of explicit exported JKL and captured ordered archives. */
class DependencyInspectorDialog final : public QDialog
{
public:
    struct Context
    {
        QString initialDirectory;
        // Evaluated on the GUI thread before publishing a report or file.
        std::function<bool()> isCurrent;
    };

    explicit DependencyInspectorDialog(Context context, QWidget* parent = nullptr);
    ~DependencyInspectorDialog() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::resources
