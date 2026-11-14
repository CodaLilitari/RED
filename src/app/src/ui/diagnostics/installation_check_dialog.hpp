#pragma once

#include <QDialog>
#include <QString>

#include <functional>
#include <memory>

namespace sed::ui::diagnostics
{

class InstallationCheckDialog final : public QDialog
{
public:
    struct Context
    {
        QString executable{};
        QString reportParentDirectory{};
        std::function<QString(QWidget*)> selectJsonReportDestination{};
    };
    explicit InstallationCheckDialog(Context context, QWidget* parent = nullptr);
    ~InstallationCheckDialog() override;

protected:
    void done(int result) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::diagnostics
