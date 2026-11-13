#pragma once
#include <QDialog>
#include <QString>
#include <functional>
#include <memory>

namespace sed::ui::resources
{
/** Explicit captured-file authoring; never edits the Level or launches a game. */
class PackageAuthoringDialog final : public QDialog
{
public:
    struct Context
    {
        QString initialDirectory;
        std::function<bool()> isCurrent;
        std::function<void(const QString&)> published;
    };
    explicit PackageAuthoringDialog(Context context, QWidget* parent = nullptr);
    ~PackageAuthoringDialog() override;
protected:
    void done(int result) override;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sed::ui::resources
