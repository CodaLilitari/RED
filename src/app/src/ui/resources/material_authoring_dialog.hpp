#pragma once

#include <functional>
#include <memory>

#include <QByteArray>
#include <QDialog>
#include <QString>

namespace sed::ui::resources
{

/** Independent, create-only indexed MAT authoring; never edits the Level. */
class MaterialAuthoringDialog final : public QDialog
{
public:
    struct Context
    {
        QString initialDirectory{};
        QString paletteName{};
        QByteArray paletteBytes{};
        // Called only on the GUI thread, before work and before publication.
        std::function<bool()> isCurrent{};
        // Notification only: the dialog never adds roots or assigns a material.
        std::function<void(const QString&)> published{};
    };

    explicit MaterialAuthoringDialog(Context context, QWidget* parent = nullptr);
    ~MaterialAuthoringDialog() override;

protected:
    void done(int result) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::resources
