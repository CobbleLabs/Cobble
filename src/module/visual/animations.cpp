#include "animations.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gl/GL.h>
#pragma comment(lib, "opengl32.lib")

#include "../../../infra/ext/minhook/minhook.h"

#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>

Animations* animations = nullptr;

namespace {

// One pose, emitted in vanilla's own axis order: translate, rotate about Y, rotate about X,
// rotate about Y again, then a roll about Z and a uniform scale that vanilla does not have.
// The defaults ARE doBlockTransformations(), so entry 0 puts back exactly what was swallowed.
struct Pose {
    float tx = -0.5f, ty = 0.2f, tz = 0.0f;
    float y1 = 30.0f;
    float x1 = -80.0f;
    float y2 = 60.0f;
    float z1 = 0.0f;
    float scale = 1.0f;
};

// What moves an entry. Static ones are a fixed pose and nothing else.
enum : std::uint8_t { kStatic = 0, kRaise, kClock, kSway, kSwing };

struct Entry {
    const char* name;
    Pose pose;
    std::uint8_t drive;
};

// clang-format off
// constexpr, not const: the static_asserts below read these names at compile time.
constexpr Entry kLibrary[] = {
    {"Vanilla",     {-0.50f,  0.20f,  0.00f,  30.0f, -80.0f,  60.0f,   0.0f, 1.00f}, kStatic},
    {"Classic 1.7", {-0.46f,  0.18f, -0.04f,  26.0f, -78.0f,  64.0f,   0.0f, 1.00f}, kStatic},
    {"Push",        {-0.34f,  0.06f,  0.16f,  18.0f, -96.0f,  48.0f,   0.0f, 1.00f}, kStatic},
    {"Pull",        {-0.64f,  0.34f, -0.16f,  42.0f, -70.0f,  72.0f,   0.0f, 1.00f}, kStatic},
    {"Swank",       {-0.62f,  0.12f, -0.04f,   6.0f, -92.0f,  38.0f,   0.0f, 1.06f}, kStatic},
    {"Exhibition",  {-0.42f,  0.10f,  0.06f,  62.0f, -58.0f,  28.0f,   0.0f, 1.00f}, kStatic},
    {"Reverse",     {-0.50f,  0.20f,  0.00f, -28.0f, -82.0f, -58.0f,   0.0f, 1.00f}, kStatic},
    {"Cross",       {-0.16f,  0.16f,  0.02f,  54.0f, -74.0f,  86.0f,   0.0f, 1.00f}, kStatic},
    {"Pierce",      {-0.30f,  0.10f,  0.24f,  10.0f,-102.0f,  18.0f,   8.0f, 1.00f}, kStatic},
    {"Lift",        {-0.50f,  0.44f, -0.02f,  30.0f, -86.0f,  60.0f,   0.0f, 1.00f}, kStatic},
    {"Lower",       {-0.50f, -0.06f,  0.02f,  30.0f, -72.0f,  60.0f,   0.0f, 1.00f}, kStatic},
    {"Barrel",      {-0.52f,  0.18f,  0.00f,  30.0f, -80.0f,  60.0f,  26.0f, 1.00f}, kStatic},
    {"Stiff",       {-0.50f,  0.20f,  0.00f,  30.0f, -80.0f,  60.0f,   0.0f, 0.92f}, kStatic},
    {"Compact",     {-0.44f,  0.14f, -0.08f,  34.0f, -84.0f,  56.0f,   0.0f, 0.82f}, kStatic},
    {"Wide",        {-0.66f,  0.24f,  0.04f,  24.0f, -76.0f,  66.0f,   0.0f, 1.18f}, kStatic},
    {"Flat",        {-0.48f,  0.22f,  0.00f,  30.0f, -46.0f,  60.0f,  -6.0f, 1.00f}, kStatic},
    {"Guard",       {-0.38f,  0.28f,  0.10f,  46.0f, -66.0f,  74.0f,  12.0f, 1.04f}, kStatic},
    // Driven entries. Their numbers are the pose they SETTLE on, and every one of them differs
    // from vanilla on its own: an animation whose rest pose is vanilla's is indistinguishable
    // from Vanilla the moment it stops moving, which is most of the time.
    {"Slide",       {-0.46f,  0.20f,  0.02f,  34.0f, -78.0f,  58.0f,   0.0f, 1.00f}, kRaise},
    {"Snap",        {-0.50f,  0.22f,  0.00f,  30.0f, -84.0f,  62.0f,   0.0f, 1.00f}, kRaise},
    {"Rise",        {-0.52f,  0.26f, -0.02f,  28.0f, -82.0f,  60.0f,   0.0f, 1.00f}, kRaise},
    {"Unfold",      {-0.48f,  0.20f,  0.00f,  30.0f, -80.0f,  66.0f,  -9.0f, 1.00f}, kRaise},
    {"Jello",       {-0.48f,  0.18f,  0.02f,  32.0f, -82.0f,  58.0f,   0.0f, 1.00f}, kClock},
    {"Spin",        {-0.50f,  0.20f,  0.00f,  30.0f, -80.0f,   0.0f,   0.0f, 1.00f}, kClock},
    {"Hover",       {-0.50f,  0.22f,  0.00f,  30.0f, -80.0f,  60.0f,   0.0f, 1.00f}, kClock},
    {"Breathe",     {-0.50f,  0.20f,  0.02f,  30.0f, -80.0f,  60.0f,   0.0f, 1.00f}, kClock},
    {"Pendulum",    {-0.50f,  0.22f,  0.00f,  30.0f, -80.0f,  60.0f,   0.0f, 1.00f}, kClock},
    {"Sway",        {-0.50f,  0.20f,  0.00f,  30.0f, -80.0f,  60.0f,   0.0f, 1.00f}, kSway},
    {"Drag",        {-0.56f,  0.20f, -0.04f,  30.0f, -80.0f,  62.0f,   0.0f, 1.00f}, kSway},
    {"Tap",         {-0.50f,  0.22f,  0.00f,  30.0f, -78.0f,  60.0f,   0.0f, 1.00f}, kSwing},
    {"Thrust",      {-0.44f,  0.16f,  0.06f,  22.0f, -88.0f,  52.0f,   0.0f, 1.00f}, kSwing},
    {"Recoil",      {-0.52f,  0.20f,  0.00f,  30.0f, -80.0f,  60.0f,   8.0f, 1.00f}, kSwing},
};
// clang-format on

constexpr int kLibrarySize = static_cast<int>(sizeof(kLibrary) / sizeof(kLibrary[0]));

// Indices of the entries whose motion is written by hand below. Only the driven ones need naming;
// the static entries are their pose and nothing else, so nothing ever asks for them by index.
// The static_asserts are the whole point: they make a reorder of the table a compile error rather
// than a Spin that hovers.
enum : int {
    kSlide = 17, kSnap, kRise, kUnfold,
    kJello, kSpin, kHover, kBreathe, kPendulum,
    kSway_, kDrag,
    kTap, kThrust, kRecoil,
};

constexpr bool sameName(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static_assert(sameName(kLibrary[kSlide].name, "Slide"), "library reordered");
static_assert(sameName(kLibrary[kSnap].name, "Snap"), "library reordered");
static_assert(sameName(kLibrary[kRise].name, "Rise"), "library reordered");
static_assert(sameName(kLibrary[kUnfold].name, "Unfold"), "library reordered");
static_assert(sameName(kLibrary[kJello].name, "Jello"), "library reordered");
static_assert(sameName(kLibrary[kSpin].name, "Spin"), "library reordered");
static_assert(sameName(kLibrary[kHover].name, "Hover"), "library reordered");
static_assert(sameName(kLibrary[kBreathe].name, "Breathe"), "library reordered");
static_assert(sameName(kLibrary[kPendulum].name, "Pendulum"), "library reordered");
static_assert(sameName(kLibrary[kSway_].name, "Sway"), "library reordered");
static_assert(sameName(kLibrary[kDrag].name, "Drag"), "library reordered");
static_assert(sameName(kLibrary[kTap].name, "Tap"), "library reordered");
static_assert(sameName(kLibrary[kThrust].name, "Thrust"), "library reordered");
static_assert(sameName(kLibrary[kRecoil].name, "Recoil"), "library reordered");
static_assert(kRecoil == kLibrarySize - 1, "library reordered");

// Names, in library order, for ui::Combo.
const char* kNames[kLibrarySize] = {};
bool kNamesReady = false;

void buildNames() {
    if (kNamesReady)
        return;
    for (int i = 0; i < kLibrarySize; ++i)
        kNames[i] = kLibrary[i].name;
    kNamesReady = true;
}

}  // namespace

int Animations::animationCount() {
    return kLibrarySize;
}

const char* Animations::animationName(int index) {
    if (index < 0 || index >= kLibrarySize)
        return "Vanilla";
    return kLibrary[index].name;
}

bool Animations::animationMoves(int index) {
    if (index < 0 || index >= kLibrarySize)
        return false;
    return kLibrary[index].drive != kStatic;
}

// ---------------------------------------------------------------------------
// The GL layer. Everything below runs on the render thread, inside Minecraft's own draw.
// ---------------------------------------------------------------------------

namespace {

typedef void(APIENTRY* glTranslatef_t)(GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* glRotatef_t)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* glScalef_t)(GLfloat, GLfloat, GLfloat);

glTranslatef_t o_glTranslatef = nullptr;
glRotatef_t o_glRotatef = nullptr;
glScalef_t p_glScalef = nullptr;  // resolved, never hooked: nothing needs to see our scale

// Set from onEnable/onDisable, read by the translate detour and nowhere else: with it clear, the
// signature never starts, so both detours reduce to the passthrough they already are for every
// other translate and rotate in the frame.
bool g_active = false;

// Both detours are in and both trampolines are live. Nothing is ever half hooked: if either symbol
// or either hook fails, this stays false and the module never activates.
bool g_hooked = false;

// How far into the four-call signature we are. Render thread only.
int g_stage = 0;

// Emits the part of vanilla's sequence that was swallowed before a mismatch. The swallowed calls
// are literals, so there is nothing to buffer -- the stage number is the whole record.
void replaySwallowed(int stage) {
    if (stage >= 1)
        o_glTranslatef(-0.5f, 0.2f, 0.0f);
    if (stage >= 2)
        o_glRotatef(30.0f, 0.0f, 1.0f, 0.0f);
    if (stage >= 3)
        o_glRotatef(-80.0f, 1.0f, 0.0f, 0.0f);
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// The frame the signature last matched, and when the current block started. A gap longer than a
// few frames means the sword went down and came back up, which is what the raise-driven entries
// animate against. Measured here rather than taken from BlockHit on purpose: this works for a
// hand on the right mouse button and for a fake block equally.
std::uint64_t g_lastMatchMs = 0;
std::uint64_t g_raiseStartMs = 0;

// Seconds the sword has been up, accumulated from the gaps between matches rather than read off a
// wall clock: the clock-driven entries then start at phase zero on every block, which is the pose
// the entry already has, so nothing pops on the frame the sword comes up. Wraps at an hour, where
// float still has four decimals of a second left.
float g_clock = 0.0f;

// Ease used by every raise-driven entry: fast out of the gate, no overshoot, settled by the end.
float easeOut(float t) {
    t = (std::clamp)(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// The pose for this frame: the library entry, moved by whichever signal drives it, then offset and
// scaled by the four settings.
//
// Every driven entry moves for as long as the sword is up, not just for the quarter second after
// it comes up. The first cut gave the raise-driven four a 220 ms intro and nothing after it, and
// since their rest pose was vanilla's they were pixel-identical to Vanilla for every frame but the
// first few -- which is exactly what "not every animation really works" looked like. The same
// went for Speed: on a pose that never moves there is no time to scale, so on 21 of 31 entries
// the slider was wired to nothing at all.
Pose resolvePose(float raise, float clock, float sway, float swing) {
    const Animations::Settings& s = animations->settings;
    const int idx = (std::clamp)(s.animation, 0, kLibrarySize - 1);
    const Entry& e = kLibrary[idx];
    Pose p = e.pose;
    const float speed = (std::clamp)(s.speed, 0.1f, 3.0f);
    const float c = clock * speed;

    switch (e.drive) {
        case kRaise: {
            // 0.34 s at speed 1, seven ticks: long enough to actually watch, short enough that a
            // blockhit's own raise still finishes inside the hold.
            const float t = easeOut(raise / (0.34f / speed));
            const float g = 1.0f - t;  // how much of the intro is left
            if (idx == kSlide) {         // in from the right edge of the frame
                p.tx = -1.15f + (p.tx + 1.15f) * t;
                p.y2 += g * 34.0f;
            } else if (idx == kSnap) {   // overshoot the pitch, then settle back onto it
                p.x1 += g * -26.0f * std::cos(t * 6.2832f);
                p.ty += g * -0.06f;
            } else if (idx == kRise) {   // up from below the frame
                p.ty = -0.46f + (p.ty + 0.46f) * t;
                p.z1 += g * 16.0f;
            } else {                     // Unfold: opens out of a folded wrist
                p.y2 += g * -78.0f;
                p.z1 += g * 30.0f;
                p.tz += g * -0.10f;
            }
            // And a breath underneath all four, so a settled pose is still a hand and not a
            // tripod -- and so Speed has something to scale once the intro is over.
            p.ty += std::sin(c * 2.1f) * 0.016f;
            p.y1 += std::sin(c * 1.7f + 0.6f) * 2.2f;
            break;
        }
        case kClock: {
            if (idx == kJello) {
                p.y1 += std::sin(c * 9.0f) * 14.0f;
                p.x1 += std::sin(c * 7.0f + 1.0f) * 10.0f;
                p.tz += std::sin(c * 11.0f) * 0.03f;
            } else if (idx == kSpin) {
                p.y2 += std::fmod(c * 200.0f, 360.0f);
            } else if (idx == kHover) {
                p.ty += std::sin(c * 3.1f) * 0.10f;
                p.y2 += std::sin(c * 3.1f + 1.57f) * 7.0f;
            } else if (idx == kBreathe) {
                p.tz += std::sin(c * 2.2f) * 0.10f;
                p.scale += std::sin(c * 2.2f) * 0.06f;
                p.x1 += std::sin(c * 2.2f) * 5.0f;
            } else {                     // Pendulum: swings from the wrist
                p.z1 += std::sin(c * 2.6f) * 26.0f;
                p.tx += std::cos(c * 2.6f) * 0.05f;
            }
            break;
        }
        case kSway: {
            // sway is yaw velocity in degrees per second, smoothed on the module thread. The
            // clamps matter more than the factors: a mouse flick is thousands of degrees a second
            // and without a ceiling the sword would leave the frame.
            const float s2 = sway * speed;
            if (idx == kSway_) {
                p.z1 += (std::clamp)(s2 * 0.10f, -30.0f, 30.0f);
                p.tx += (std::clamp)(s2 * 0.0006f, -0.12f, 0.12f);
            } else {                     // Drag: the blade trails behind the turn
                p.y2 += (std::clamp)(s2 * 0.15f, -38.0f, 38.0f);
                p.tz += (std::clamp)(std::fabs(s2) * 0.0004f, 0.0f, 0.08f);
            }
            // Standing still is a legitimate answer for these two, but a dead-still sword reads
            // as a broken animation, so there is a slow drift under the mouse-driven part.
            p.ty += std::sin(c * 1.9f) * 0.014f;
            break;
        }
        case kSwing: {
            // swing is seconds since the last attack. Out fast, back slow, nothing at rest: the
            // sqrt puts the peak a quarter of the way through the window instead of halfway.
            const float u = (std::clamp)(swing / (0.24f / speed), 0.0f, 1.0f);
            const float k = std::sin(3.14159265f * std::sqrt(u));
            if (idx == kTap) {
                p.ty -= k * 0.13f;
                p.x1 += k * 20.0f;
            } else if (idx == kThrust) {
                p.tz += k * 0.26f;
                p.x1 -= k * 22.0f;
            } else {                     // Recoil: kicks back along the arm
                p.tx -= k * 0.17f;
                p.z1 += k * 26.0f;
            }
            // Idle for the same reason as the sway pair: between swings there is no signal, and
            // a frozen sword is the thing that reads as "this animation does not work".
            p.y1 += std::sin(c * 2.4f) * 2.6f;
            p.ty += std::sin(c * 1.6f + 1.1f) * 0.012f;
            break;
        }
        default:
            break;                       // static: the entry's pose, exactly as written
    }

    // The four settings, last, so they offset whatever the animation produced rather than
    // something the animation then moves away from.
    p.tx += s.posX;
    p.ty += s.posY;
    p.tz += s.posZ;
    p.scale *= (std::clamp)(s.scale, 0.5f, 2.0f);
    return p;
}

// Our pose, in vanilla's axis order. Emitted through the trampolines: our own detours must not see
// these calls, or the first translate would look like the start of another signature.
void emitPose(const Pose& p) {
    o_glTranslatef(p.tx, p.ty, p.tz);
    o_glRotatef(p.y1, 0.0f, 1.0f, 0.0f);
    o_glRotatef(p.x1, 1.0f, 0.0f, 0.0f);
    o_glRotatef(p.y2, 0.0f, 1.0f, 0.0f);
    if (p.z1 != 0.0f)
        o_glRotatef(p.z1, 0.0f, 0.0f, 1.0f);
    if (p_glScalef && p.scale != 1.0f)
        p_glScalef(p.scale, p.scale, p.scale);
}

// Only the first call of the signature consults g_active, so a signature that has started is always
// finished by the same rules no matter what the module thread does to that flag halfway through.
// g_stage itself is touched by nothing but the render thread.
void APIENTRY hk_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (x == -0.5f && y == 0.2f && z == 0.0f && g_active) {
        if (g_stage != 0)
            replaySwallowed(g_stage);  // a partial match that never completed
        g_stage = 1;
        return;                        // swallowed
    }
    if (g_stage != 0) {
        replaySwallowed(g_stage);
        g_stage = 0;
    }
    o_glTranslatef(x, y, z);
}

void APIENTRY hk_glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) {
    // The fast path for the thousands of rotations a frame that are not ours: one comparison.
    if (g_stage == 0) {
        o_glRotatef(a, x, y, z);
        return;
    }

    const bool ok = g_stage == 1 ? (a == 30.0f && x == 0.0f && y == 1.0f && z == 0.0f)
                  : g_stage == 2 ? (a == -80.0f && x == 1.0f && y == 0.0f && z == 0.0f)
                                 : (a == 60.0f && x == 0.0f && y == 1.0f && z == 0.0f);
    if (!ok) {
        replaySwallowed(g_stage);
        g_stage = 0;
        o_glRotatef(a, x, y, z);
        return;
    }
    if (g_stage < 3) {
        ++g_stage;
        return;
    }
    g_stage = 0;

    // Shutting down with the hooks still in: put vanilla's own block back and stay out of it.
    if (!animations) {
        replaySwallowed(3);
        o_glRotatef(60.0f, 0.0f, 1.0f, 0.0f);
        return;
    }

    // A gap wider than a few frames means the sword went down and came back up. 120 ms rather than
    // one frame's worth because the measurement has to survive a hitch -- it holds down to about
    // eight FPS -- and rather than the quarter second this started at, which was longer than the
    // holes a blockhit leaves, so the raise-driven entries never re-triggered in an actual fight.
    const std::uint64_t t = nowMs();
    if (g_lastMatchMs == 0 || t - g_lastMatchMs > 120) {
        g_raiseStartMs = t;
        g_clock = 0.0f;
    } else {
        g_clock += static_cast<float>(t - g_lastMatchMs) * 0.001f;
        if (g_clock > 3600.0f)
            g_clock -= 3600.0f;
    }
    g_lastMatchMs = t;

    emitPose(resolvePose(static_cast<float>(t - g_raiseStartMs) * 0.001f, g_clock,
                         animations->swayDegPerSec(), animations->swingSeconds()));
}

}  // namespace

// ---------------------------------------------------------------------------
// The module.
// ---------------------------------------------------------------------------

Animations::Animations() : Module("Animations") {
    keybind = 0;
    // Nothing to draw: the pose is emitted inside Minecraft's own render pass, so there is no
    // onRender for the overlay to call and no reason for the manager to keep a frame budget for it.
    renders = false;
}

void Animations::ensureGlHooks() {
    static bool tried = false;
    if (tried)
        return;
    tried = true;

    // MinHook is already up by the time any module can be toggled -- the overlay needs it to hook
    // wglSwapBuffers at all -- but a second Initialize is cheap and says so out loud.
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        log_warn("[Animations] MinHook unavailable (%d); module inert", (int)init);
        return;
    }

    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (!gl) {
        log_warn("[Animations] opengl32.dll not loaded; module inert");
        return;
    }

