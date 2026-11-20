#include "ui/lighting/ray_lighting_gpu.hpp"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace sed::ui::lighting
{
namespace
{

constexpr std::size_t kBatchSize = 256;
constexpr char kShader[] = R"GLSL(#version 430 core
layout(local_size_x = 64) in;
struct Triangle { vec4 a; vec4 b; vec4 c; };
struct Node { vec4 minimum; vec4 maximum; ivec4 links; };
struct Ray { vec4 origin; vec4 direction; ivec4 root; };
struct Hit { ivec4 index; vec4 values; };
layout(std430, binding = 0) readonly buffer Triangles { Triangle triangles[]; };
layout(std430, binding = 1) readonly buffer Nodes { Node nodes[]; };
layout(std430, binding = 2) readonly buffer Rays { Ray rays[]; };
layout(std430, binding = 3) writeonly buffer Hits { Hit hits[]; };
uniform int rayCount;
uniform int nodeCount;
uniform int triangleCount;
uniform int maximumQueryWork;

bool boxHit(Node n, Ray r, float nearest)
{
    float low = r.origin.w;
    float high = nearest;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (r.direction[axis] == 0.0)
        {
            if (r.origin[axis] < n.minimum[axis] || r.origin[axis] > n.maximum[axis])
                return false;
        }
        else
        {
            precise float a = (n.minimum[axis] - r.origin[axis]) / r.direction[axis];
            precise float b = (n.maximum[axis] - r.origin[axis]) / r.direction[axis];
            low = max(low, min(a, b));
            high = min(high, max(a, b));
            if (low > high) return false;
        }
    }
    return true;
}

bool triangleHit(Triangle t, Ray r, out vec3 value)
{
    precise vec3 e1 = t.b.xyz - t.a.xyz;
    precise vec3 e2 = t.c.xyz - t.a.xyz;
    precise vec3 p = cross(r.direction.xyz, e2);
    precise float determinant = dot(e1, p);
    precise vec3 normal = cross(e1, e2);
    precise float scale = max(max(abs(normal.x), abs(normal.y)), abs(normal.z))
        * max(max(abs(r.direction.x), abs(r.direction.y)), abs(r.direction.z));
    if (abs(determinant) <= 1e-8 * scale) return false;
    precise float inverse = 1.0 / determinant;
    precise vec3 s = r.origin.xyz - t.a.xyz;
    precise float u = dot(s, p) * inverse;
    if (u < 0.0 || u > 1.0) return false;
    precise vec3 q = cross(s, e1);
    precise float v = dot(r.direction.xyz, q) * inverse;
    if (v < 0.0 || u + v > 1.0) return false;
    precise float distance = dot(e2, q) * inverse;
    if (isnan(distance) || isinf(distance) || distance <= r.origin.w || distance > r.direction.w)
        return false;
    value = vec3(distance, u, v);
    return true;
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(rayCount)) return;
    Ray ray = rays[index];
    Hit result = Hit(ivec4(-1, 0, 0, 0), vec4(0.0));
    int stack[64];
    int top = 0;
    if (ray.root.x >= 0) stack[top++] = ray.root.x;
    float nearest = ray.direction.w;
    int visited = 0;
    int work = 0;
    while (top > 0)
    {
        int nodeIndex = stack[--top];
        if (nodeIndex < 0 || nodeIndex >= nodeCount || visited++ >= nodeCount || ++work > maximumQueryWork)
        {
            result.index.x = -2;
            break;
        }
        Node node = nodes[nodeIndex];
        if (!boxHit(node, ray, nearest)) continue;
        if (node.links.w > 0)
        {
            if (node.links.z < 0 || node.links.w > triangleCount - node.links.z)
            {
                result.index.x = -2;
                break;
            }
            for (int i = node.links.z; i < node.links.z + node.links.w; ++i)
            {
                if (++work > maximumQueryWork)
                {
                    result.index.x = -2;
                    break;
                }
                vec3 hit;
                if (triangleHit(triangles[i], ray, hit)
                    && (hit.x < nearest || (hit.x == nearest && (result.index.x < 0 || i < result.index.x))))
                {
                    nearest = hit.x;
                    result.index.x = i;
                    result.values.xyz = hit;
                }
            }
            if (result.index.x == -2) break;
        }
        else
        {
            if (top + 2 > 64)
            {
                result.index.x = -2;
                break;
            }
            stack[top++] = node.links.y;
            stack[top++] = node.links.x;
        }
    }
    result.index.y = work;
    hits[index] = result;
}
)GLSL";

