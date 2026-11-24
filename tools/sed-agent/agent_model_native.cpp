#include "agent_model_native.hpp"

#include "agent_workspace.hpp"
#include "utf8_path.hpp"
#include "sed/assets/model_collision_compare.hpp"
#include "sed/assets/model_lighting.hpp"
#include "sed/assets/native_model_document.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string_view>

namespace sed_agent
{
namespace
{
using sed::assets::Model3doMesh;
using sed::assets::NativeMaterialRename;
using sed::assets::NativeMeshTransform;
using sed::assets::NativeModelDocument;
using sed::assets::NativeTextureVertexEdit;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "modelNativeInvalidRequest" };
}

std::filesystem::path nativePath(std::string_view text)
{
    if ( !isUtf8PathText(text) )
    {
        invalid("Native file paths must be valid UTF-8 without NUL bytes");
    }
    return pathFromUtf8(text);
}

void fields(const Json& value, std::initializer_list<std::string_view> allowed)
{
    if ( !value.isObject() )
    {
        invalid("Expected an object");
    }
    for ( const auto& [name, unused] : value.object )
    {
        if ( std::find(allowed.begin(), allowed.end(), name) == allowed.end() )
        {
            invalid("Unknown native model field");
        }
    }
}

const Json& required(const Json& object, const char* key)
{
    const auto* value = object.find(key);
    if ( !value )
    {
        invalid(std::string("Missing required field: ") + key);
    }
    return *value;
}

std::string_view stringField(const Json& object, const char* key)
{
    const auto& value = required(object, key);
    if ( value.type != Json::Type::String )
    {
        invalid(std::string(key) + " must be a string");
    }
    return value.str;
}

double number(const Json& value, const char* key, double minimum, double maximum)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number)
        || value.number < minimum || value.number > maximum )
    {
        invalid(std::string(key) + " outside its finite numeric range");
    }
    return value.number;
}

std::size_t ordinal(const Json& object, const char* key, int maximum, int minimum = 0)
{
    const double n = number(required(object, key), key, minimum, maximum);
    if ( n != std::floor(n) )
    {
        invalid(std::string(key) + " must be an integer");
    }
    return static_cast<std::size_t>(n);
}

std::string digest(std::string_view value)
{
    if ( value.size() != 64 || value.find_first_not_of("0123456789abcdefABCDEF") != std::string_view::npos )
    {
        invalid("SHA-256 must contain exactly 64 hexadecimal characters");
    }
    return sed::text::lowerAscii(value);
}

NativeModelDocument openSource(const std::string& source, std::string_view role = {})
{
    try
    {
        return NativeModelDocument::open(source);
    }
    catch ( const std::exception& error )
    {
        throw AgentError{ kExitInput, std::string(role) + error.what(), "modelNativeInputUnsupported" };
    }
}

// The material table is read and renamed through the ordinary 3DO reader, which also admits the RGBA vertex
// rows RED's own writer puts in a 2.1 file; native editing admits only the scalar form.
std::vector<sed::assets::Model3do> readGeosets(const std::string_view source, std::string_view role = {})
{
    try
    {
        return sed::assets::readModelGeosets(source);
    }
    catch ( const std::exception& error )
    {
        throw AgentError{ kExitInput, std::string(role) + error.what(), "modelNativeInputUnsupported" };
    }
}

Json count(std::size_t n)
{
    return Json::makeNumber(static_cast<double>(n));
}

Json xyz(double x, double y, double z)
{
    Json result = Json::makeArray();
    result.array = { Json::makeNumber(x), Json::makeNumber(y), Json::makeNumber(z) };
    return result;
}

Json bounds(const Model3doMesh& mesh)
{
    if ( mesh.vertices.empty() )
    {
        return {};
    }
    auto lo = mesh.vertices.front(), hi = lo;
    for ( const auto& v : mesh.vertices )
    {
        lo.x = std::min(lo.x, v.x);
        lo.y = std::min(lo.y, v.y);
        lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x);
        hi.y = std::max(hi.y, v.y);
        hi.z = std::max(hi.z, v.z);
    }
    auto result = Json::makeObject();
    result.set("min", xyz(lo.x, lo.y, lo.z));
    result.set("max", xyz(hi.x, hi.y, hi.z));
    return result;
}

