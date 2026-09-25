#pragma once

#include "../module.h"

// Screen-space player outline.
//
// The mask holds the player body geometry and nothing else. ModelBiped.render is hooked
// through Cobble's bytecode patcher, and on the outer call the model is re-rendered into an
// offscreen buffer with a coverage-only shader bound. The held item, the drop shadow, fire and
// the nametag are drawn by other parts of the entity pass, so none of them reach the mask. A
// separable two-pass dilation then subtracts the body from its own dilation, leaving the rim.
//
// Three phases, because each one is only correct in one place:
//   onRender3D   - world pass entry: clear the mask and arm it.
//   captureModel - per model, from the Java hook, while the entity transform is live.
//   onRender     - overlay pass: dilate and composite.
class Outline : public Module {
public:
    Outline();

    void onRender3D();
    void onRender() override;
    void onWorldChange() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Cheap gate for the model hook, so it can bail before touching JNI.
    bool maskActive() const;
    bool maskWantsEntity(double x, double y, double z, bool invisible) const;
    // Draws through `draw` into the mask. The callback runs with the mask framebuffer, the
    // coverage shader and the mask GL state bound; everything is restored on return.
    void captureModel(void (*draw)(void*), void* context);

    // Called when the GL context is going away, from the same thread that owns it.
    void releaseGL();

    struct Settings {
        bool drawInvisiblePlayers = false;
        float color[4] = {0.89f, 0.89f, 0.91f, 1.0f};
        float thickness = 2.0f;  // rim width in pixels
        bool glow = false;
        float glowStrength = 0.5f;
        float range = 128.0f;
    } settings;

private:
    // True once the framebuffer and both programs exist at the current screen size.
    bool prepare(int width, int height);
    void destroyTargets();
    void destroyResources();
    void runEffect();
    // Rejects any pass that is not the full-screen perspective world draw, which is what
    // keeps GUI entity previews and shader-pack passes out of the mask.
    bool isWorldPass() const;

    // Owning GL context. Names from a dead context must never be deleted in a live one, so
    // a context change abandons the handles instead of freeing them.
    void* glOwner_ = nullptr;
    unsigned int fbo_ = 0;
    unsigned int textures_[3] = {0, 0, 0};
    unsigned int maskProgram_ = 0;
    unsigned int effectProgram_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool glFailed_ = false;
    // armed_: the mask was cleared for this frame. captured_: something was drawn into it.
    bool armed_ = false;
    bool captured_ = false;
    double localX_ = 0.0;
    double localY_ = 0.0;
    double localZ_ = 0.0;

    int uSource_ = -1;
    int uMask_ = -1;
    int uStep_ = -1;
    int uPass_ = -1;
    int uOuter_ = -1;
    int uGlow_ = -1;
    int uColor_ = -1;
};

extern Outline* outline;
