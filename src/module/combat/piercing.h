#pragma once

#include "../../jni/java.h"
#include "../module.h"

#include <atomic>
#include <cstdint>

class Piercing : public Module {
public:
    Piercing();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;
    bool handleAttackInput();
    bool shouldSendOutgoingPacket(JNIEnv* env, jobject packet);

    struct Settings {
        float distance = 3.0f;
        bool weaponsOnly = false;
        bool targetInvisibles = false;
        bool ignoreFriends = true;
        bool ignoreTeams = true;
        bool throughWalls = true;
        float throughWallsRange = 3.0f;
        bool backtrackCheck = true;
        float expand = 0.0f;
    } settings;

private:
    static std::uint64_t nowMs();
    jobject selectTarget(bool hiddenOnly, bool& targetVisible);

    static bool rayIntersectsBox(double originX, double originY, double originZ,
                                 double directionX, double directionY, double directionZ,
                                 double minX, double minY, double minZ,
                                 double maxX, double maxY, double maxZ,
                                 double maxDistance, double& hitDistance);

    std::atomic<bool> manualAttackActive_{false};
    std::atomic<std::uint64_t> suppressDuplicatePacketsUntilMs_{0};
    std::atomic<std::uint64_t> lastAttackMs_{0};
};

extern Piercing* piercing;