    LPVOID tTranslate = (LPVOID)GetProcAddress(gl, "glTranslatef");
    LPVOID tRotate = (LPVOID)GetProcAddress(gl, "glRotatef");
    p_glScalef = (glScalef_t)GetProcAddress(gl, "glScalef");
    if (!tTranslate || !tRotate) {
        log_warn("[Animations] glTranslatef/glRotatef not exported; module inert");
        return;
    }

    // Both or neither. A single hook would swallow the translate with nothing to recognise the
    // rotations that follow it, which is the one state this must never be left in.
    if (MH_CreateHook(tTranslate, (LPVOID)hk_glTranslatef, (LPVOID*)&o_glTranslatef) != MH_OK ||
        MH_CreateHook(tRotate, (LPVOID)hk_glRotatef, (LPVOID*)&o_glRotatef) != MH_OK) {
        log_warn("[Animations] GL hook creation failed; module inert");
        return;
    }
    if (MH_EnableHook(tTranslate) != MH_OK || MH_EnableHook(tRotate) != MH_OK) {
        MH_DisableHook(tTranslate);
        MH_DisableHook(tRotate);
        log_warn("[Animations] GL hook enable failed; module inert");
        return;
    }

    g_hooked = true;
    log_ok("[Animations] GL hooks installed (translate/rotate)");
}