struct alignas(16) GpuTriangle
{
    std::array<float, 4> a{}, b{}, c{};
};
struct alignas(16) GpuNode
{
    std::array<float, 4> minimum{}, maximum{};
    std::array<std::int32_t, 4> links{};
};
struct alignas(16) GpuRay
{
    std::array<float, 4> origin{}, direction{};
    std::array<std::int32_t, 4> root{};
};
struct alignas(16) GpuHit
{
    std::array<std::int32_t, 4> index{};
    std::array<float, 4> values{};
};
static_assert(sizeof(GpuTriangle) == 48 && sizeof(GpuNode) == 48 && sizeof(GpuRay) == 48);
static_assert(sizeof(GpuHit) == 32 && offsetof(GpuNode, links) == 32 && offsetof(GpuHit, values) == 16);

std::array<float, 4> pack(const level::RayLightVec value, const float w = 0)
{
    return { value.x, value.y, value.z, w };
}

bool finite(const level::RayLightVec value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

struct RayLightingGpu::State
{
    QOffscreenSurface* surface{};
    std::unique_ptr<QOpenGLContext> context{};
    QOpenGLFunctions_4_3_Core gl{};
    std::unique_ptr<QOpenGLShaderProgram> program{};
    std::array<GLuint, 4> buffers{};
    GLint64 maximumBlockSize{};
    bool ready{};
    bool functionsReady{};
    QString description{ QStringLiteral("OpenGL compute unavailable: no offscreen surface.") };
    const level::RayLightTriangle* uploadedTriangles{};
    const level::RayLightNode* uploadedNodes{};
    std::size_t triangleCount{}, nodeCount{};
    std::uint64_t traversalWork{};

    bool fail(const QString& reason)
    {
        description = QStringLiteral("OpenGL compute failed: ") + reason;
        ready = false;
        return false;
    }

    ~State()
    {
        if ( context && surface && context->makeCurrent(surface) )
        {
            if ( functionsReady )
            {
                gl.glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
            }
            program.reset();
            context->doneCurrent();
        }
    }
};

std::shared_ptr<QOffscreenSurface> createRayLightingGpuSurface()
{
    if ( !QGuiApplication::instance()
        || QThread::currentThread() != QGuiApplication::instance()->thread()
        || !QOpenGLContext::supportsThreadedOpenGL() )
    {
        return {};
    }
    auto surface = std::shared_ptr<QOffscreenSurface>(new QOffscreenSurface(), [](QOffscreenSurface* value)
    {
        if ( QThread::currentThread() == value->thread() )
        {
            delete value;
        }
        else
        {
            value->deleteLater();
        }
    });
    QSurfaceFormat format{};
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    surface->setFormat(format);
    surface->create();
    return surface->isValid() ? surface : nullptr;
}

RayLightingGpu::RayLightingGpu(QOffscreenSurface* surface) : state_(std::make_unique<State>())
{
    State& s = *state_;
    s.surface = surface;
    if ( !surface || !surface->isValid() )
    {
        return;
    }
    s.context = std::make_unique<QOpenGLContext>();
    s.context->setFormat(surface->requestedFormat());
    s.context->setScreen(surface->screen());
    if ( !s.context->create() || !s.context->makeCurrent(surface) )
    {
        s.fail(QStringLiteral("cannot create a worker OpenGL 4.3 context."));
        return;
    }
    if ( s.context->isOpenGLES() || !s.gl.initializeOpenGLFunctions() )
    {
        s.fail(QStringLiteral("desktop OpenGL 4.3 compute is not supported."));
        return;
    }
    s.functionsReady = true;
    GLint storageBindings{}, workGroupSize{}, invocations{}, computeStorageBlocks{}, workGroupCount{};
    s.gl.glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &s.maximumBlockSize);
    s.gl.glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &storageBindings);
    s.gl.glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &computeStorageBlocks);
    s.gl.glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &invocations);
    s.gl.glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &workGroupSize);
    s.gl.glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &workGroupCount);
    if ( storageBindings < 4 || computeStorageBlocks < 4 || workGroupSize < 64 || invocations < 64
        || workGroupCount < static_cast<GLint>(kBatchSize / 64)
        || s.maximumBlockSize < static_cast<GLint64>(kBatchSize * sizeof(GpuRay)) )
    {
        s.fail(QStringLiteral("device compute/storage limits are insufficient."));
        return;
    }
    s.program = std::make_unique<QOpenGLShaderProgram>();
    if ( !s.program->addShaderFromSourceCode(QOpenGLShader::Compute, kShader) || !s.program->link() )
    {
        s.fail(QStringLiteral("shader compilation/link failed: ") + s.program->log());
        return;
    }
    s.gl.glGenBuffers(static_cast<GLsizei>(s.buffers.size()), s.buffers.data());
    if ( s.gl.glGetError() != GL_NO_ERROR
        || std::ranges::any_of(s.buffers, [](GLuint buffer) { return buffer == 0; }) )
    {
        s.fail(QStringLiteral("buffer allocation failed."));
        return;
    }
    const auto text = [&s](GLenum name)
    {
        const auto* value = s.gl.glGetString(name);
        return value ? QString::fromUtf8(reinterpret_cast<const char*>(value)) : QStringLiteral("unknown");
    };
    s.description = QStringLiteral("OpenGL compute: %1; %2; %3")
        .arg(text(GL_VENDOR), text(GL_RENDERER), text(GL_VERSION));
    s.ready = true;
}

