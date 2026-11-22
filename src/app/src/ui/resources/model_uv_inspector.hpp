#pragma once

#include "ui/resources/model_uv_material.hpp"
#include "ui/resources/native_model_uv_preview.hpp"

#include <functional>
#include <memory>

namespace sed::ui::resources
{
class ModelUvInspector final : public QWidget
{
public:
    struct Context
    {
        std::shared_ptr<const assets::ResourceLocator> resources;
        std::string masterColormap;
        std::function<bool()> isCurrent; // GUI thread only.
    };
    struct WorkHooksForTests
    {
        std::function<void()> beforeCapture;
    };

    explicit ModelUvInspector(Context context, QWidget* parent = nullptr);
    ~ModelUvInspector() override;
    void setModel(assets::NativeModelDocument document, editor::NativeModelSelection selection,
                  std::uint64_t sourceGeneration, bool fit = false);
    void clear();
    bool busy() const;
    NativeModelUvPreview* canvas() const;
    const ModelUvMaterialCapture* capture() const;
    void setWorkHooksForTests(WorkHooksForTests hooks);

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
