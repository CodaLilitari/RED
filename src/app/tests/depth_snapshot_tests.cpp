// Native OpenGL regression: original synthetic depth producers/readback only.
// No game assets, persisted app settings or unrelated windows are accessed.
// The fixture owns every exposed window/context and requires the controlled
// native test desktop. A missing requested GL capability is a test failure.
#include "render/mesh/depth_snapshot.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QOpenGLVersionFunctionsFactory>
#endif
#include <QOpenGLWidget>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
int failures = 0;

void check(bool value, const char* message)
{
    if ( !value )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using Functions = QOpenGLFunctions_3_3_Core;

Functions* functions()
{
    auto* context = QOpenGLContext::currentContext();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto* result = context ? QOpenGLVersionFunctionsFactory::get<Functions>(context) : nullptr;
#else
    auto* result = context ? context->versionFunctions<Functions>() : nullptr;
#endif
    const bool initialized = result && result->initializeOpenGLFunctions();
    check(initialized, "native depth fixture has desktop OpenGL 3.3 core functions");
    return initialized ? result : nullptr;
}

class Surface final : public QOpenGLWidget
{
public:
    explicit Surface(int samples)
    {
        QSurfaceFormat requested = QSurfaceFormat::defaultFormat();
        requested.setDepthBufferSize(16); // Deliberately not the expected actual D24S8.
        requested.setStencilBufferSize(8);
        requested.setSamples(samples);
        setFormat(requested);
        setWindowTitle(QStringLiteral("RED synthetic depth snapshot regression"));
        resize(96, 80);
    }

    bool expose()
    {
        show();
        QElapsedTimer timeout;
        timeout.start();
        while ( (!isValid() || paints == 0) && timeout.elapsed() < 5000 )
        {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        makeCurrent();
        return isValid() && QOpenGLContext::currentContext() == context() && paints > 0;
    }

    int paints{};

protected:
    void paintGL() override
    {
        auto* f = functions();
        if ( !f )
        {
            return;
        }
        f->glClearDepth(1.0);
        f->glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        ++paints;
    }
};

struct State
{
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

    explicit State(Functions* f)
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

    bool operator==(const State&) const = default;
};

struct Source
{
    QSize extent;
    GLint format{};
    GLint depthBits{};
    GLint stencilBits{};
    GLint samples{};
    std::array<QVector2D, 16> offsets{};
};

Source describeSource(Surface& surface, Functions* f)
{
    Source result;
    f->glBindFramebuffer(GL_FRAMEBUFFER, surface.defaultFramebufferObject());
    GLint type = 0;
    GLint name = 0;
    f->glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    f->glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    check(type == GL_RENDERBUFFER && name != 0, "native fixture provides a real QOpenGLWidget depth renderbuffer");
    f->glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(name));
    GLint width = 0;
    GLint height = 0;
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &result.format);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &result.depthBits);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &result.stencilBits);
    f->glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &result.samples);
    result.extent = QSize(width, height);
    check(result.samples <= 16, "native fixture sample count fits its bounded test reader");
    for ( int sample = 0; sample < result.samples && sample < 16; ++sample )
    {
        GLfloat position[2]{};
        f->glGetMultisamplefv(GL_SAMPLE_POSITION, static_cast<GLuint>(sample), position);
        result.offsets[static_cast<std::size_t>(sample)] = QVector2D(position[0] - 0.5f, position[1] - 0.5f);
    }
    f->glBindRenderbuffer(GL_RENDERBUFFER, 0);
    std::cout << "SOURCE fbo=" << surface.defaultFramebufferObject() << " requestedDepth=16 format=" << result.format
              << " depth=" << result.depthBits << " stencil=" << result.stencilBits
              << " samples=" << result.samples << " width=" << width << " height=" << height << '\n';
    return result;
}

const char* vertexShader = R"GLSL(#version 330 core
const vec2 vertices[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main()
{
    gl_Position = vec4(vertices[gl_VertexID], 0, 1);
}
)GLSL";

