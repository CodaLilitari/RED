#include "sed/assets/native_model_document.hpp"

#include "native_model_source.hpp"
#include "sed/assets/model_lighting.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <tuple>

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
    std::size_t begin, size;
    double original, value;
};

template <class Number>
Patch replacement(const detail::NativeToken<Number>& token, const double value)
{
    return { token.begin, token.size, token.value, value };
}

// Both editors supply disjoint replacements in ascending source order. UV floats
// have always been promoted to double before formatting; keep that wire spelling.
std::string patchSource(const std::string_view source, const std::span<const Patch> patches, const char* formatError)
{
    const auto byteLimit = ModelLightingLimits{}.sourceBytes;
    std::string output;
    output.reserve(source.size());
    auto append = [&](const std::string_view part)
    {
        require(part.size() <= byteLimit - output.size(), "edited source exceeds byte limit.");
        output.append(part);
    };
    std::size_t copied = 0;
    for ( const auto& patch : patches )
    {
        append(source.substr(copied, patch.begin - copied));
        char buffer[64];
        const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), patch.value);
        require(formatted.ec == std::errc{}, formatError);
        append({ buffer, static_cast<std::size_t>(formatted.ptr - buffer) });
        copied = patch.begin + patch.size;
    }
    append(source.substr(copied));
    return output;
}

TVector transformedPosition(const Model3doVertex& vertex, const NativeMeshTransform& transform)
{
    return { std::fma(transform.scale, vertex.x, transform.translation.x),
        std::fma(transform.scale, vertex.y, transform.translation.y),
        std::fma(transform.scale, vertex.z, transform.translation.z) };
}

// TVector equality is approximate. Sorting and source/readback identity need exact doubles.
template <class Position>
auto coordinates(const Position& point)
{
    return std::tie(point.x, point.y, point.z);
}

void chargeWork(std::size_t& remaining, const std::size_t count, const std::size_t visits = 1)
{
    require(count == 0 || visits <= remaining / count, "transform validation work budget exceeded.");
    remaining -= count * visits;
}

void validateShape(const Model3doMesh& mesh, const std::span<const TVector> candidates,
    const double scale, std::size_t& remaining)
{
    const auto n = candidates.size();
    // Initialize and scan the sorted index, plus N * ceil(log2(max(N, 2))) for sorting.
    chargeWork(remaining, n, 2);
    chargeWork(remaining, n, std::bit_width(std::max(n, std::size_t{ 2 }) - 1));
    for ( const auto& face : mesh.faces )
    {
        const auto corners = face.vertices.size();
        // Each strict polygon visits its corners to anchor, normalize, check the
        // plane and edges, and validate its fans. One final pass compares vectors.
        chargeWork(remaining, corners, 11);
        chargeWork(remaining, corners, corners);
        chargeWork(remaining, corners, corners);
    }

    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{});
    std::sort(order.begin(), order.end(), [&](const auto a, const auto b)
    {
        return coordinates(candidates[a]) < coordinates(candidates[b]);
    });
    for ( std::size_t i = 1; i < n; ++i )
    {
        const auto a = order[i - 1];
        const auto b = order[i];
        require(coordinates(candidates[a]) != coordinates(candidates[b]) ||
                coordinates(mesh.vertices[a]) == coordinates(mesh.vertices[b]),
            "distinct source positions became coincident.");
    }
    for ( const auto& face : mesh.faces )
    {
        detail::validateNativeTransformedFace(mesh, face, candidates, scale);
    }
}
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
                && edit.textureVertex < slots[edit.geoset][edit.sourceMesh].textureVertices.size(),
            "texture vertex address outside the document.");
        require(std::isfinite(edit.u) && std::isfinite(edit.v), "nonfinite texture coordinate.");
        const auto& pair = slots[edit.geoset][edit.sourceMesh].textureVertices[edit.textureVertex];
        patches.push_back(replacement(pair[0], edit.u));
        patches.push_back(replacement(pair[1], edit.v));
    }
    std::sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b)
    {
        return a.begin < b.begin;
    });
    for ( std::size_t i = 1; i < patches.size(); ++i )
    {
        require(patches[i - 1].begin != patches[i].begin, "duplicate texture vertex address.");
    }
    std::erase_if(patches, [](const Patch& patch) { return patch.original == patch.value; });
    if ( patches.empty() )
    {
        return *this;
    }

    auto result = open(patchSource(source(), patches, "could not format texture coordinate."));
    for ( const auto& edit : edits )
    {
        const auto& readback = result.geosets()[edit.geoset].meshes[edit.sourceMesh].texVertices[edit.textureVertex];
        require(readback.u == edit.u && readback.v == edit.v, "edited coordinate failed readback.");
    }
    return result;
}

