#pragma once

#include "../module.h"

class Chams : public Module {
public:
    Chams();

    void onRender() override;   // 2D pass (unused — GL chams replaces it)
    void onRender3D();          // GL model chams, called from the world render hook
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Fill paints a solid flat-color model over the player. See Player keeps the
    // real textured model (their skin) visible, rendered through walls.
    enum Mode { MODE_FILL = 0, MODE_SEE_PLAYER = 1 };

    struct Settings {
        bool drawInvisiblePlayers = false;
        float color[4] = {0.0f, 1.0f, 0.0f, 0.85f};
        float range = 128.0f;
        int mode = MODE_FILL;
    } settings;
};

extern Chams* chams;
