#pragma once

#include "sed/assets/mat.hpp"
#include "sed/assets/resource_locator.hpp"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

#include <filesystem>
#include <memory>
#include <string>

namespace sed::ui::resources
{

struct ModelUvMaterialRequest
{
    std::shared_ptr<const assets::ResourceLocator> resources;
    std::string materialName;
    std::string paletteName;
    std::filesystem::path materialPath;
    std::filesystem::path palettePath;
};

struct ModelUvMaterialSource
{
    assets::ResourceReadOrigin origin;
    QString sha256;
    QString description;
};

struct ModelUvMaterialCapture
{
    QImage image;
    QSize sourceSize{0, 0};
    assets::MatPreviewKind kind{};
    std::uint8_t solidColorIndex{};
    QColor solidColor;
    ModelUvMaterialSource material;
    ModelUvMaterialSource palette;
    QString colorsUnavailable;
};

// Nonempty native paths explicitly override the corresponding project resource.
// Bad MAT input throws. A missing or invalid CMP leaves the decoded source geometry available.
ModelUvMaterialCapture captureModelUvMaterial(const ModelUvMaterialRequest& request);

}
