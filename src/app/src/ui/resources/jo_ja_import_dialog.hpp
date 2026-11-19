#pragma once

#include <QDialog>
#include <functional>
#include <memory>

namespace sed::ui::resources
{

class JoJaImportDialog final : public QDialog
{
  public:
    struct Context
    {
        QString initialDirectory;
        std::function<bool()> isCurrent;
        std::function<void(const QString&)> published;
    };

    explicit JoJaImportDialog(Context context, QWidget* parent = nullptr);
    ~JoJaImportDialog() override;

  protected:
    void done(int result) override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::resources