bool fillDistinctDepthSamples(Surface& surface, Functions* f, const Source& source)
{
    QOpenGLShaderProgram program;
    const bool linked = program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
        && program.addShaderFromSourceCode(QOpenGLShader::Fragment, R"GLSL(#version 330 core
uniform float depthValue;
out vec4 color;
void main()
{
    gl_FragDepth = depthValue;
    color = vec4(0.2, 0.3, 0.4, 1);
}
)GLSL") && program.link();
    check(linked, "native fixture links its independent per-sample depth producer");
    if ( !linked )
    {
        std::cerr << program.log().toStdString() << '\n';
        return false;
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, surface.defaultFramebufferObject());
    f->glViewport(0, 0, source.extent.width(), source.extent.height());
    f->glDisable(GL_SCISSOR_TEST);
    f->glEnable(GL_DEPTH_TEST);
    f->glDepthFunc(GL_ALWAYS);
    f->glDepthMask(GL_TRUE);
    f->glEnable(GL_MULTISAMPLE);
    f->glEnable(GL_SAMPLE_MASK);
    GLuint vao = 0;
    f->glGenVertexArrays(1, &vao);
    f->glBindVertexArray(vao);
    program.bind();
    const int count = std::max(1, source.samples);
    for ( int sample = 0; sample < count; ++sample )
    {
        f->glSampleMaski(0, 1u << sample);
        program.setUniformValue("depthValue", static_cast<float>(sample + 1) / static_cast<float>(count + 2));
        f->glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    program.release();
    f->glBindVertexArray(0);
    f->glDeleteVertexArrays(1, &vao);
    f->glDisable(GL_SAMPLE_MASK);
    f->glSampleMaski(0, ~0u);
    check(f->glGetError() == GL_NO_ERROR, "native per-sample depth producer has no GL error");
    return true;
}

void checkCopiedSamples(Functions* f, const sed::render::DepthSnapshot& snapshot)
{
    QOpenGLFramebufferObjectFormat format;
    format.setInternalTextureFormat(GL_RGBA32F);
    QOpenGLFramebufferObject output(QSize(8, 8), format);
    check(output.isValid(), "native sample reader has a float color target");
    QOpenGLShaderProgram program;
    const char* fragment = snapshot.sourceSamples > 0 ? R"GLSL(#version 330 core
uniform sampler2DMS depthImage;
uniform int sampleIndex;
out vec4 color;
void main()
{
    color = vec4(texelFetch(depthImage, ivec2(3, 4), sampleIndex).r);
}
)GLSL" : R"GLSL(#version 330 core
uniform sampler2D depthImage;
out vec4 color;
void main()
{
    color = vec4(texelFetch(depthImage, ivec2(3, 4), 0).r);
}
)GLSL";
    const bool linked = program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
        && program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) && program.link();
    check(linked, "native fixture links independent depth sampler");
    if ( !linked )
    {
        return;
    }
    output.bind();
    f->glViewport(0, 0, 8, 8);
    f->glDisable(GL_SCISSOR_TEST);
    f->glDisable(GL_DEPTH_TEST);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(snapshot.target, snapshot.texture);
    GLuint vao = 0;
    f->glGenVertexArrays(1, &vao);
    f->glBindVertexArray(vao);
    program.bind();
    program.setUniformValue("depthImage", 0);
    const int count = std::max(1, snapshot.sourceSamples);
    for ( int sample = 0; sample < count; ++sample )
    {
        if ( snapshot.sourceSamples > 0 )
        {
            program.setUniformValue("sampleIndex", sample);
        }
        f->glDrawArrays(GL_TRIANGLES, 0, 3);
        std::array<GLfloat, 4> result{};
        f->glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, result.data());
        const float expected = static_cast<float>(sample + 1) / static_cast<float>(count + 2);
        check(std::abs(result[0] - expected) < 0.000002f,
            "depth copy preserves each distinct source sample instead of resolving or replicating one");
    }
    program.release();
    f->glBindVertexArray(0);
    f->glDeleteVertexArrays(1, &vao);
    f->glBindTexture(snapshot.target, 0);
    check(f->glGetError() == GL_NO_ERROR, "native copied-sample reader has no GL error");
}

