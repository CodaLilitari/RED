#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <QDialog>

namespace sed
{
enum class ProjectType : std::int32_t;
namespace assets
{
class ResourceLocator;
}
}

namespace sed::ui::resources
{

/** Independent, source-preserving model asset authoring. Binding a saved
 * variant to a document is a separate explicit document transaction. */
class ModelLightingDialog final : public QDialog
{
public:
    struct Context
    {
        ProjectType project{};
        std::shared_ptr<const assets::ResourceLocator> resources;
        std::string colormap;
        // Called on the GUI thread before publishing results or files.
        std::function<bool()> isCurrent;
    };

    explicit ModelLightingDialog(Context context, QWidget* parent = nullptr);
    ~ModelLightingDialog() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::resources
