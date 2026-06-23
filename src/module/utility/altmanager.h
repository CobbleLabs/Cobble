#pragma once

#include "../module.h"

class AltManager : public Module {
public:
    AltManager();

    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    static constexpr int MAX_NAME_LEN = 16;

    struct Settings {
        char spoofedName[MAX_NAME_LEN + 1] = "Player";
    } settings;

    bool isActive() const { return enabled && validateName(settings.spoofedName); }

    static bool validateName(const char* name);
};

extern AltManager* altManager;