void captureCases(int requestedSamples)
{
    Surface surface(requestedSamples);
    check(surface.expose(), "synthetic native QOpenGLWidget becomes valid within five seconds");
    auto* f = functions();
    if ( !surface.isValid() || !f )
    {
        return;
    }
    const Source source = describeSource(surface, f);
    check(requestedSamples == 0 ? source.samples == 0 : source.samples == 4,
        "native fixture actually provides requested single-sample or four-sample depth");
    if ( source.samples > 16 || !fillDistinctDepthSamples(surface, f, source) )
    {
        return;
    }
    sed::render::DepthSnapshotCache cache;
    QOpenGLFramebufferObject readSentinel(QSize(8, 8));
    QOpenGLFramebufferObject drawSentinel(QSize(8, 8));
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, readSentinel.handle());
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawSentinel.handle());
    f->glViewport(3, 4, 13, 11);
    f->glScissor(1, 2, 3, 4);
    f->glEnable(GL_SCISSOR_TEST);
    f->glActiveTexture(GL_TEXTURE3);
    std::array<GLuint, 2> textureSentinels{};
    GLuint renderbuffer = 0;
    GLuint unpackBuffer = 0;
    f->glGenTextures(2, textureSentinels.data());
    f->glBindTexture(GL_TEXTURE_2D, textureSentinels[0]);
    f->glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textureSentinels[1]);
    f->glGenRenderbuffers(1, &renderbuffer);
    f->glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    f->glGenBuffers(1, &unpackBuffer);
    f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
    f->glBufferData(GL_PIXEL_UNPACK_BUFFER, 16, nullptr, GL_STREAM_DRAW);
    const State before(f);
    const auto copied = cache.capture(surface.context(), surface.defaultFramebufferObject(), source.extent);
    check(State(f) == before, "capture restores distinct read/draw FBOs, viewport, scissor, active texture/both bindings, RBO and unpack buffer");
    check(copied.valid && copied.texture != 0, "real QOpenGLWidget opaque depth is captured into a reusable texture");
    if ( !copied.valid )
    {
        std::cerr << "CAPTURE refusal: " << copied.diagnostic.toStdString() << '\n';
    }
    if ( copied.valid )
    {
        check(copied.extent == source.extent && copied.internalFormat == static_cast<GLenum>(source.format)
                && copied.depthBits == source.depthBits && copied.stencilBits == source.stencilBits,
            "snapshot reports actual framebuffer dimensions and depth/stencil precision, not requested format");
        check(copied.sourceSamples == source.samples
                && copied.target == static_cast<GLenum>(source.samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D),
            "snapshot retains the exact source sample count and compatible sampler target");
        QVector2D mean;
        QVector2D maximum;
        for ( int sample = 0; sample < source.samples; ++sample )
        {
            const auto index = static_cast<std::size_t>(sample);
            check(copied.sampleOffsets[index] == source.offsets[index], "snapshot exposes queried per-sample subpixel offsets");
            mean += source.offsets[index];
            maximum.setX(std::max(maximum.x(), std::abs(source.offsets[index].x())));
            maximum.setY(std::max(maximum.y(), std::abs(source.offsets[index].y())));
        }
        mean /= static_cast<float>(std::max(1, source.samples));
        check(copied.meanSampleOffset == mean && copied.maximumAbsoluteSampleOffset == maximum,
            "snapshot sample mean and maximum offsets match actual driver positions");
        const auto again = cache.capture(surface.context(), surface.defaultFramebufferObject(), source.extent);
        check(again.valid && again.texture == copied.texture, "unchanged extent/format/samples reuses one texture allocation");
        check(State(f) == before, "cached capture also preserves exact entry state");
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        checkCopiedSamples(f, copied);
        f->glBindFramebuffer(GL_FRAMEBUFFER, surface.defaultFramebufferObject());
        check(!cache.capture(surface.context(), 0, source.extent).valid && !cache.current().valid,
            "invalid source refuses and cannot expose a stale previous frame");
        check(!cache.capture(surface.context(), surface.defaultFramebufferObject(), source.extent + QSize(1, 0)).valid,
            "source dimension mismatch refuses instead of stretching depth");
        check(!cache.capture(surface.context(), surface.defaultFramebufferObject(), QSize(-1, 80)).valid,
            "nonpositive extent refuses before allocation");
        check(!cache.capture(surface.context(), surface.defaultFramebufferObject(), source.extent, 1).valid,
            "depth image exceeding explicit byte budget refuses without allocating");
        const auto restored = cache.capture(surface.context(), surface.defaultFramebufferObject(), source.extent);
        check(restored.valid && restored.texture == copied.texture, "admission failure retains reusable owned allocation but invalidates its view");
        const GLuint released = restored.texture;
        check(cache.release() && !cache.current().valid && f->glIsTexture(released) == GL_FALSE,
            "normal current-owner release deletes owned texture and invalidates metadata");
    }
    f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    f->glDeleteBuffers(1, &unpackBuffer);
    f->glBindRenderbuffer(GL_RENDERBUFFER, 0);
    f->glDeleteRenderbuffers(1, &renderbuffer);
    f->glActiveTexture(GL_TEXTURE3);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    f->glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    f->glDeleteTextures(2, textureSentinels.data());
    f->glActiveTexture(GL_TEXTURE0);
    f->glDisable(GL_SCISSOR_TEST);
    check(f->glGetError() == GL_NO_ERROR, "capture cases leave no GL error");
}