RayLightingGpu::~RayLightingGpu() = default;

bool RayLightingGpu::available() const noexcept
{
    return state_->ready;
}

QString RayLightingGpu::description() const
{
    return state_->description;
}

bool RayLightingGpu::intersect(std::span<const level::RayLightTriangle> triangles,
    std::span<const level::RayLightNode> nodes, std::span<const level::RayLightRay> rays,
    std::span<level::RayLightHit> hits)
{
    State& s = *state_;
    if ( !s.ready || hits.size() != rays.size() )
    {
        return false;
    }
    if ( rays.empty() )
    {
        return true;
    }
    if ( !s.context->makeCurrent(s.surface) )
    {
        return s.fail(QStringLiteral("worker context was lost."));
    }
    if ( triangles.size() > level::kRayLightingMaxTriangles
        || nodes.size() > 2u * level::kRayLightingMaxTriangles
        || triangles.size() > static_cast<std::size_t>(s.maximumBlockSize) / sizeof(GpuTriangle)
        || nodes.size() > static_cast<std::size_t>(s.maximumBlockSize) / sizeof(GpuNode) )
    {
        return s.fail(QStringLiteral("scene exceeds the device storage budget."));
    }
    if ( s.uploadedTriangles != triangles.data() || s.triangleCount != triangles.size()
        || s.uploadedNodes != nodes.data() || s.nodeCount != nodes.size() )
    {
        std::vector<GpuTriangle> packedTriangles{};
        packedTriangles.reserve(std::max<std::size_t>(1, triangles.size()));
        for ( const auto& triangle : triangles )
        {
            if ( !finite(triangle.a) || !finite(triangle.b) || !finite(triangle.c) )
            {
                return s.fail(QStringLiteral("nonfinite triangle input."));
            }
            packedTriangles.push_back({ pack(triangle.a), pack(triangle.b), pack(triangle.c) });
        }
        if ( packedTriangles.empty() )
        {
            packedTriangles.emplace_back();
        }
        std::vector<GpuNode> packedNodes{};
        packedNodes.reserve(std::max<std::size_t>(1, nodes.size()));
        for ( const auto& node : nodes )
        {
            if ( !finite(node.minimum) || !finite(node.maximum)
                || node.minimum.x > node.maximum.x || node.minimum.y > node.maximum.y
                || node.minimum.z > node.maximum.z || node.count < 0
                || (node.count > 0 && (node.first < 0 || static_cast<std::size_t>(node.first) > triangles.size()
                    || static_cast<std::size_t>(node.count) > triangles.size() - static_cast<std::size_t>(node.first)))
                || (node.count == 0 && (node.left < 0 || node.right < 0
                    || static_cast<std::size_t>(node.left) >= nodes.size()
                    || static_cast<std::size_t>(node.right) >= nodes.size())) )
            {
                return s.fail(QStringLiteral("invalid acceleration structure."));
            }
            packedNodes.push_back({ pack(node.minimum), pack(node.maximum),
                { node.left, node.right, node.first, node.count } });
        }
        if ( packedNodes.empty() )
        {
            packedNodes.emplace_back();
        }
        s.gl.glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[0]);
        s.gl.glBufferData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(packedTriangles.size() * sizeof(GpuTriangle)), packedTriangles.data(), GL_STATIC_DRAW);
        s.gl.glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[1]);
        s.gl.glBufferData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(packedNodes.size() * sizeof(GpuNode)), packedNodes.data(), GL_STATIC_DRAW);
        if ( s.gl.glGetError() != GL_NO_ERROR )
        {
            return s.fail(QStringLiteral("scene upload failed."));
        }
        s.uploadedTriangles = triangles.data();
        s.uploadedNodes = nodes.data();
        s.triangleCount = triangles.size();
        s.nodeCount = nodes.size();
    }
    if ( !s.program->bind() )
    {
        return s.fail(QStringLiteral("compute program could not be bound."));
    }
    s.program->setUniformValue("nodeCount", static_cast<int>(nodes.size()));
    s.program->setUniformValue("triangleCount", static_cast<int>(triangles.size()));
    s.program->setUniformValue("maximumQueryWork", static_cast<int>(level::kRayLightingMaxQueryWork));
    for ( GLuint index = 0; index < s.buffers.size(); ++index )
    {
        s.gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, index, s.buffers[index]);
    }
    std::array<GpuRay, kBatchSize> packedRays{};
    std::array<GpuHit, kBatchSize> packedHits{};
    for ( std::size_t first = 0; first < rays.size(); first += kBatchSize )
    {
        const std::size_t count = std::min(kBatchSize, rays.size() - first);
        for ( std::size_t i = 0; i < count; ++i )
        {
            const auto& ray = rays[first + i];
            if ( !finite(ray.origin) || !finite(ray.direction) || !std::isfinite(ray.minDistance)
                || !std::isfinite(ray.maxDistance) || ray.minDistance < 0 || ray.maxDistance <= ray.minDistance
                || (ray.direction.x == 0 && ray.direction.y == 0 && ray.direction.z == 0)
                || ray.root < -1 || (ray.root >= 0 && static_cast<std::size_t>(ray.root) >= nodes.size()) )
            {
                return s.fail(QStringLiteral("invalid query interval, direction or sector root."));
            }
            packedRays[i] = { pack(ray.origin, ray.minDistance), pack(ray.direction, ray.maxDistance),
                { ray.root, 0, 0, 0 } };
        }
        s.gl.glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[2]);
        s.gl.glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(count * sizeof(GpuRay)),
            packedRays.data(), GL_STREAM_DRAW);
        s.gl.glBindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[3]);
        s.gl.glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(count * sizeof(GpuHit)), nullptr, GL_STREAM_READ);
        s.program->setUniformValue("rayCount", static_cast<int>(count));
        s.gl.glDispatchCompute(static_cast<GLuint>((count + 63) / 64), 1, 1);
        s.gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        s.gl.glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(GpuHit)),
            packedHits.data());
        if ( s.gl.glGetError() != GL_NO_ERROR )
        {
            return s.fail(QStringLiteral("query dispatch or readback failed."));
        }
        for ( std::size_t i = 0; i < count; ++i )
        {
            const auto& hit = packedHits[i];
            if ( hit.index[0] < -1 || (hit.index[0] >= 0 && static_cast<std::size_t>(hit.index[0]) >= triangles.size()) )
            {
                return s.fail(QStringLiteral("bounded traversal rejected the scene."));
            }
            if ( hit.index[1] < 0 || static_cast<std::uint64_t>(hit.index[1]) > level::kRayLightingMaxQueryWork
                || static_cast<std::uint64_t>(hit.index[1]) > level::kRayLightingMaxTraversalWork - s.traversalWork )
            {
                return s.fail(QStringLiteral("traversal work budget exceeded."));
            }
            s.traversalWork += static_cast<std::uint64_t>(hit.index[1]);
            hits[first + i] = { hit.index[0], hit.values[0], hit.values[1], hit.values[2] };
        }
    }
    return true;
}

} // namespace sed::ui::lighting