std::vector<std::size_t> nodeCounts(const sed::assets::Model3do& lod)
{
    std::vector<std::size_t> counts(lod.meshes.size());
    for ( const auto& node : lod.nodes )
    {
        if ( node.mesh >= 0 )
        {
            ++counts.at(static_cast<std::size_t>(node.mesh));
        }
    }
    return counts;
}

std::vector<std::size_t> cornerCounts(const Model3doMesh& mesh)
{
    std::vector<std::size_t> counts(mesh.texVertices.size());
    if ( counts.empty() )
    {
        return counts;
    }
    for ( const auto& face : mesh.faces )
    {
        for ( const auto index : face.texVertices )
        {
            if ( index >= 0 )
            {
                ++counts.at(static_cast<std::size_t>(index));
            }
        }
    }
    return counts;
}

Json address(std::size_t geoset, std::size_t mesh)
{
    Json row = Json::makeObject();
    row.set("geoset", count(geoset));
    row.set("sourceMesh", count(mesh));
    return row;
}

Json common(const std::string& command, const std::string& sourceHash)
{
    auto result = Json::makeObject();
    result.set("ok", Json::makeBool(true));
    result.set("command", Json::makeString(command));
    result.set("inputSha256", Json::makeString(sourceHash));
    result.set("profile", Json::makeString("classic2.1"));
    result.set("coordinateSpace", Json::makeString("mesh-local"));
    result.set("uvUnits", Json::makeString("texture-pixels"));
    result.set("runtimeVerified", Json::makeBool(false));
    return result;
}

void requireBounded(const Json& response)
{
    if ( toJsonString(response).size() > nativeReceiptLimit() )
    {
        throw AgentError{ kExitRefusal, "Native model receipt exceeds 1 MiB", "modelNativeResponseLimit" };
    }
}

Json meshRow(std::size_t geoset, std::size_t meshId, const Model3doMesh& mesh, std::size_t nodes)
{
    auto row = address(geoset, meshId);
    if ( mesh.name.size() > 256 || !isValidJsonUtf8(mesh.name) )
    {
        row.set("nameOmitted", Json::makeBool(true));
    }
    else
    {
        row.set("name", Json::makeString(mesh.name));
    }
    row.set("vertices", count(mesh.vertices.size()));
    row.set("textureVertices", count(mesh.texVertices.size()));
    row.set("faces", count(mesh.faces.size()));
    row.set("affectedNodeCount", count(nodes));
    row.set("bounds", bounds(mesh));
    row.set("authoredRadius", Json::makeNumber(mesh.authoredRadius));
    return row;
}

struct Batch
{
    std::vector<NativeMeshTransform> transforms;
    std::vector<NativeTextureVertexEdit> uvs;
    std::vector<NativeMaterialRename> materials;
};

float textureComponent(const Json& item, const char* key)
{
    constexpr double max = std::numeric_limits<float>::max();
    const double n = number(required(item, key), key, -max, max);
    const auto stored = static_cast<float>(n);
    if ( n != 0 && stored == 0 )
    {
        invalid("Nonzero texture component would round to zero");
    }
    return stored;
}