void Animations::onEnable() {
    ensureGlHooks();
    g_active = g_hooked;
    haveYaw_ = false;
    lastSampleMs_ = 0;
    sway_.store(0.0f, std::memory_order_relaxed);
    swingPulse_.store(9.0f, std::memory_order_relaxed);
    if (!g_hooked)
        log_warn("[Animations] enabled with no hooks; the vanilla pose stays");
}

void Animations::onDisable() {
    // The hooks stay in. Uninstalling them from the module thread while the render thread is inside
    // one is the only way this module could ever crash the game, and a detour that does nothing but
    // pass the call through costs less than that risk.
    g_active = false;
}

void Animations::onUpdate() {
    const std::uint64_t t = nowMs();

    // Seconds since the last swing. A hand on the left button is read here rather than from a
    // tick hook because a click shorter than a client tick is invisible from one.
    const bool click = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (click && !prevClick_)
        swingAtMs_ = t;
    prevClick_ = click;
    swingPulse_.store(swingAtMs_ == 0 ? 9.0f : static_cast<float>(t - swingAtMs_) * 0.001f,
                      std::memory_order_relaxed);

    // Yaw velocity, in degrees per second. Time-gated rather than smoothed per call: this runs at
    // loop rate, not tick rate, so a per-call weight would fold hundreds of samples a frame into
    // something indistinguishable from zero.
    if (t - lastSampleMs_ < 12)
        return;

    if (!player.isValid()) {
        haveYaw_ = false;
        sway_.store(0.0f, std::memory_order_relaxed);
        lastSampleMs_ = t;
        return;
    }

    const float yaw = player.getYaw();
    if (!haveYaw_) {
        lastYaw_ = yaw;
        haveYaw_ = true;
        lastSampleMs_ = t;
        return;
    }

    float d = yaw - lastYaw_;
    while (d > 180.0f)
        d -= 360.0f;
    while (d < -180.0f)
        d += 360.0f;
    const float dt = static_cast<float>(t - lastSampleMs_) * 0.001f;
    const float vel = dt > 0.0f ? d / dt : 0.0f;
    // Roughly five samples of history, about 60 ms: fast enough to follow a flick, slow enough that
    // the sword does not jitter with the mouse's own sampling noise.
    const float prev = sway_.load(std::memory_order_relaxed);
    sway_.store(prev + (vel - prev) * 0.25f, std::memory_order_relaxed);
    lastYaw_ = yaw;
    lastSampleMs_ = t;
}

