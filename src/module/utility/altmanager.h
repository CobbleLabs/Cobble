#pragma once

#include <vector>
#include <string>
#include "../module.h"

class AltManager : public Module {
public:
    AltManager();

    void onEnable() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    static constexpr int MAX_NAME_LEN = 16;
    static constexpr size_t MAX_HISTORY_LEN = 30;

    struct Settings {
        char spoofedName[MAX_NAME_LEN + 1] = "Player";
    } settings;

    std::vector<std::string> nameHistory;

    void addToHistory(const std::string& name);

    bool isActive() const { return enabled && validateName(settings.spoofedName); }

    static bool validateName(const char* name);
};

extern AltManager* altManager;
