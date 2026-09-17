#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "../module.h"

struct ActivePing {
    std::string sender;
    int type = 0;  // 0 = Location, 1 = Player Focus
    double x = 0, y = 0, z = 0;
    std::string targetPlayer;
    long long timestamp = 0;
};

class Pings : public Module {
public:
    Pings();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRender3D();
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        int pingKeybind = 0x58;                             // Default: 'X' key
        float pingScale = 1.0f;                             // 0.5 to 3.0 — controls on-screen pin icon size
        float locationColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // Red location pin
        float chamsColor[4] = {0.72f, 0.38f, 1.0f, 1.0f};   // Soft violet outline
    } settings;

private:
    std::mutex pingsMutex;
    std::vector<ActivePing> activePings;
    std::string lastServerIp;
    long long lastFetchTime = 0;
    long long lastPingSentTime = 0;  // Cooldown to prevent double-ping spam
    bool keyWasDown = false;

    void sendPing(int type, double x, double y, double z, const std::string& targetPlayer);
    void fetchPings();
};

extern Pings* pingsModule;
