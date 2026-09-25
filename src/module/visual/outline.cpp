#include "outline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"

#include <Windows.h>
#include <gl/GL.h>

Outline* outline = nullptr;

namespace {

// GL 1.5+/2.0 names. Minecraft ships a 1.x context, so everything past fixed-function has to
// come through wglGetProcAddress; gl/GL.h on Windows stops at 1.1.
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kDrawFramebuffer = 0x8CA9;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kTexture1 = 0x84C1;
constexpr GLenum kDrawFramebufferBinding = 0x8CA6;
constexpr GLenum kReadFramebufferBinding = 0x8CAA;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kActiveTextureQuery = 0x84E0;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kClampToEdge = 0x812F;

#define OUTLINE_GL_FUNCTIONS(X)                                       \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                      \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))             \
    X(void, BindFramebuffer, (GLenum, GLuint))                        \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum))                       \
    X(GLuint, CreateShader, (GLenum))                                 \
    X(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*)) \
    X(void, CompileShader, (GLuint))                                  \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                    \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*))     \
    X(void, DeleteShader, (GLuint))                                   \
    X(GLuint, CreateProgram, ())                                      \
    X(void, AttachShader, (GLuint, GLuint))                           \
    X(void, LinkProgram, (GLuint))                                    \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                   \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*))    \
    X(void, DeleteProgram, (GLuint))                                  \
    X(void, UseProgram, (GLuint))                                     \
    X(GLint, GetUniformLocation, (GLuint, const char*))               \
    X(void, Uniform1i, (GLint, GLint))                                \
    X(void, Uniform1f, (GLint, GLfloat))                              \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                     \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))   \
    X(void, ActiveTexture, (GLenum))

#define OUTLINE_DECLARE(ret, name, args) ret(APIENTRY* gl##name) args = nullptr;
OUTLINE_GL_FUNCTIONS(OUTLINE_DECLARE)
#undef OUTLINE_DECLARE

