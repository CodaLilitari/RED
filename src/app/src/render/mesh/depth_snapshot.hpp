#pragma once

#include <QOpenGLContext>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QVector2D>
#include <array>
#include <cstdint>

namespace sed::render
{

struct DepthSnapshot
{
    bool valid{};
    GLuint texture{};
    GLenum target{};
    QSize extent;
    GLenum internalFormat{};
    int depthBits{};
    int stencilBits{};
    int sourceSamples{};
    bool floatingPoint{};
    std::array<QVector2D, 16> sampleOffsets{};
    QVector2D meanSampleOffset;
    QVector2D maximumAbsoluteSampleOffset;
    std::uint64_t allocationBytes{};
    QString diagnostic;
};

// GUI-thread owner of one current-frame depth copy. Capture requires the exact
// source owner current. Release accepts a current context in the same live share
// group; otherwise it reports refusal and retains ownership for a later release.
// A destroyed share group invalidates the numeric handle without issuing GL.
class DepthSnapshotCache
{
public:
    DepthSnapshotCache() = default;
    ~DepthSnapshotCache();
    DepthSnapshotCache(const DepthSnapshotCache&) = delete;
    DepthSnapshotCache& operator=(const DepthSnapshotCache&) = delete;

    const DepthSnapshot& capture(QOpenGLContext* owner, GLuint sourceFramebuffer, QSize extent,
        std::uint64_t byteBudget = 128ull * 1024 * 1024);
    bool release();
    const DepthSnapshot& current() const;

private:
    mutable DepthSnapshot snapshot_;
    QPointer<QOpenGLContext> owner_;
    QPointer<QOpenGLContextGroup> group_;
};

} // namespace sed::render
