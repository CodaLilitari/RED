#include "sed/assets/native_model_document.hpp"

#include "native_model_source.hpp"
#include "sed/assets/model_lighting.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
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
// The JK and MotS 3DO loaders read each position into a 32-bit float. Out-of-range
// values are given as infinities here rather than through an undefined conversion.
float storedFloat(const double value)
{
    constexpr double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
    if ( value > maxFloat )
    {
        return std::numeric_limits<float>::infinity();
    }
    if ( value < -maxFloat )
    {
        return -std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(value);
}

template <class Position>
std::array<float, 3> storedPosition(const Position& point)
{
    return { storedFloat(point.x), storedFloat(point.y), storedFloat(point.z) };
}

// A changed coordinate must survive the game's float conversion: it may not overflow or
// read as zero. Positions distinct in the game before the edit must stay distinct after it.
void validateStoredPositions(const Model3doMesh& mesh, const std::span<const TVector> candidates,
    std::size_t& remaining)
{
    const auto n = candidates.size();
    chargeWork(remaining, n, 3);
    chargeWork(remaining, n, std::bit_width(std::max(n, std::size_t{ 2 }) - 1));
    for ( std::size_t v = 0; v < n; ++v )
    {
        const std::array changed{ candidates[v].x, candidates[v].y, candidates[v].z };
        const std::array source{ mesh.vertices[v].x, mesh.vertices[v].y, mesh.vertices[v].z };
        for ( std::size_t axis = 0; axis < changed.size(); ++axis )
        {
            if ( changed[axis] == source[axis] )
            {
                continue;
            }
            const float stored = storedFloat(changed[axis]);
            require(std::isfinite(stored), "transformed coordinate is outside the float range the game reads.");
            require(changed[axis] == 0 || stored != 0.0f, "transformed coordinate would read as zero in the game.");
        }
    }
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{});
    std::sort(order.begin(), order.end(), [&](const auto a, const auto b)
    {
        return storedPosition(candidates[a]) < storedPosition(candidates[b]);
    });
    for ( std::size_t i = 1; i < n; ++i )
    {
        const auto a = order[i - 1];
        const auto b = order[i];
        require(storedPosition(candidates[a]) != storedPosition(candidates[b]) ||
                storedPosition(mesh.vertices[a]) == storedPosition(mesh.vertices[b]),
            "distinct positions would become coincident in the game's float positions.");
    }
}

// Where each material-table name sits in the source: the MODELRESOURCE section's MATERIALS header and the
// rows after it, found the way the 3DO reader finds them (comments cut at '#', lines trimmed, the name the
// text after a row's last ':').
std::vector<std::pair<std::size_t, std::size_t>> materialNameSpans(const std::string_view model)
{
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    bool inResource = false;
    std::size_t expected = 0;
    bool header = false;
    std::size_t lineBegin = 0;
    while ( lineBegin < model.size() )
    {
        const std::size_t newline = model.find('\n', lineBegin);
        const std::size_t lineEnd = newline == std::string_view::npos ? model.size() : newline;
        std::string_view line = model.substr(lineBegin, lineEnd - lineBegin);
        if ( const std::size_t comment = line.find('#'); comment != std::string_view::npos )
        {
            line = line.substr(0, comment);
        }
        line = text::trimView(line);
        if ( !line.empty() )
        {
            if ( text::iequalsAscii(line, "END") )
            {
                inResource = false;
            }
            else if ( text::istartsWithAscii(line, "SECTION:") )
            {
                inResource = text::iequalsAscii(text::trimView(line.substr(8)), "MODELRESOURCE");
            }
            else if ( inResource && !header )
            {
                require(text::istartsWithAscii(line, "MATERIALS"), "material table header not found.");
                const auto count = text::trimView(line.substr(9));
                const auto parsed = std::from_chars(count.data(), count.data() + count.size(), expected);
                require(parsed.ec == std::errc{} && parsed.ptr == count.data() + count.size(), "material count unreadable.");
                header = true;
            }
            else if ( inResource && spans.size() < expected )
            {
                std::string_view name = line;
                if ( const std::size_t colon = name.find_last_of(':'); colon != std::string_view::npos )
                {
                    name.remove_prefix(colon + 1);
                }
                name = text::trimView(name);
                require(!name.empty(), "material row without a name.");
                spans.emplace_back(static_cast<std::size_t>(name.data() - model.data()), name.size());
            }
        }
        if ( newline == std::string_view::npos )
        {
            break;
        }
        lineBegin = newline + 1;
    }
    require(header && spans.size() == expected, "material table rows disagree with their count.");
    return spans;
}

void requireMaterialName(const std::string_view name)
{
    require(!name.empty() && name.size() <= 31, "material name must be 1 to 31 bytes (the game's name field).");
    require(name.size() > 4 && text::iequalsAscii(name.substr(name.size() - 4), ".mat"), "material name must end in .mat.");
    for ( const char c : name )
    {
        require(c > ' ' && c < 127 && c != ':' && c != '#', "material name must be printable ASCII without spaces, ':' or '#'.");
    }
}

