#pragma once

#include <QDialog>
#include <QString>

#include <memory>

namespace sed::ui::project
{
/** Independent capture request/runner UI; never exports or changes a Level. */
class EngineCaptureDialog final : public QDialog
{
public:
    struct Context
    {
        QString initialDirectory{};
        QString pythonExecutable{};
        QString runnerScript{};
    };

    explicit EngineCaptureDialog(Context context, QWidget* parent = nullptr);
    ~EngineCaptureDialog() override;

protected:
    void done(int result) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sed::ui::project