bool loadGL() {
    // wglGetProcAddress returns 1/2/3/-1 as failure values on some drivers, not just null.
#define OUTLINE_LOAD(ret, name, args)                                              \
    {                                                                              \
        const PROC proc = wglGetProcAddress("gl" #name);                            \
        const auto raw = reinterpret_cast<std::intptr_t>(proc);                     \
        if (raw == 0 || raw == 1 || raw == 2 || raw == 3 || raw == -1)               \
            return false;                                                           \
        gl##name = reinterpret_cast<decltype(gl##name)>(proc);                       \
    }
    OUTLINE_GL_FUNCTIONS(OUTLINE_LOAD)
#undef OUTLINE_LOAD
    return true;
}
#undef OUTLINE_GL_FUNCTIONS

// Minecraft keeps drawing the frame after every one of these passes returns, so each of them
// has to hand the context back untouched. The attribute stack covers the enables, the buffer
// state and the viewport; the framebuffer binding, the bound program and the active texture
// unit belong to no attribute group and are saved by hand.
//
// No matrix state is listed because no pass here changes one: the mask draws with whatever
// transform the entity pass has set, and the effect quad is already in clip space.
constexpr GLbitfield kGuardedAttribs =
    GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_VIEWPORT_BIT | GL_TEXTURE_BIT;

struct GLStateGuard {
    GLint drawFbo = 0, readFbo = 0, program = 0, textureUnit = 0;

    GLStateGuard() {
        glGetIntegerv(kDrawFramebufferBinding, &drawFbo);
        glGetIntegerv(kReadFramebufferBinding, &readFbo);
        glGetIntegerv(kCurrentProgram, &program);
        glGetIntegerv(kActiveTextureQuery, &textureUnit);
        glPushAttrib(kGuardedAttribs);
    }

    ~GLStateGuard() {
        glPopAttrib();
        glUseProgram((GLuint)program);
        glActiveTexture((GLenum)textureUnit);
        glBindFramebuffer(kDrawFramebuffer, (GLuint)drawFbo);
        glBindFramebuffer(kReadFramebuffer, (GLuint)readFbo);
    }
};

GLuint compileShader(GLenum kind, const char* source) {
    const GLuint shader = glCreateShader(kind);
    if (!shader)
        return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, kCompileStatus, &ok);
    if (!ok) {
        char message[512]{};
        glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
        log_warn("[Outline] shader compile failed: %s", message);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(const char* vertex, const char* fragment) {
    const GLuint vs = compileShader(kVertexShader, vertex);
    const GLuint fs = compileShader(kFragmentShader, fragment);
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, kLinkStatus, &ok);
        if (!ok) {
            char message[512]{};
            glGetProgramInfoLog(program, sizeof(message), nullptr, message);
            log_warn("[Outline] shader link failed: %s", message);
            glDeleteProgram(program);
            program = 0;
        }
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// Coverage-only program for the silhouette pass.
//
// This is what makes the mask a shape rather than a picture: every fragment comes out white no
// matter what texture or colour the model's own draw calls set. Minecraft renders entities
// through fixed-function and binds no program of its own, so this holds for the whole replay,
// and the model geometry still lands where it belongs because the entity transform is already
// on the matrix stack when the hook fires.
constexpr const char* kMaskVertex = "#version 120\nvoid main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;}";
constexpr const char* kMaskFragment = "#version 120\nvoid main(){gl_FragColor=vec4(1.0);}";

constexpr const char* kEffectVertex =
    "#version 120\nvoid main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}";

// Separable outline, run twice over the silhouette.
//
// A rim is a dilation minus the original shape. Dilating a 2D mask by a radius costs
// radius^2 samples done directly, but the max filter is separable, so two 1D passes give the
// same result for 2*radius samples. Pass 0 sweeps horizontally, pass 1 sweeps vertically over
// pass 0's output and finishes the job: subtract the untouched silhouette so only the ring
// outside the body survives, then tint it.
//
// Both terms ride along in separate channels to avoid a third pass. R carries the hard
// dilation that becomes the rim; G carries a Gaussian-weighted version of the same coverage,
// which is the halo when glow is on. The weights are recomputed rather than normalised from
// pass 0, so a wide glow does not wash out.
constexpr const char* kEffectFragment = R"GLSL(
#version 120
uniform sampler2D sweepTex;
uniform sampler2D silhouetteTex;
uniform vec2 sweepStep;
uniform int finalPass;
uniform float rimPixels;
uniform float glowAmount;
uniform vec4 rimColor;

void main() {
    vec2 uv = gl_TexCoord[0].xy;

    float sigma = 1.0 + glowAmount * 3.0;
    float glowReach = glowAmount > 0.0 ? ceil(sigma * 3.0) : 0.0;
    float reach = max(ceil(rimPixels), glowReach);

    float dilated = 0.0;
    float glowSum = 0.0;
    float glowWeight = 0.0;

    for (int i = -16; i <= 16; ++i) {
        float offset = abs(float(i));
        if (offset > reach)
            continue;

        vec2 tap = texture2D(sweepTex, uv + sweepStep * float(i)).rg;

        // Feathered max: a tap one pixel past the rim width contributes nothing, so the
        // edge lands on a fractional width instead of snapping to whole pixels.
        dilated = max(dilated, tap.r * clamp(rimPixels + 1.0 - offset, 0.0, 1.0));

        if (glowAmount > 0.0 && offset <= glowReach) {
            float weight = exp(-(offset * offset) / (2.0 * sigma * sigma));
            glowSum += (finalPass == 0 ? tap.r : tap.g) * weight;
            glowWeight += weight;
        }
    }

    float glow = glowWeight > 0.0 ? glowSum / glowWeight : 0.0;

    if (finalPass == 0) {
        gl_FragColor = vec4(dilated, glow, 0.0, 1.0);
        return;
    }

    // Everything inside the body is masked out: the rim belongs outside the silhouette, and
    // the halo has to fade behind it rather than tint the player.
    float body = texture2D(silhouetteTex, uv).r;
    float outside = 1.0 - body;
    float rim = max(dilated - body, 0.0);
    float halo = glow * glowAmount * 2.5;
    float alpha = max(rim, halo * outside) * outside * rimColor.a;
    gl_FragColor = vec4(rimColor.rgb, clamp(alpha, 0.0, 1.0));
}
)GLSL";

// Coverage and nothing else. Depth off so a body behind a wall still registers and two
// overlapping players merge into one silhouette; blending and alpha test off so the shader's
// output lands verbatim.
void applyMaskState(int width, int height) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, width, height);
}

float bounded(float value, float lo, float hi, float fallback) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

}  // namespace

Outline::Outline() : Module("Outline") {
    keybind = 0;
    renders = true;
    usesPlayerCache = true;
}

void Outline::destroyTargets() {
    if (fbo_)
        glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(3, textures_);
    fbo_ = 0;
    for (auto& texture : textures_)
        texture = 0;
    width_ = height_ = 0;
    armed_ = captured_ = false;
}

void Outline::destroyResources() {
    destroyTargets();
    if (maskProgram_)
        glDeleteProgram(maskProgram_);
    if (effectProgram_)
        glDeleteProgram(effectProgram_);
    maskProgram_ = effectProgram_ = 0;
}

