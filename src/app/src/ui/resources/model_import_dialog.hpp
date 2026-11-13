#pragma once

#include <functional>
#include <memory>

#include <QDialog>
#include <QString>

namespace sed::ui::resources
{

/** Independent OBJ/GLB conversion. Publishes new assets without editing the Level. */
class ModelImportDialog final : public QDialog
{
public:
    struct Context
    {
        QString initialDirectory{};
        std::function<bool()> isCurrent{};
        std::function<void(const QString&)> published{};
    };

    explicit ModelImportDialog(Context context, QWidget* parent = nullptr);
    ~ModelImportDialog() override;

protected:
    void done(int result) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::resources