Batch parseBatch(const Json& request, const std::string& sourceHash)
{
    fields(request, { "schemaVersion", "profile", "inputSha256", "operations" });
    (void)ordinal(request, "schemaVersion", 1, 1);
    if ( stringField(request, "profile") != "classic2.1" )
    {
        invalid("Expected profile classic2.1");
    }
    const std::string inputHash = digest(stringField(request, "inputSha256"));
    const auto& operations = required(request, "operations");
    if ( operations.type != Json::Type::Array || operations.array.empty() || operations.array.size() > 256 )
    {
        invalid("Expected 1 through 256 operations");
    }
    Batch batch;
    for ( const auto& item : operations.array )
    {
        const auto op = stringField(item, "op");
        if ( op == "renameMaterial" )
        {
            // The model's material table entry by ordinal (model-native-inspect, collection materials).
            fields(item, { "op", "material", "name" });
            const auto name = stringField(item, "name");
            if ( name.size() > 31 )
            {
                invalid("Material names hold 1 to 31 bytes");
            }
            batch.materials.push_back({ ordinal(item, "material", 4095), std::string(name) });
            continue;
        }
        const auto lod = ordinal(item, "geoset", 31);
        const auto mesh = ordinal(item, "sourceMesh", 4095);
        if ( op == "setTextureVertex" )
        {
            fields(item, { "op", "geoset", "sourceMesh", "textureVertex", "u", "v" });
            batch.uvs.push_back({ lod, mesh, ordinal(item, "textureVertex", 262143),
                textureComponent(item, "u"), textureComponent(item, "v") });
        }
        else if ( op == "transformMesh" )
        {
            fields(item, { "op", "geoset", "sourceMesh", "scale", "translation", "radiusPolicy" });
            if ( stringField(item, "radiusPolicy") != "preserve" )
            {
                invalid("Expected radiusPolicy preserve");
            }
            const auto& t = required(item, "translation");
            if ( t.type != Json::Type::Array || t.array.size() != 3 )
            {
                invalid("Expected three translation components");
            }
            const double scale = number(required(item, "scale"), "scale", 1e-6, 1e6);
            batch.transforms.push_back({ lod, mesh, scale, { number(t.array[0], "translation", -1e6, 1e6),
                number(t.array[1], "translation", -1e6, 1e6), number(t.array[2], "translation", -1e6, 1e6) } });
        }
        else
        {
            invalid("Unknown native model operation");
        }
    }
    if ( (!batch.transforms.empty()) + (!batch.uvs.empty()) + (!batch.materials.empty()) > 1 )
    {
        invalid("Mixed native operation families");
    }
    if ( inputHash != sourceHash )
    {
        throw AgentError{ kExitConflict, "Native model input changed; inspect and plan again", "modelNativeStaleInput" };
    }
    return batch;
}

NativeModelDocument applyBatch(const NativeModelDocument& document, const Batch& batch)
{
    try
    {
        return batch.uvs.empty() ? document.applyMeshTransforms(batch.transforms)
            : document.applyTextureVertexEdits(batch.uvs);
    }
    catch ( const std::exception& error )
    {
        throw AgentError{ kExitRefusal, error.what(), "modelNativeEditRefused" };
    }
}

Json uvValues(const sed::assets::Model3doTexVertex& uv)
{
    auto result = Json::makeObject();
    result.set("u", Json::makeNumber(uv.u));
    result.set("v", Json::makeNumber(uv.v));
    return result;
}