void lifecycleCases()
{
    auto first = std::make_unique<Surface>(0);
    check(first->expose(), "lifecycle owner context initializes");
    auto* f = functions();
    if ( !f )
    {
        return;
    }
    Source source = describeSource(*first, f);
    sed::render::DepthSnapshotCache cache;
    const auto initial = cache.capture(first->context(), first->defaultFramebufferObject(), source.extent);
    check(initial.valid, "lifecycle fixture captures its original owner");
    if ( !initial.valid )
    {
        return;
    }
    Surface unrelated(0);
    check(unrelated.expose(), "unrelated native context initializes");
    auto* other = functions();
    check(other != nullptr, "unrelated context exposes required OpenGL functions");
    if ( !other )
    {
        return;
    }
    check(!QOpenGLContext::areSharing(first->context(), unrelated.context()),
        "lifecycle fixture uses genuinely unrelated share groups");
    const State otherBefore(other);
    check(!cache.capture(first->context(), first->defaultFramebufferObject(), source.extent).valid,
        "wrong current context refuses source capture");
    check(!cache.release() && State(other) == otherBefore,
        "wrong-context release refuses without modifying unrelated GL state");
    first->makeCurrent();
    check(f->glIsTexture(initial.texture) == GL_TRUE, "wrong-context refusal leaves original allocation owned and alive");
    first->resize(112, 88);
    QApplication::processEvents();
    first->makeCurrent();
    source = describeSource(*first, f);
    const auto resized = cache.capture(first->context(), first->defaultFramebufferObject(), source.extent);
    check(resized.valid && resized.extent == source.extent && source.extent != initial.extent,
        "resize replaces depth storage with exact new physical dimensions");
    const GLuint old = resized.texture;
    first->doneCurrent();
    check(!cache.current().valid, "no current owner makes snapshot view invalid");
    first->makeCurrent();
    check(cache.release() && f->glIsTexture(old) == GL_FALSE, "reacquired owner releases the retained texture");
    cache.capture(first->context(), first->defaultFramebufferObject(), source.extent);
    first.reset();
    unrelated.makeCurrent();
    const State afterLoss(other);
    check(!cache.current().valid, "destroyed owner invalidates cached frame without touching unrelated context");
    check(cache.release() && State(other) == afterLoss,
        "destroyed share group retires its obsolete numeric handle without an unrelated deletion");
    const Source replacementSource = describeSource(unrelated, other);
    check(cache.capture(unrelated.context(), unrelated.defaultFramebufferObject(), replacementSource.extent).valid,
        "cache can initialize fresh after previous share group destruction");
    check(cache.release(), "replacement context releases its independent allocation");
    check(other->glGetError() == GL_NO_ERROR, "context loss and replacement leave no unrelated GL error");
}

