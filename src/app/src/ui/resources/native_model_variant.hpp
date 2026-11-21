#pragma once

#include "sed/assets/native_model_document.hpp"
#include "sed/assets/resource_locator.hpp"

#include <QByteArray>
#include <QString>

#include <filesystem>
#include <memory>

namespace sed::assets::detail
{
class StagedAssetFile;
}

namespace sed::ui::resources
{

struct NativeModelSource
{
    QString name;
    bool resource{};
    std::shared_ptr<const assets::ResourceLocator> resources;
};

struct NativeModelSourceSnapshot
{
    NativeModelSource source;
    QByteArray bytes;
    QString sha256;
    assets::ResourceReadOrigin origin;
    // Canonical loose file or disk-backed archive. Captured payloads have no physical origin.
    std::filesystem::path diskOrigin;
    // Presentation only. Resource identity comparisons use the exact typed origin above.
    QString originIdentity;
};

NativeModelSourceSnapshot readNativeModelSource(NativeModelSource source);
void revalidateNativeModelSource(const NativeModelSourceSnapshot& original);

struct NativeModelVariantReview
{
    NativeModelSourceSnapshot original;
    assets::NativeModelDocument candidate;
    std::filesystem::path destination;
    QString destinationText;
    QString sha256;
    QString summary;
};

// The caller supplies the cumulative committed document from the session opened on original.
std::shared_ptr<const NativeModelVariantReview> reviewNativeModelVariant(
    const NativeModelSourceSnapshot& original, assets::NativeModelDocument candidate, const QString& destination);

class NativeModelVariantStage
{
public:
    ~NativeModelVariantStage();
    bool published() const noexcept;

private:
    friend std::shared_ptr<NativeModelVariantStage> stageNativeModelVariant(
        std::shared_ptr<const NativeModelVariantReview> review);
    friend void publishNativeModelVariant(NativeModelVariantStage& stage);
    explicit NativeModelVariantStage(std::shared_ptr<const NativeModelVariantReview> review);

    std::shared_ptr<const NativeModelVariantReview> review_;
    std::unique_ptr<assets::detail::StagedAssetFile> file_;
    bool published_{};
};

std::shared_ptr<NativeModelVariantStage> stageNativeModelVariant(std::shared_ptr<const NativeModelVariantReview> review);
// The GUI owner holds the accepted revision stable and checks the live resource context immediately before calling.
void publishNativeModelVariant(NativeModelVariantStage& stage);

} // namespace sed::ui::resources