void reportEdits(Json& response, const Batch& batch, const std::span<const sed::assets::Model3do> before,
    const std::span<const sed::assets::Model3do> after)
{
    auto operations = Json::makeArray();
    std::size_t changed{};
    bool changedSharedUv{};
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> references;
    for ( const auto& edit : batch.uvs )
    {
        const auto& mesh = before[edit.geoset].meshes[edit.sourceMesh];
        auto [entry, inserted] = references.try_emplace({ edit.geoset, edit.sourceMesh });
        if ( inserted )
        {
            entry->second = cornerCounts(mesh);
        }
        const auto& oldUv = mesh.texVertices[edit.textureVertex];
        const auto& newUv = after[edit.geoset].meshes[edit.sourceMesh].texVertices[edit.textureVertex];
        const bool effective = oldUv.u != newUv.u || oldUv.v != newUv.v;
        auto row = address(edit.geoset, edit.sourceMesh);
        row.set("op", Json::makeString("setTextureVertex"));
        row.set("textureVertex", count(edit.textureVertex));
        row.set("before", uvValues(oldUv));
        row.set("after", uvValues(newUv));
        const auto referencesToRow = entry->second[edit.textureVertex];
        row.set("referencingCorners", count(referencesToRow));
        changedSharedUv |= effective && referencesToRow > 1;
        row.set("changed", Json::makeBool(effective));
        changed += effective;
        operations.array.push_back(std::move(row));
    }
    std::map<std::size_t, std::vector<std::size_t>> instances;
    for ( const auto& transform : batch.transforms )
    {
        const auto& lod = before[transform.geoset];
        auto [entry, inserted] = instances.try_emplace(transform.geoset);
        if ( inserted )
        {
            entry->second = nodeCounts(lod);
        }
        const auto& previous = lod.meshes[transform.sourceMesh];
        const auto& candidate = after[transform.geoset].meshes[transform.sourceMesh];
        std::size_t rows{}, components{};
        for ( std::size_t i = 0; i < previous.vertices.size(); ++i )
        {
            const auto& a = previous.vertices[i];
            const auto& b = candidate.vertices[i];
            const auto different = static_cast<std::size_t>(a.x != b.x) + (a.y != b.y) + (a.z != b.z);
            rows += different != 0;
            components += different;
        }
        auto row = address(transform.geoset, transform.sourceMesh);
        row.set("op", Json::makeString("transformMesh"));
        row.set("scale", Json::makeNumber(transform.scale));
        const auto& t = transform.translation;
        row.set("translation", xyz(t.x, t.y, t.z));
        row.set("changed", Json::makeBool(rows != 0));
        row.set("changedPositionRows", count(rows));
        row.set("changedComponents", count(components));
        row.set("affectedNodeCount", count(entry->second[transform.sourceMesh]));
        row.set("beforeBounds", bounds(previous));
        row.set("afterBounds", bounds(candidate));
        row.set("authoredRadius", Json::makeNumber(previous.authoredRadius));
        changed += rows != 0;
        operations.array.push_back(std::move(row));
    }
    for ( const auto& rename : batch.materials )
    {
        const auto& previous = before.front().materials[rename.material];
        const auto& current = after.front().materials[rename.material];
        std::size_t faces{};
        for ( const auto& lod : before )
        {
            for ( const auto& mesh : lod.meshes )
            {
                for ( const auto& face : mesh.faces )
                {
                    faces += face.material == static_cast<std::int32_t>(rename.material);
                }
            }
        }
        auto row = Json::makeObject();
        row.set("op", Json::makeString("renameMaterial"));
        row.set("material", count(rename.material));
        row.set("before", previous.size() <= 256 && isValidJsonUtf8(previous) ? Json::makeString(previous) : Json{});
        row.set("after", Json::makeString(current));
        row.set("referencingFaces", count(faces));
        const bool effective = previous != current;
        row.set("changed", Json::makeBool(effective));
        changed += effective;
        operations.array.push_back(std::move(row));
    }
    auto warnings = Json::makeArray();
    const bool geometryChanged = !batch.transforms.empty() && changed != 0;
    if ( geometryChanged )
    {
        warnings.array.push_back(Json::makeString("Review retained authored radii against the new shape and revalidate stored lighting and external Level/ARCH bindings."));
    }
    if ( changedSharedUv )
    {
        warnings.array.push_back(Json::makeString("Every corner referencing a changed shared UV row receives its new coordinates."));
    }
    if ( !batch.materials.empty() && changed != 0 )
    {
        warnings.array.push_back(Json::makeString("Faces naming a renamed entry draw the new MAT: package it with the model, "
            "and keep the old MAT only if another model or the level still names it."));
    }
    response.set("changedOperations", count(changed));
    response.set("noopOperations", count(operations.array.size() - changed));
    response.set("operations", std::move(operations));
    response.set("lightingNeedsRevalidation", Json::makeBool(geometryChanged));
    for ( const auto* key : { "normalsPreserved", "hierarchyPreserved", "radiiPreserved", "storedLightingPreserved" } )
    {
        response.set(key, Json::makeBool(true));
    }
    response.set("warnings", std::move(warnings));
}
} // namespace

bool isModelNativeCommand(const std::string_view name)
{
    return name == "model-collision-compare" || name == "model-native-inspect"
        || name == "model-native-plan" || name == "model-native-apply";
}

Json modelCollisionCompare(const std::vector<std::string>& args)
{
    if ( args.size() != 2 )
    {
        invalid("model-collision-compare requires <source.3do> <candidate.3do>");
    }
    const auto limit = sed::assets::ModelLightingLimits{}.sourceBytes;
    const auto source = readFileBytes(nativePath(args[0]), limit);
    const auto candidate = readFileBytes(nativePath(args[1]), limit);
    const auto original = openSource(source, "Source model: ");
    const auto replacement = openSource(candidate, "Candidate model: ");
    const auto comparison = sed::assets::compareModelCollision(original, replacement);
    const auto summary = [](const sed::assets::ModelCollisionFaceCounts& counts, const std::string& bytes)
    {
        auto side = Json::makeObject();
        side.set("sha256", Json::makeString(sha256Hex(bytes)));
        side.set("meshes", count(counts.meshes));
        side.set("faces", count(counts.faces));
        side.set("untexturedModeZeroFaces", count(counts.untexturedModeZeroFaces));
        return side;
    };
    auto receipt = Json::makeObject();
    receipt.set("ok", Json::makeBool(true));
    receipt.set("command", Json::makeString("model-collision-compare"));
    receipt.set("profile", Json::makeString("classic2.1"));
    receipt.set("geoset", count(0));
    receipt.set("coordinateSpace", Json::makeString("mesh-local"));
    receipt.set("source", summary(comparison.source, source));
    receipt.set("candidate", summary(comparison.candidate, candidate));
    receipt.set("matchedFaces", count(comparison.matchedFaces));
    receipt.set("missingFaces", count(comparison.missingFaces));
    receipt.set("extraFaces", count(comparison.extraFaces));
    receipt.set("sameFaceMultiset", Json::makeBool(comparison.missingFaces == 0 && comparison.extraFaces == 0));
    receipt.set("sameFaceSequence", Json::makeBool(comparison.sameFaceSequence));
    receipt.set("sameMeshFaceCounts", Json::makeBool(comparison.sameMeshFaceCounts));
    receipt.set("runtimeVerified", Json::makeBool(false));
    requireBounded(receipt);
    return receipt;
}

