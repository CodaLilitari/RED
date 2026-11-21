#include "sed/assets/native_model_document.hpp"

#include "native_model_source.hpp"
#include "sed/assets/model_lighting.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace sed::assets
{
namespace
{
void require(const bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(std::string("Native model document: ") + message);
    }
}

struct Patch
{
    detail::NativeUvToken token;
    float value;
};
} // namespace

struct NativeModelDocument::Data
{
    std::string text;
    detail::NativeModelSource parsed;
};

NativeModelDocument::NativeModelDocument(std::shared_ptr<const Data> data)
    : data_(std::move(data))
{
}

NativeModelDocument NativeModelDocument::open(const std::string_view source)
{
    require(source.size() <= ModelLightingLimits{}.sourceBytes, "source byte limit exceeded.");
    auto data = std::make_shared<Data>();
    data->text = source;
    data->parsed = detail::readNativeModelSource(data->text);
    return NativeModelDocument(std::move(data));
}

std::string_view NativeModelDocument::source() const noexcept
{
    return data_->text;
}

std::span<const Model3do> NativeModelDocument::geosets() const noexcept
{
    return data_->parsed.models;
}

NativeModelDocument NativeModelDocument::applyTextureVertexEdits(
    const std::span<const NativeTextureVertexEdit> edits) const
{
    const ModelLightingLimits limits;
    require(edits.size() <= limits.lines, "edit count exceeds source row limit.");
    std::vector<Patch> patches;
    patches.reserve(edits.size() * 2);
    for ( const auto& edit : edits )
    {
        const auto& slots = data_->parsed.slots;
        require(edit.geoset < slots.size() && edit.sourceMesh < slots[edit.geoset].size()
                && edit.textureVertex < slots[edit.geoset][edit.sourceMesh].size(),
            "texture vertex address outside the document.");
        require(std::isfinite(edit.u) && std::isfinite(edit.v), "nonfinite texture coordinate.");
        const auto& pair = slots[edit.geoset][edit.sourceMesh][edit.textureVertex];
        patches.push_back({ pair[0], edit.u });
        patches.push_back({ pair[1], edit.v });
    }
    std::sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b)
    {
        return a.token.begin < b.token.begin;
    });
    for ( std::size_t i = 1; i < patches.size(); ++i )
    {
        require(patches[i - 1].token.begin != patches[i].token.begin, "duplicate texture vertex address.");
    }
    std::erase_if(patches, [](const Patch& patch) { return patch.token.value == patch.value; });
    if ( patches.empty() )
    {
        return *this;
    }

    std::string output;
    output.reserve(source().size());
    auto append = [&](const std::string_view part)
    {
        require(part.size() <= limits.sourceBytes - output.size(), "edited source exceeds byte limit.");
        output.append(part);
    };
    std::size_t copied = 0;
    for ( const auto& patch : patches )
    {
        append(source().substr(copied, patch.token.begin - copied));
        char buffer[64];
        const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<double>(patch.value));
        require(formatted.ec == std::errc{}, "could not format texture coordinate.");
        append({ buffer, static_cast<std::size_t>(formatted.ptr - buffer) });
        copied = patch.token.begin + patch.token.size;
    }
    append(source().substr(copied));
    auto result = open(output);
    for ( const auto& edit : edits )
    {
        const auto& readback = result.geosets()[edit.geoset].meshes[edit.sourceMesh].texVertices[edit.textureVertex];
        require(readback.u == edit.u && readback.v == edit.v, "edited coordinate failed readback.");
    }
    return result;
}

} // namespace sed::assets