bool Outline::prepare(int width, int height) {
    HGLRC current = wglGetCurrentContext();
    if (!current)
        return false;

    if (glOwner_ != current) {
        // A new context invalidates every name we hold. Abandon them rather than deleting:
        // the objects died with the old context and these ids mean something else now.
        glOwner_ = current;
        fbo_ = maskProgram_ = effectProgram_ = 0;
        for (auto& texture : textures_)
            texture = 0;
        width_ = height_ = 0;
        glFailed_ = armed_ = captured_ = false;
        if (!loadGL()) {
            glFailed_ = true;
            log_warn("[Outline] required OpenGL functions unavailable; effect disabled");
        }
    }
    if (glFailed_)
        return false;
    if (fbo_ && width_ == width && height_ == height)
        return true;

    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    if (width <= 0 || height <= 0 || width > maxTexture || height > maxTexture)
        return false;

    GLStateGuard guard;
    destroyTargets();

    if (!maskProgram_)
        maskProgram_ = linkProgram(kMaskVertex, kMaskFragment);
    if (!effectProgram_)
        effectProgram_ = linkProgram(kEffectVertex, kEffectFragment);
    if (!maskProgram_ || !effectProgram_) {
        destroyResources();
        glFailed_ = true;
        return false;
    }

    glGenFramebuffers(1, &fbo_);
    glGenTextures(3, textures_);
    if (!fbo_) {
        destroyResources();
        glFailed_ = true;
        return false;
    }

    glActiveTexture(kTexture0);
    glBindFramebuffer(kFramebuffer, fbo_);
    glDrawBuffer(kColorAttachment0);
    glReadBuffer(kColorAttachment0);
    for (GLuint texture : textures_) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, texture, 0);
        if (!texture || glCheckFramebufferStatus(kFramebuffer) != kFramebufferComplete) {
            log_warn("[Outline] incomplete framebuffer at %dx%d; effect disabled", width, height);
            destroyResources();
            glFailed_ = true;
            return false;
        }
    }

    uSource_ = glGetUniformLocation(effectProgram_, "sweepTex");
    uMask_ = glGetUniformLocation(effectProgram_, "silhouetteTex");
    uStep_ = glGetUniformLocation(effectProgram_, "sweepStep");
    uPass_ = glGetUniformLocation(effectProgram_, "finalPass");
    uOuter_ = glGetUniformLocation(effectProgram_, "rimPixels");
    uGlow_ = glGetUniformLocation(effectProgram_, "glowAmount");
    uColor_ = glGetUniformLocation(effectProgram_, "rimColor");

    width_ = width;
    height_ = height;
    log_ok("[Outline] render targets ready (%dx%d)", width, height);
    return true;
}

bool Outline::isWorldPass() const {
    // The mask has to line up with the frame the player sees, so only the full-screen
    // perspective draw qualifies. A GUI entity preview is orthographic and a shader pack has a
    // program of its own; capturing either would smear the rim over unrelated geometry.
    GLint viewport[4]{};
    GLint currentProgram = 0;
    GLdouble projection[16]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(kCurrentProgram, &currentProgram);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != width_ || viewport[3] != height_)
        return false;
    return std::abs(projection[15]) <= 0.001 && currentProgram == 0;
}

void Outline::onRender3D() {
    armed_ = false;
    if (!enabled || glFailed_ || !fbo_ || !mc.isValid())
        return;
    if (glOwner_ != wglGetCurrentContext() || !isWorldPass())
        return;

    const auto snap = VisualCache::GetSnapshot();
    if (!snap.valid)
        return;

    const float partial = std::clamp(snap.partialTicks, 0.0f, 1.0f);
    localX_ = snap.localLastX + (snap.localX - snap.localLastX) * partial;
    localY_ = snap.localLastY + (snap.localY - snap.localLastY) * partial;
    localZ_ = snap.localLastZ + (snap.localZ - snap.localLastZ) * partial;

    GLStateGuard guard;
    glBindFramebuffer(kFramebuffer, fbo_);
    glFramebufferTexture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, textures_[0], 0);
    applyMaskState(width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    armed_ = true;
}

bool Outline::maskActive() const {
    return enabled && armed_ && !glFailed_ && fbo_ != 0;
}

bool Outline::maskWantsEntity(double x, double y, double z, bool invisible) const {
    if (invisible && !settings.drawInvisiblePlayers)
        return false;
    const double dx = x - localX_;
    const double dy = y - localY_;
    const double dz = z - localZ_;
    return dx * dx + dy * dy + dz * dz <= static_cast<double>(settings.range) * settings.range;
}

void Outline::captureModel(void (*draw)(void*), void* context) {
    if (!maskActive() || glOwner_ != wglGetCurrentContext() || !isWorldPass())
        return;

    GLStateGuard guard;
    glBindFramebuffer(kFramebuffer, fbo_);
    applyMaskState(width_, height_);
    glUseProgram(maskProgram_);
    draw(context);
    captured_ = true;
}