Json modelNativeInspect(const std::vector<std::string>& args)
{
    if ( args.empty() || args.size() > 2 )
    {
        invalid("model-native-inspect requires <input.3do> [query.json|-]");
    }
    const auto source = readFileBytes(nativePath(args[0]), sed::assets::ModelLightingLimits{}.sourceBytes);
    const auto query = args.size() == 2 ? readRequestFile(nativePath(args[1])) : parseJson(R"({"collection":"meshes"})");
    const auto collection = stringField(query, "collection");
    const bool meshes = collection == "meshes";
    const bool materials = collection == "materials";
    if ( meshes || materials )
    {
        fields(query, { "collection", "offset", "limit" });
    }
    else if ( collection == "vertices" || collection == "textureVertices" )
    {
        fields(query, { "collection", "offset", "limit", "geoset", "sourceMesh" });
    }
    else
    {
        invalid("Unknown inspect collection");
    }
    const std::size_t offset = query.find("offset") ? ordinal(query, "offset", 262144) : 0;
    const std::size_t limit = query.find("limit") ? ordinal(query, "limit", 128, 1) : 64;
    std::vector<sed::assets::Model3do> tableLods;
    std::optional<NativeModelDocument> document;
    std::span<const sed::assets::Model3do> lods;
    if ( materials )
    {
        tableLods = readGeosets(source);
        lods = tableLods;
    }
    else
    {
        document.emplace(openSource(source));
        lods = document->geosets();
    }
    auto items = Json::makeArray();
    std::size_t total{};
    if ( materials )
    {
        // The material table is the model's, shared by every geoset; faces count across all of them.
        const auto& table = lods.empty() ? std::vector<std::string>{} : lods.front().materials;
        total = table.size();
        std::vector<std::size_t> faces(table.size());
        for ( const auto& lod : lods )
        {
            for ( const auto& mesh : lod.meshes )
            {
                for ( const auto& face : mesh.faces )
                {
                    if ( face.material >= 0 && static_cast<std::size_t>(face.material) < faces.size() )
                    {
                        ++faces[static_cast<std::size_t>(face.material)];
                    }
                }
            }
        }
        for ( auto i = offset; i < total && items.array.size() < limit; ++i )
        {
            Json row = Json::makeObject();
            row.set("material", count(i));
            if ( table[i].size() > 256 || !isValidJsonUtf8(table[i]) )
            {
                row.set("nameOmitted", Json::makeBool(true));
            }
            else
            {
                row.set("name", Json::makeString(table[i]));
            }
            row.set("referencingFaces", count(faces[i]));
            items.array.push_back(std::move(row));
        }
    }
    else if ( meshes )
    {
        for ( std::size_t g = 0; g < lods.size(); ++g )
        {
            const auto& lod = lods[g];
            const auto first = offset > total ? offset - total : 0;
            if ( first < lod.meshes.size() && items.array.size() < limit )
            {
                const auto counts = nodeCounts(lod);
                for ( auto m = first; m < lod.meshes.size() && items.array.size() < limit; ++m )
                {
                    items.array.push_back(meshRow(g, m, lod.meshes[m], counts[m]));
                }
            }
            total += lod.meshes.size();
        }
    }
    else
    {
        const auto g = ordinal(query, "geoset", 31);
        const auto m = ordinal(query, "sourceMesh", 4095);
        if ( g >= lods.size() || m >= lods[g].meshes.size() )
        {
            invalid("Inspect mesh address outside document");
        }
        const auto& mesh = lods[g].meshes[m];
        const bool uv = collection == "textureVertices";
        total = uv ? mesh.texVertices.size() : mesh.vertices.size();
        const auto refs = uv && offset < total ? cornerCounts(mesh) : std::vector<std::size_t>{};
        for ( auto i = offset; i < total && items.array.size() < limit; ++i )
        {
            Json row = Json::makeObject();
            if ( uv )
            {
                row = uvValues(mesh.texVertices[i]);
                row.set("textureVertex", count(i));
                row.set("referencingCorners", count(refs[i]));
            }
            else
            {
                const auto& v = mesh.vertices[i];
                row.set("vertex", count(i));
                row.set("x", Json::makeNumber(v.x));
                row.set("y", Json::makeNumber(v.y));
                row.set("z", Json::makeNumber(v.z));
            }
            items.array.push_back(std::move(row));
        }
    }
    auto response = common("model-native-inspect", sha256Hex(source));
    response.set("geosetCount", count(lods.size()));
    response.set("collection", Json::makeString(std::string(collection)));
    response.set("offset", count(offset));
    response.set("total", count(total));
    const auto next = offset + items.array.size();
    response.set("nextOffset", next < total ? count(next) : Json{});
    response.set("items", std::move(items));
    requireBounded(response);
    return response;
}

