#pragma once

#include "../module.h"

#include <atomic>
#include <cstdint>

// Animations — the first-person sword-blocking pose, replaced.
//
// WHERE THE WORK HAPPENS
// ----------------------
// Not in onRender(). The block pose is built inside Minecraft's own render pass, in
// ItemRenderer.doBlockTransformations(), which runs between transformFirstPersonItem() and
// renderItem() and is four GL calls long:
//
//     GlStateManager.translate(-0.5F, 0.2F, 0.0F);
//     GlStateManager.rotate( 30.0F, 0.0F, 1.0F, 0.0F);
//     GlStateManager.rotate(-80.0F, 1.0F, 0.0F, 0.0F);
//     GlStateManager.rotate( 60.0F, 0.0F, 1.0F, 0.0F);
//
// GlStateManager does not cache matrix operations in 1.8 -- translate() and rotate() call
// glTranslatef and glRotatef straight through -- so those four calls arrive in opengl32.dll
// exactly as written. This module hooks glTranslatef and glRotatef, recognises that four-call
// sequence by its literal arguments, swallows it, and emits its own pose in its place.
//
// That is the whole mechanism. No bytecode transform, no Java hook class, no new native
// registration: the four constants ARE the signature, and nothing else in the client emits
// translate(-0.5, 0.2, 0) followed by those three rotations. A partial match replays whatever was
// swallowed and lets it through, so a false start costs nothing.
//
// WHY THE DETOUR DOES NO WORK
// ---------------------------
// glTranslatef and glRotatef are called thousands of times a frame, by every entity, every tile
// entity and every GUI element. The fast path out of both detours is one comparison. Everything
// the pose needs -- the animation's numbers, the sway signal, the raise clock -- is computed
// somewhere else and read as plain floats: no JNI, no allocation, no lock, nothing that can
// block the render thread.
//
// THE LIBRARY
// -----------
// Each entry is a full replacement pose, emitted in vanilla's own axis order (Y, X, Y) plus a Z
// roll and a uniform scale, so entry 0 reproduces vanilla bit for bit and every other entry is a
// deliberate departure from it. Some are static; the rest are driven by one of three signals:
//   raise  seconds since the sword came up, for animations that move into place
//   clock  a free-running second count, for the ones that never stop moving
//   sway   smoothed yaw velocity, for the ones that trail the mouse
class Animations : public Module {
public:
    Animations();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        int animation = 0;
        float scale = 1.0f;       // multiplies the pose's own scale
        float posX = 0.0f;        // added to the pose translation, in the same units
        float posY = 0.0f;
        float posZ = 0.0f;
        float speed = 1.0f;       // time multiplier for the animated entries
    } settings;

    // Number of entries in the library, for the menu combo.
    static int animationCount();
    static const char* animationName(int index);

    // False for the pose-only entries. Speed has nothing to scale on those, so the menu greys the
    // slider out rather than leaving a live control wired to nothing.
    static bool animationMoves(int index);

    // Read by the detour, on the render thread, once per block frame. Relaxed on purpose: a frame
    // of staleness in a sway angle is not worth the fence.
    float swayDegPerSec() const { return sway_.load(std::memory_order_relaxed); }
    float swingSeconds() const { return swingPulse_.load(std::memory_order_relaxed); }

private:
    // Installs the glTranslatef/glRotatef detours. Idempotent: the second and later calls are a
    // no-op, and a failure to resolve either symbol leaves the module inert rather than half hooked.
    static void ensureGlHooks();

    // Published for the detour to read. Plain relaxed atomics: the detour runs on the render
    // thread, these are written on the module thread, and a frame of staleness in a sway angle is
    // not worth a lock. sway_ is degrees per second; swingPulse_ is seconds since the last swing.
    std::atomic<float> sway_{0.0f};
    std::atomic<float> swingPulse_{9.0f};

    // Module-thread sampling state.
    float lastYaw_ = 0.0f;
    bool haveYaw_ = false;
    std::uint64_t lastSampleMs_ = 0;
    bool prevClick_ = false;
    std::uint64_t swingAtMs_ = 0;
};

extern Animations* animations;
