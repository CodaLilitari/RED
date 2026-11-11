#pragma once

// Shared by the OBJ and binary glTF readers: the source model they both
// produce and the builder that turns it into a JK 3DO. Not a public header.

#include "sed/assets/obj_import.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sed::assets::model_import
{

struct Vec3
{
    double x{};
    double y{};
    double z{};
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) noexcept
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-(const Vec3& a, const Vec3& b) noexcept
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator*(const Vec3& a, const double s) noexcept
{
    return { a.x * s, a.y * s, a.z * s };
}

inline double dot(const Vec3& a, const Vec3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) noexcept
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline double length(const Vec3& a) noexcept
{
    return std::sqrt(dot(a, a));
}

inline bool isFinite(const Vec3& a) noexcept
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

struct SourceCorner
{
    std::int64_t vertex{};
    std::int64_t texture{ -1 };
    // OBJ: the vn index (its value is not used); glTF: index into
    // SourceModel::normals, -1 when the corner has no usable normal.
    std::int64_t normal{ -1 };
};

struct SourcePolygon
{
    // OBJ: the source line. glTF: the triangle's 1-based ordinal in its
    // primitive, which `place` names.
    std::size_t line{};
    std::size_t place{};
    std::size_t mesh{};
    std::int64_t material{ -1 }; // index into SourceModel::materialNames; -1 when the face has none
    std::size_t firstCorner{};
    std::size_t cornerCount{};
    bool textured{};
};

struct SourceMeshName
{
    std::string object{};
    std::string group{};
};

/** A polygon model in source axes, before placement and JK conversion. */
struct SourceModel
{
    ModelSourceFormat format{ ModelSourceFormat::Obj };
    std::vector<Vec3> vertices{};
    std::vector<std::array<double, 2>> textureCoordinates{};
    // Authored unit normals (glTF only), in source axes after node transforms.
    std::vector<Vec3> normals{};
    // OBJ: how many vn statements were read (their values are not kept).
    std::size_t normalCount{};
    std::vector<SourceCorner> corners{};
    std::vector<SourcePolygon> polygons{};
    std::vector<std::string> materialNames{};
    std::vector<SourceMeshName> meshes{};
    // glTF: where each polygon's primitive is, e.g. "node 'body' primitive 0".
    std::vector<std::string> places{};
    std::vector<std::pair<std::string, std::size_t>> ignored{};
    // glTF texture coordinates start at the image's top edge, OBJ's at its
    // bottom edge.
    bool textureOriginTop{ false };
    // glTF faces without area are dropped and counted; OBJ refuses them.
    bool dropDegenerate{ false };
    // glTF triangles the reader already dropped (two corners on one position).
    std::size_t droppedDegenerate{};
};

[[noreturn]] void refuse(ObjImportErrorKind kind, const char* code, const std::string& message);

// A token quoted for a message, shortened when long.
std::string inQuotes(std::string_view token);

void validateOptions(const ObjImportOptions& options);

/** Places, triangulates, splits and converts a source model into a 3DO. */
ObjImportResult buildModel(const SourceModel& source, const ObjImportOptions& options);

} // namespace sed::assets::model_import