Json modelNativePlanOrApply(const std::vector<std::string>& args, const bool apply)
{
    const std::string command = apply ? "model-native-apply" : "model-native-plan";
    if ( args.size() != (apply ? 5u : 2u) )
    {
        invalid(command + " requires <input.3do> <request.json|->" +
            (apply ? " <expected-request-sha256> <expected-output-sha256> <new-output.3do>" : ""));
    }
    const auto source = readFileBytes(nativePath(args[0]), sed::assets::ModelLightingLimits{}.sourceBytes);
    const auto inputHash = sha256Hex(source);
    const auto request = readRequestFile(nativePath(args[1]));
    const auto batch = parseBatch(request, inputHash);
    const auto intent = sha256Hex(toJsonString(request));
    // A rename touches only the material table, so it takes any model the ordinary reader reads (and reads the
    // result back the same way); UV and transform edits need the native document.
    std::vector<sed::assets::Model3do> beforeLods, afterLods;
    std::optional<NativeModelDocument> document, candidate;
    std::span<const sed::assets::Model3do> before, after;
    std::string output;
    if ( !batch.materials.empty() )
    {
        beforeLods = readGeosets(source);
        try
        {
            output = sed::assets::renameModelMaterials(source, batch.materials);
        }
        catch ( const std::exception& error )
        {
            throw AgentError{ kExitRefusal, error.what(), "modelNativeEditRefused" };
        }
        afterLods = output == source ? beforeLods : readGeosets(output, "renamed model: ");
        before = beforeLods;
        after = afterLods;
    }
    else
    {
        document.emplace(openSource(source));
        candidate.emplace(applyBatch(*document, batch));
        output = std::string(candidate->source());
        before = document->geosets();
        after = candidate->geosets();
    }
    const auto outputHash = sha256Hex(output);
    std::filesystem::path destination;
    if ( apply )
    {
        const auto reviewedRequest = digest(args[2]);
        const auto reviewedOutput = digest(args[3]);
        if ( reviewedRequest != intent || reviewedOutput != outputHash )
        {
            throw AgentError{ kExitConflict, "Reviewed request or output differs; plan again", "modelNativePlanMismatch" };
        }
        destination = nativePath(args[4]);
        const auto ext = pathToUtf8(destination.extension());
        if ( !sed::text::iequalsAscii(ext, ".3do") )
        {
            invalid("Output must end in .3do");
        }
    }
    auto receipt = common(command, inputHash);
    receipt.set("requestSha256", Json::makeString(intent));
    receipt.set("outputSha256", Json::makeString(outputHash));
    receipt.set("written", Json::makeBool(apply));
    reportEdits(receipt, batch, before, after);
    if ( apply )
    {
        receipt.set("output", Json::makeString(pathToUtf8(destination)));
    }
    requireBounded(receipt);
    if ( apply )
    {
        writeNewFileAtomic(destination, output);
    }
    return receipt;
}

} // namespace sed_agent
