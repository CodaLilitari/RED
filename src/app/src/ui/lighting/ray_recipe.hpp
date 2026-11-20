#pragma once

#include "sed/level/ray_lighting_resources.hpp"

#include <QByteArray>
#include <QString>
#include <vector>

namespace sed::ui::lighting
{
struct ResourceDigest
{
    std::string name;
    std::string extension;
    QByteArray sha256;
    bool operator==(const ResourceDigest&) const = default;
};

struct RayRecipe
{
    level::RayLightingOptions options;
    std::vector<std::int32_t> targets;
    std::vector<ResourceDigest> resources;
    std::uint64_t geometry{};
    std::uint64_t materials{};
    QByteArray corners;
    QString backend;
    QString build;
    bool preferGpu{};
    level::RayLightingContributions contributions{};
};

QByteArray encodeRayRecipe(const RayRecipe& recipe);
std::optional<RayRecipe> decodeRayRecipe(const QByteArray& data);
QByteArray cornerDigest(const Level& level, std::span<const std::int32_t> targets);
std::vector<ResourceDigest> resourceDigests(const level::RayLightingResources& resources);
struct RayRecipeState { bool current{}; QString message; };
std::uint64_t recipeResourceInputs(const Level& level, const assets::Templates* templates);
RayRecipeState checkRayRecipe(const RayRecipe& recipe, const Level& level, const assets::Templates* templates,
    const level::EmissiveResourceReader& reader);
} // namespace sed::ui::lighting
