#pragma once

#include "sed/assets/3do.hpp"

class QDialog;
class QWidget;

namespace sed::ui::resources
{
QDialog *createModelPosePreview(QWidget *parent, const std::filesystem::path &modelPath, const assets::Model3do &hierarchy,
                                const std::filesystem::path &keyPath);
}