void Outline::runEffect() {
    GLStateGuard guard;
    glBindFramebuffer(kFramebuffer, fbo_);
    applyMaskState(width_, height_);
    glUseProgram(effectProgram_);
    glUniform1i(uSource_, 0);
    glUniform1i(uMask_, 1);
    glUniform1f(uOuter_, bounded(settings.thickness, 0.5f, 6.0f, 2.0f));
    glUniform1f(uGlow_, settings.glow ? bounded(settings.glowStrength, 0.0f, 1.0f, 0.5f) : 0.0f);
    glUniform4f(uColor_, bounded(settings.color[0], 0.0f, 1.0f, 1.0f), bounded(settings.color[1], 0.0f, 1.0f, 1.0f),
                bounded(settings.color[2], 0.0f, 1.0f, 1.0f), bounded(settings.color[3], 0.0f, 1.0f, 1.0f));

    // Unit 1 holds the untouched body mask for the whole run: pass 1 needs it to subtract the
    // interior and leave only the rim.
    glActiveTexture(kTexture1);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glActiveTexture(kTexture0);

    for (int pass = 0; pass < 2; ++pass) {
        glFramebufferTexture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, textures_[pass + 1], 0);
        glBindTexture(GL_TEXTURE_2D, textures_[pass]);
        glUniform1i(uPass_, pass);
        glUniform2f(uStep_, pass == 0 ? 1.0f / width_ : 0.0f, pass == 1 ? 1.0f / height_ : 0.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2f(1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2f(-1.0f, 1.0f);
        glEnd();
    }
}

void Outline::onRender() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    // Build the targets even while disabled, so toggling never has to compile shaders or
    // allocate full-screen textures mid-frame.
    if (!prepare((int)display.x, (int)display.y))
        return;
    if (!enabled || !captured_) {
        captured_ = false;
        return;
    }

    runEffect();
    captured_ = false;

    // V flipped: GL textures start at the bottom row, ImGui expects the top.
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (draw)
        draw->AddImage((ImTextureID)(std::intptr_t)textures_[2], ImVec2(0.0f, 0.0f), display, ImVec2(0.0f, 1.0f),
                       ImVec2(1.0f, 0.0f));
}

void Outline::onWorldChange() {
    armed_ = captured_ = false;
}

void Outline::releaseGL() {
    if (glOwner_ && glOwner_ == wglGetCurrentContext())
        destroyResources();
    glOwner_ = nullptr;
    fbo_ = maskProgram_ = effectProgram_ = 0;
    for (auto& texture : textures_)
        texture = 0;
    width_ = height_ = 0;
    armed_ = captured_ = false;
    glFailed_ = false;
}

void Outline::onRenderMenu() {
    bool enabledValue = enabled;

    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ui::SliderFloat("Thickness", &settings.thickness, 0.5f, 6.0f, "%.1fpx");
            ui::Checkbox("Glow", &settings.glow);
            if (settings.glow)
                ui::SliderFloat("Glow Strength", &settings.glowStrength, 0.0f, 1.0f, "%.2f");
            ui::Checkbox("Draw Invisibles", &settings.drawInvisiblePlayers);
            ui::SliderFloat("Range", &settings.range, 16.0f, 255.0f, "%.0fm");
        },
        "Appearance",
        [this]() {
            ui::color("Color", settings.color, false);
            ui::SliderFloat("Opacity", &settings.color[3], 0.05f, 1.0f, "%.2f");
        });
}

void Outline::saveSettings(json::Value& node) const {
    node["drawInvisiblePlayers"] = settings.drawInvisiblePlayers;
    node["thickness"] = settings.thickness;
    node["glow"] = settings.glow;
    node["glowStrength"] = settings.glowStrength;
    node["range"] = settings.range;
    json::writeColor(node, "color", settings.color);
}

void Outline::loadSettings(const json::Value& node) {
    settings.drawInvisiblePlayers = node["drawInvisiblePlayers"].as_bool(settings.drawInvisiblePlayers);
    settings.thickness = std::clamp(node["thickness"].as_float(settings.thickness), 0.5f, 6.0f);
    settings.glow = node["glow"].as_bool(settings.glow);
    settings.glowStrength = std::clamp(node["glowStrength"].as_float(settings.glowStrength), 0.0f, 1.0f);
    settings.range = std::clamp(node["range"].as_float(settings.range), 16.0f, 255.0f);
    json::readColor(node, "color", settings.color);
    settings.color[3] = std::clamp(settings.color[3], 0.05f, 1.0f);
}