void Animations::onRenderMenu() {
    buildNames();
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ImGui::Spacing();
            ui::Combo("Animation", &settings.animation, kNames, kLibrarySize);
            // Greyed out on the pose-only entries: there is no time in a still pose for a time
            // multiplier to scale, and a slider that moves without changing anything is worse
            // than one that says it cannot.
            const bool moves = animationMoves(settings.animation);
            ImGui::BeginDisabled(!moves);
            ui::SliderFloat("Speed", &settings.speed, 0.1f, 3.0f, "%.2f");
            ImGui::EndDisabled();
            if (!moves)
                ImGui::TextDisabled("Still pose - nothing to speed up");
            else if (!g_hooked && enabled)
                ImGui::TextDisabled("No GL hooks - vanilla pose");
        },
        "Placement",
        [this]() {
            ui::SliderFloat("Scale", &settings.scale, 0.5f, 2.0f, "%.2f");

            ImGui::Spacing();
            ui::SliderFloat("Pos X", &settings.posX, -0.5f, 0.5f, "%.2f");
            ui::SliderFloat("Pos Y", &settings.posY, -0.5f, 0.5f, "%.2f");
            ui::SliderFloat("Pos Z", &settings.posZ, -0.5f, 0.5f, "%.2f");
        });
}

std::string Animations::getDisplayInfo() {
    return animationName(settings.animation);
}

void Animations::saveSettings(json::Value& node) const {
    node["animation"] = settings.animation;
    node["scale"] = settings.scale;
    node["posX"] = settings.posX;
    node["posY"] = settings.posY;
    node["posZ"] = settings.posZ;
    node["speed"] = settings.speed;
}

void Animations::loadSettings(const json::Value& node) {
    settings.animation =
        (std::clamp)(node["animation"].as_int(settings.animation), 0, kLibrarySize - 1);
    settings.scale = (std::clamp)(node["scale"].as_float(settings.scale), 0.5f, 2.0f);
    settings.posX = (std::clamp)(node["posX"].as_float(settings.posX), -0.5f, 0.5f);
    settings.posY = (std::clamp)(node["posY"].as_float(settings.posY), -0.5f, 0.5f);
    settings.posZ = (std::clamp)(node["posZ"].as_float(settings.posZ), -0.5f, 0.5f);
    settings.speed = (std::clamp)(node["speed"].as_float(settings.speed), 0.1f, 3.0f);
}


