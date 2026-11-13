#pragma once

#include "viewport/saved_camera.hpp"
#include <QDialog>
#include <functional>
#include <memory>

namespace sed::ui::viewport
{
class SavedCamerasDialog final : public QDialog
{
public:
    struct Context
    {
        std::vector<app::SavedCamera> cameras;
        std::function<app::CameraPose()> capture;
        std::function<bool(const app::CameraPose&, QString*)> apply;
        std::function<bool(const std::vector<app::SavedCamera>&, QString*)> store;
        std::function<bool()> isCurrent;
    };
    explicit SavedCamerasDialog(Context context, QWidget* parent = nullptr);
    ~SavedCamerasDialog() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sed::ui::viewport
