#pragma once

#include "../module.h"

class Chams : public Module {
public:
    Chams();

    void onRender3D();
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        bool drawInvisiblePlayers = false;
        float color[4] = {0.0f, 1.0f, 0.0f, 0.85f};
        float range = 128.0f;
    } settings;
};

extern Chams* chams;
