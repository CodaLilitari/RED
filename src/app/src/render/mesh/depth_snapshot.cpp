#include "depth_snapshot.hpp"

#include <QDebug>
#include <QOpenGLFunctions_3_3_Core>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QOpenGLVersionFunctionsFactory>
#endif
#include <algorithm>
#include <cmath>

namespace sed::render
{
namespace
{
using Functions = QOpenGLFunctions_3_3_Core;

struct StateGuard
{
    Functions* f;
    GLint read{};
    GLint draw{};
    GLint active{};
    GLint texture2d{};
    GLint textureMs{};
    GLint renderbuffer{};
    GLint unpackBuffer{};
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissor{};
    GLboolean scissorEnabled{};

    explicit StateGuard(Functions* functions) : f(functions)
    {
        f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        f->glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2d);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE, &textureMs);
        f->glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        f->glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        f->glGetIntegerv(GL_VIEWPORT, viewport.data());
        f->glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
        scissorEnabled = f->glIsEnabled(GL_SCISSOR_TEST);
    }

    ~StateGuard()
    {
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        f->glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        if ( scissorEnabled )
        {
            f->glEnable(GL_SCISSOR_TEST);
        }
        else
        {
            f->glDisable(GL_SCISSOR_TEST);
        }
        f->glActiveTexture(static_cast<GLenum>(active));
        f->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2d));
        f->glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, static_cast<GLuint>(textureMs));
        f->glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(renderbuffer));
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer));
    }
};

struct FramebufferGuard
{
    Functions* f;
    GLuint name{};

    explicit FramebufferGuard(Functions* functions) : f(functions)
    {
        f->glGenFramebuffers(1, &name);
    }

    ~FramebufferGuard()
    {
        if ( name )
        {
            f->glDeleteFramebuffers(1, &name);
        }
    }
};

struct TextureGuard
{
    Functions* f;
    GLuint name{};

    explicit TextureGuard(Functions* functions) : f(functions)
    {
    }

    ~TextureGuard()
    {
        if ( name )
        {
            f->glDeleteTextures(1, &name);
        }
    }
};

Functions* currentFunctions()
{
    auto* context = QOpenGLContext::currentContext();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto* f = context ? QOpenGLVersionFunctionsFactory::get<Functions>(context) : nullptr;
#else
    auto* f = context ? context->versionFunctions<Functions>() : nullptr;
#endif
    return f && f->initializeOpenGLFunctions() ? f : nullptr;
}

bool textureBoundOnAnyUnit(Functions* f, GLuint texture, GLenum target)
{
    GLint active = 0;
    GLint count = 0;
    f->glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    f->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &count);
    bool found = false;
    for ( GLint unit = 0; unit < count; ++unit )
    {
        f->glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        GLint binding = 0;
        f->glGetIntegerv(target == GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_2D_MULTISAMPLE, &binding);
        found = found || static_cast<GLuint>(binding) == texture;
    }
    f->glActiveTexture(static_cast<GLenum>(active));
    return found;
}

} // namespace

DepthSnapshotCache::~DepthSnapshotCache()
{
    if ( !release() )
    {
        qWarning().nospace() << "Depth snapshot cleanup could not acquire a matching current share group; "
                            << snapshot_.allocationBytes << " bytes remain allocated until that share group is destroyed.";
    }
}

