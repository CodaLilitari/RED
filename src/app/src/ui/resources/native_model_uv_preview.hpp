#pragma once

#include "sed/editor/native_model_session.hpp"

#include <QImage>
#include <QWidget>

#include <optional>
#include <vector>

namespace sed::ui::resources
{
class NativeModelUvPreview final : public QWidget
{
public:
    struct CornerAddress
    {
        std::size_t face{}, corner{};
        bool operator==(const CornerAddress&) const = default;
    };

    explicit NativeModelUvPreview(QWidget* parent = nullptr);
    void setModel(assets::NativeModelDocument document, editor::NativeModelSelection selection,
                  int materialSlot, bool fit = false);
    void setMaterial(QImage image, QSize sourceSize);
    void clear();
    void reframe();
    const std::vector<CornerAddress>& selectedCorners() const noexcept { return corners_; }
    std::size_t totalSelectedUses() const noexcept { return totalUses_; }
    bool overlayAvailable() const noexcept { return available_; }
    QString overlayError() const { return error_; }
    QPointF uvToWidget(QPointF uv) const;
    QRectF frameBounds() const noexcept { return frame_; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const assets::Model3doMesh* mesh() const;
    QRectF plotRect() const;
    double plotScale() const;
    std::optional<assets::NativeModelDocument> document_;
    editor::NativeModelSelection selection_;
    int slot_{-1};
    QImage image_;
    QSize sourceSize_;
    std::optional<QPointF> marker_;
    std::optional<QRectF> overlayBounds_;
    std::vector<CornerAddress> corners_;
    std::size_t totalUses_{};
    bool available_{};
    QString error_;
    QRectF frame_;
};
}