void refusalAndFormatCases()
{
    Surface surface(0);
    check(surface.expose(), "boundary source context initializes");
    auto* f = functions();
    if ( !f )
    {
        return;
    }
    const Source source = describeSource(surface, f);
    sed::render::DepthSnapshotCache cache;
    const auto initial = cache.capture(surface.context(), surface.defaultFramebufferObject(), source.extent);
    check(initial.valid, "boundary fixture captures actual widget depth");
    if ( !initial.valid )
    {
        return;
    }
    const std::uint64_t bytesPerSample = source.format == GL_DEPTH_COMPONENT16 ? 2
        : source.format == GL_DEPTH32F_STENCIL8 ? 8 : 4;
    check(initial.allocationBytes == static_cast<std::uint64_t>(source.extent.width())
            * static_cast<std::uint64_t>(source.extent.height()) * bytesPerSample,
        "actual depth format controls the bounded single-sample allocation estimate");
    QOpenGLFramebufferObject colorOnly(QSize(16, 16));
    const State beforeMissing(f);
    const auto missing = cache.capture(surface.context(), colorOnly.handle(), QSize(16, 16));
    check(!missing.valid && missing.diagnostic.contains(QStringLiteral("depth renderbuffer"))
            && State(f) == beforeMissing && f->glIsTexture(initial.texture),
        "missing depth refuses explicitly and preserves existing allocation and exact GL entry state");
    GLuint incomplete = 0;
    f->glGenFramebuffers(1, &incomplete);
    f->glBindFramebuffer(GL_FRAMEBUFFER, incomplete);
    f->glBindFramebuffer(GL_FRAMEBUFFER, surface.defaultFramebufferObject());
    const State beforeIncomplete(f);
    const auto refused = cache.capture(surface.context(), incomplete, QSize(16, 16));
    check(!refused.valid && refused.diagnostic.contains(QStringLiteral("incomplete"))
            && State(f) == beforeIncomplete && f->glIsTexture(initial.texture),
        "incomplete source refuses atomically without deleting the reusable allocation");
    f->glDeleteFramebuffers(1, &incomplete);
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QOpenGLFramebufferObject resized(QSize(48, 32), format);
    f->glActiveTexture(GL_TEXTURE5);
    f->glBindTexture(initial.target, initial.texture);
    const State beforeBound(f);
    const auto bound = cache.capture(surface.context(), resized.handle(), QSize(48, 32));
    check(!bound.valid && bound.diagnostic.contains(QStringLiteral("Unbind"))
            && State(f) == beforeBound && f->glIsTexture(initial.texture),
        "resizing a bound cached image refuses instead of deleting a caller's live texture binding");
    f->glBindTexture(initial.target, 0);
    const auto afterUnbind = cache.capture(surface.context(), resized.handle(), QSize(48, 32));
    check(afterUnbind.valid && afterUnbind.extent == QSize(48, 32),
        "resizing succeeds after the old cached sampler binding is released");
    check(cache.release(), "boundary allocation releases normally");

    // A synthetic explicit D16 renderbuffer proves format selection is not a
    // hardcoded D24S8 assumption, without relying on a driver's widget choice.
    GLuint depth = 0;
    GLuint framebuffer = 0;
    f->glGenRenderbuffers(1, &depth);
    f->glBindRenderbuffer(GL_RENDERBUFFER, depth);
    f->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 32, 24);
    f->glGenFramebuffers(1, &framebuffer);
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    f->glDrawBuffer(GL_NONE);
    f->glReadBuffer(GL_NONE);
    check(f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "explicit D16 fixture is complete");
    const auto d16 = cache.capture(surface.context(), framebuffer, QSize(32, 24));
    check(d16.valid && d16.internalFormat == GL_DEPTH_COMPONENT16 && d16.depthBits == 16
            && d16.stencilBits == 0 && !d16.floatingPoint && d16.allocationBytes == 1536,
        "actual D16 source retains its exact format, precision and bounded byte count");
    check(cache.release(), "D16 allocation releases normally");
    f->glBindFramebuffer(GL_FRAMEBUFFER, surface.defaultFramebufferObject());
    f->glDeleteFramebuffers(1, &framebuffer);
    f->glBindRenderbuffer(GL_RENDERBUFFER, 0);
    f->glDeleteRenderbuffers(1, &depth);
    check(f->glGetError() == GL_NO_ERROR, "refusal and explicit-format boundaries leave no GL error");
}

void shareGroupSurvivorCase()
{
    auto owner = std::make_unique<Surface>(0);
    check(owner->expose(), "share survivor source initializes");
    auto* f = functions();
    if ( !f )
    {
        return;
    }
    const Source source = describeSource(*owner, f);
    sed::render::DepthSnapshotCache cache;
    const auto captured = cache.capture(owner->context(), owner->defaultFramebufferObject(), source.extent);
    check(captured.valid, "share survivor captures source texture");
    QOpenGLContext survivor;
    survivor.setFormat(owner->context()->format());
    survivor.setShareContext(owner->context());
    check(survivor.create(), "public sharing context creates before source destruction");
    QOffscreenSurface target;
    target.setFormat(survivor.format());
    target.create();
    check(target.isValid() && QOpenGLContext::areSharing(owner->context(), &survivor),
        "sharing fixture owns a valid surface and same actual group");
    owner.reset();
    check(survivor.makeCurrent(&target), "surviving shared context becomes current");
    f = functions();
    if ( !f )
    {
        return;
    }
    check(!cache.current().valid && f->glIsTexture(captured.texture) == GL_TRUE,
        "owner destruction invalidates the frame but shared allocation remains live");
    const State before(f);
    check(cache.release() && f->glIsTexture(captured.texture) == GL_FALSE && State(f) == before,
        "release through surviving group uses its live functions and deletes the original allocation");
    check(f->glGetError() == GL_NO_ERROR, "sharing-owner destruction has no stale-function-table GL error");
    survivor.doneCurrent();
}

} // namespace

int main(int argc, char** argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    captureCases(0);
    captureCases(4);
    lifecycleCases();
    refusalAndFormatCases();
    shareGroupSurvivorCase();
    std::cout << "Depth snapshot failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
