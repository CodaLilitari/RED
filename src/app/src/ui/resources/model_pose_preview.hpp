#pragma once

#include "sed/assets/3do.hpp"
#include "sed/assets/key.hpp"
#include "sed/assets/native_model_document.hpp"

class QDialog;
class QWidget;

namespace sed::ui::resources
{
QDialog *createModelPosePreview(QWidget *parent, const std::filesystem::path &modelPath, const assets::Model3do &hierarchy,
                                const std::filesystem::path &keyPath);

// Owns the unsaved raw all-LOD document and the captured KEY. keyIdentity is
// optional display/PUP-matching metadata; this overload never opens that path.
QDialog *createModelPosePreview(QWidget *parent, assets::NativeModelDocument snapshot, assets::KeyFile key,
                                const std::filesystem::path &keyIdentity = {});
}
