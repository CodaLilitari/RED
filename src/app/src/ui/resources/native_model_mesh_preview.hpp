#pragma once

#include "sed/assets/native_model_document.hpp"

#include <QPointF>
#include <QWidget>

#include <cstddef>
#include <optional>

namespace sed::ui::resources
{

class NativeModelMeshPreview final : public QWidget
{
public:
    explicit NativeModelMeshPreview(QWidget* parent = nullptr);

    // Ordinals address raw source geometry. Keep the same framing while a
    // proposal changes that mesh; pass fit=true when replacing its source.
    void setMesh(assets::NativeModelDocument snapshot, std::size_t geoset, std::size_t sourceMesh, bool fit = false,
                 bool showPlacement = false);
    void clear();
    void reframe();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<assets::NativeModelDocument> snapshot_;
    std::size_t geoset_{};
    std::size_t sourceMesh_{};
    QPointF center_{};
    bool placement_{};
    QPointF placementShift_{};
    QPointF halfExtent_{};
};

} // namespace sed::ui::resources