NativeModelDocument NativeModelDocument::applyMeshTransforms(const std::span<const NativeMeshTransform> transforms) const
{
    const ModelLightingLimits limits;
    require(transforms.size() <= limits.meshes, "transform count exceeds mesh limit.");
    std::vector<NativeMeshTransform> ordered(transforms.begin(), transforms.end());
    const auto address = [](const auto& transform) { return std::tie(transform.geoset, transform.sourceMesh); };
    std::sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) { return address(a) < address(b); });
    for ( std::size_t i = 0; i < ordered.size(); ++i )
    {
        const auto& transform = ordered[i];
        require(transform.geoset < geosets().size() && transform.sourceMesh < geosets()[transform.geoset].meshes.size(),
            "mesh address outside the document.");
        require(i == 0 || address(ordered[i - 1]) != address(transform), "duplicate mesh address.");
        const auto& shift = transform.translation;
        require(std::isfinite(transform.scale) && transform.scale > 0 && std::isfinite(shift.x)
                && std::isfinite(shift.y) && std::isfinite(shift.z), "invalid mesh scale or translation.");
    }

    std::size_t remaining = limits.workUnits;
    std::vector<Patch> patches;
    for ( const auto& transform : ordered )
    {
        const auto& mesh = geosets()[transform.geoset].meshes[transform.sourceMesh];
        const auto& slots = data_->parsed.slots[transform.geoset][transform.sourceMesh].positions;
        // Compute every candidate, and allow for one exact readback visit after reopening.
        chargeWork(remaining, mesh.vertices.size(), 2);
        std::vector<TVector> candidates;
        candidates.reserve(mesh.vertices.size());
        const auto initialPatches = patches.size();
        for ( std::size_t v = 0; v < mesh.vertices.size(); ++v )
        {
            const auto candidate = transformedPosition(mesh.vertices[v], transform);
            candidates.push_back(candidate);
            const std::array values{ candidate.x, candidate.y, candidate.z };
            for ( std::size_t axis = 0; axis < values.size(); ++axis )
            {
                require(std::isfinite(values[axis]), "nonfinite transformed coordinate.");
                if ( slots[v][axis].value != values[axis] )
                {
                    patches.push_back(replacement(slots[v][axis], values[axis]));
                }
            }
        }
        if ( patches.size() != initialPatches )
        {
            validateShape(mesh, candidates, transform.scale, remaining);
        }
    }
    if ( patches.empty() )
    {
        return *this;
    }

    auto result = open(patchSource(source(), patches, "could not format position coordinate."));
    for ( const auto& transform : ordered )
    {
        const auto& original = geosets()[transform.geoset].meshes[transform.sourceMesh].vertices;
        const auto& readback = result.geosets()[transform.geoset].meshes[transform.sourceMesh].vertices;
        require(original.size() == readback.size(), "transformed position count failed readback.");
        for ( std::size_t v = 0; v < original.size(); ++v )
        {
            const auto expected = transformedPosition(original[v], transform);
            require(coordinates(readback[v]) == coordinates(expected), "transformed position failed readback.");
        }
    }
    return result;
}

} // namespace sed::assets