// The source with the renamed entries' bytes replaced, or nothing when every entry already has its name. Checks the
// ordinals and names against the table; names receives the table as it will read. The caller reads the result back.
std::optional<std::string> patchMaterialNames(const std::string_view source, const std::vector<std::string>& table,
    const std::span<const NativeMaterialRename> renames, std::vector<std::string>& names)
{
    const auto spans = materialNameSpans(source);
    require(spans.size() == table.size(), "material table disagrees with the parsed model.");
    names = table;
    std::vector<const NativeMaterialRename*> ordered;
    for ( const auto& rename : renames )
    {
        require(rename.material < table.size(), "material ordinal outside the table.");
        requireMaterialName(rename.name);
        ordered.push_back(&rename);
        names[rename.material] = rename.name;
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->material < b->material; });
    for ( std::size_t i = 1; i < ordered.size(); ++i )
    {
        require(ordered[i - 1]->material != ordered[i]->material, "duplicate material ordinal.");
    }
    for ( std::size_t i = 0; i < names.size(); ++i )
    {
        for ( std::size_t j = i + 1; j < names.size(); ++j )
        {
            require(!text::iequalsAscii(names[i], names[j]) || text::iequalsAscii(table[i], table[j]),
                "a renamed entry would repeat another entry's name.");
        }
    }
    std::string output;
    output.reserve(source.size() + 64 * ordered.size());
    std::size_t copied = 0;
    bool changed = false;
    for ( const auto* rename : ordered )
    {
        const auto [begin, size] = spans[rename->material];
        changed |= source.substr(begin, size) != rename->name;
        output.append(source.substr(copied, begin - copied));
        output.append(rename->name);
        copied = begin + size;
    }
    if ( !changed )
    {
        return std::nullopt;
    }
    output.append(source.substr(copied));
    require(output.size() <= ModelLightingLimits{}.sourceBytes, "edited source exceeds byte limit.");
    return output;
}

void requireSameMeshes(const Model3do& before, const Model3do& after)
{
    require(before.meshes.size() == after.meshes.size(), "mesh count changed by a material rename.");
    for ( std::size_t m = 0; m < before.meshes.size(); ++m )
    {
        require(before.meshes[m].vertices.size() == after.meshes[m].vertices.size()
                && before.meshes[m].faces.size() == after.meshes[m].faces.size(),
            "mesh rows changed by a material rename.");
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
            validateStoredPositions(mesh, candidates, remaining);
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

NativeModelDocument NativeModelDocument::applyMaterialRenames(const std::span<const NativeMaterialRename> renames) const
{
    require(!geosets().empty(), "model has no geoset.");
    std::vector<std::string> names;
    auto output = patchMaterialNames(source(), geosets().front().materials, renames, names);
    if ( !output )
    {
        return *this;
    }
    auto result = open(*output);
    require(result.geosets().size() == geosets().size(), "geoset count changed by a material rename.");
    for ( std::size_t g = 0; g < result.geosets().size(); ++g )
    {
        require(result.geosets()[g].materials == names, "renamed material table failed readback.");
        requireSameMeshes(geosets()[g], result.geosets()[g]);
    }
    return result;
}

std::vector<Model3do> readModelGeosets(const std::string_view source)
{
    const ModelLightingLimits limits;
    require(source.size() <= limits.sourceBytes, "model source exceeds byte limit.");
    const auto bytes = std::as_bytes(std::span<const char>(source.data(), source.size()));
    Model3doLoadOptions options;
    options.applyHierarchyTransforms = false;
    options.auditSourceRecords = true;
    std::vector<Model3do> lods;
    lods.push_back(loadFrom3DO(bytes, options));
    const std::size_t count = lods.front().geosetCount;
    require(count >= 1 && count <= limits.geosets, "geoset count outside the reader's limits.");
    for ( std::size_t lod = 1; lod < count; ++lod )
    {
        options.lod = static_cast<std::int32_t>(lod);
        lods.push_back(loadFrom3DO(bytes, options));
    }
    return lods;
}

std::string renameModelMaterials(const std::string_view source, const std::span<const NativeMaterialRename> renames)
{
    const auto before = readModelGeosets(source);
    std::vector<std::string> names;
    auto output = patchMaterialNames(source, before.front().materials, renames, names);
    if ( !output )
    {
        return std::string(source);
    }
    const auto after = readModelGeosets(*output);
    require(after.size() == before.size(), "geoset count changed by a material rename.");
    for ( std::size_t g = 0; g < after.size(); ++g )
    {
        require(after[g].materials == names, "renamed material table failed readback.");
        requireSameMeshes(before[g], after[g]);
    }
    return std::move(*output);
}

} // namespace sed::assets