const DepthSnapshot& DepthSnapshotCache::capture(QOpenGLContext* owner, GLuint sourceFramebuffer,
    QSize extent, std::uint64_t byteBudget)
{
    snapshot_.valid = false;
    const auto refuse = [&](const QString& diagnostic) -> const DepthSnapshot&
    {
        snapshot_.diagnostic = diagnostic;
        return snapshot_;
    };
    if ( !owner || owner != QOpenGLContext::currentContext() || !owner->isValid() )
    {
        return refuse(QStringLiteral("Depth capture requires its exact live source context current."));
    }
    auto* f = currentFunctions();
    if ( !f )
    {
        return refuse(QStringLiteral("Depth capture requires desktop OpenGL 3.3 functions."));
    }
    if ( group_.isNull() )
    {
        snapshot_ = {};
        owner_.clear();
    }
    else if ( group_ != owner->shareGroup() )
    {
        return refuse(QStringLiteral("The previous depth allocation still belongs to another live share group; release it first."));
    }
    if ( extent.width() <= 0 || extent.height() <= 0 || byteBudget == 0 )
    {
        return refuse(QStringLiteral("Depth capture requires positive dimensions and an allocation budget."));
    }
    const GLenum priorError = f->glGetError();
    if ( priorError != GL_NO_ERROR )
    {
        return refuse(QStringLiteral("Depth capture refused an incoming OpenGL error: %1.").arg(priorError));
    }
    const StateGuard state(f);
    if ( !sourceFramebuffer || !f->glIsFramebuffer(sourceFramebuffer) )
    {
        return refuse(QStringLiteral("Depth capture source is not a live framebuffer object."));
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, sourceFramebuffer);
    if ( f->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE )
    {
        return refuse(QStringLiteral("Depth capture source framebuffer is incomplete."));
    }
    GLint type = 0;
    GLint object = 0;
    f->glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if ( type != GL_RENDERBUFFER )
    {
        return refuse(QStringLiteral("Depth capture supports the widget's depth renderbuffer attachment only."));
    }
    f->glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object);
    f->glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(object));
    GLint width = 0;
    GLint height = 0;
    GLint internalFormat = 0;
    GLint depthBits = 0;
    GLint stencilBits = 0;
    GLint samples = 0;
    GLint maximumSize = 0;
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &internalFormat);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &depthBits);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &stencilBits);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
    f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumSize);
    if ( QSize(width, height) != extent || width > maximumSize || height > maximumSize )
    {
        return refuse(QStringLiteral("Depth capture dimensions must exactly match the source and texture limit."));
    }
    if ( samples < 0 || samples > 16 )
    {
        return refuse(QStringLiteral("Depth capture supports at most 16 actual samples."));
    }
    GLenum pixelFormat = GL_DEPTH_COMPONENT;
    GLenum pixelType = GL_UNSIGNED_INT;
    std::uint64_t bytesPerSample = 4;
    bool floatingPoint = false;
    switch ( internalFormat )
    {
    case GL_DEPTH_COMPONENT16:
        bytesPerSample = 2;
        pixelType = GL_UNSIGNED_SHORT;
        break;
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32:
        break;
    case GL_DEPTH_COMPONENT32F:
        pixelType = GL_FLOAT;
        floatingPoint = true;
        break;
    case GL_DEPTH24_STENCIL8:
        pixelFormat = GL_DEPTH_STENCIL;
        pixelType = GL_UNSIGNED_INT_24_8;
        break;
    case GL_DEPTH32F_STENCIL8:
        pixelFormat = GL_DEPTH_STENCIL;
        pixelType = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
        bytesPerSample = 8;
        floatingPoint = true;
        break;
    default:
        return refuse(QStringLiteral("Unsupported actual depth internal format: %1.").arg(internalFormat));
    }
    const auto bytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height)
        * static_cast<std::uint64_t>(std::max(1, samples)) * bytesPerSample;
    if ( bytes > byteBudget )
    {
        return refuse(QStringLiteral("Depth snapshot needs %1 bytes, exceeding its %2-byte budget.")
                          .arg(bytes).arg(byteBudget));
    }
    DepthSnapshot next;
    next.extent = extent;
    next.internalFormat = static_cast<GLenum>(internalFormat);
    next.depthBits = depthBits;
    next.stencilBits = stencilBits;
    next.sourceSamples = samples;
    next.floatingPoint = floatingPoint;
    next.allocationBytes = bytes;
    next.target = samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    // Query the SOURCE draw framebuffer's positions. A same-count blit copies
    // sample values by index; destination rasterization positions are irrelevant.
    for ( int sample = 0; sample < samples; ++sample )
    {
        GLfloat position[2]{};
        f->glGetMultisamplefv(GL_SAMPLE_POSITION, static_cast<GLuint>(sample), position);
        if ( !std::isfinite(position[0]) || !std::isfinite(position[1])
            || position[0] < 0 || position[0] > 1 || position[1] < 0 || position[1] > 1 )
        {
            return refuse(QStringLiteral("Depth source returned invalid sample positions."));
        }
        const QVector2D offset(position[0] - 0.5f, position[1] - 0.5f);
        next.sampleOffsets[static_cast<std::size_t>(sample)] = offset;
        next.meanSampleOffset += offset;
        next.maximumAbsoluteSampleOffset.setX(std::max(next.maximumAbsoluteSampleOffset.x(), std::abs(offset.x())));
        next.maximumAbsoluteSampleOffset.setY(std::max(next.maximumAbsoluteSampleOffset.y(), std::abs(offset.y())));
    }
    next.meanSampleOffset /= static_cast<float>(std::max(1, samples));
    if ( f->glGetError() != GL_NO_ERROR )
    {
        return refuse(QStringLiteral("Depth source metadata query failed."));
    }
    const bool reuse = snapshot_.texture != 0 && snapshot_.extent == extent
        && snapshot_.internalFormat == next.internalFormat && snapshot_.sourceSamples == samples;
    if ( !reuse && snapshot_.texture )
    {
        if ( textureBoundOnAnyUnit(f, snapshot_.texture, snapshot_.target) )
        {
            return refuse(QStringLiteral("Unbind the previous depth image before resizing its allocation."));
        }
        if ( !release() )
        {
            return refuse(QStringLiteral("Previous depth image could not be released in its owning share group."));
        }
    }
    // Release changed-size storage before allocating: at most one full image.
    TextureGuard created(f);
    if ( !reuse )
    {
        f->glGenTextures(1, &created.name);
        if ( !created.name )
        {
            return refuse(QStringLiteral("Depth texture name allocation failed."));
        }
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        f->glBindTexture(next.target, created.name);
        if ( samples > 0 )
        {
            f->glTexImage2DMultisample(next.target, samples, next.internalFormat, width, height, GL_TRUE);
        }
        else
        {
            f->glTexImage2D(next.target, 0, internalFormat, width, height, 0, pixelFormat, pixelType, nullptr);
            f->glTexParameteri(next.target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            f->glTexParameteri(next.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            f->glTexParameteri(next.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            f->glTexParameteri(next.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            f->glTexParameteri(next.target, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        }
        if ( f->glGetError() != GL_NO_ERROR )
        {
            return refuse(QStringLiteral("Depth texture storage allocation failed."));
        }
        next.texture = created.name;
    }
    else
    {
        next.texture = snapshot_.texture;
    }
    FramebufferGuard destination(f);
    if ( !destination.name )
    {
        return refuse(QStringLiteral("Temporary depth framebuffer allocation failed."));
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, destination.name);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER,
        stencilBits > 0 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, next.target, next.texture, 0);
    f->glDrawBuffer(GL_NONE);
    f->glReadBuffer(GL_NONE);
    if ( f->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE )
    {
        return refuse(QStringLiteral("Depth snapshot framebuffer is incomplete for the actual source format."));
    }
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);
    f->glDisable(GL_SCISSOR_TEST);
    f->glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    if ( f->glGetError() != GL_NO_ERROR )
    {
        return refuse(QStringLiteral("Same-count depth copy failed; no current snapshot is available."));
    }
    if ( !reuse )
    {
        created.name = 0;
    }
    owner_ = owner;
    group_ = owner->shareGroup();
    next.valid = true;
    snapshot_ = next;
    return snapshot_;
}

bool DepthSnapshotCache::release()
{
    snapshot_.valid = false;
    if ( !snapshot_.texture || group_.isNull() )
    {
        snapshot_ = {};
        owner_.clear();
        group_.clear();
        return true;
    }
    auto* context = QOpenGLContext::currentContext();
    if ( !context || context->shareGroup() != group_ )
    {
        snapshot_.diagnostic = QStringLiteral("Depth allocation remains owned until a matching sharing context releases it.");
        return false;
    }
    auto* f = currentFunctions();
    if ( !f )
    {
        return false;
    }
    f->glDeleteTextures(1, &snapshot_.texture);
    snapshot_ = {};
    owner_.clear();
    group_.clear();
    return true;
}

const DepthSnapshot& DepthSnapshotCache::current() const
{
    if ( !owner_ || QOpenGLContext::currentContext() != owner_ || !owner_->isValid() )
    {
        snapshot_.valid = false;
    }
    return snapshot_;
}

} // namespace sed::render
