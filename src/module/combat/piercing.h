#pragma once

#include "../module.h"

class Piercing : public Module {
public:
    Piercing();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;

    struct Settings {
        float distance = 3.0f;
        bool weaponsOnly = false;
        bool targetInvisibles = false;
        bool ignoreFriends = true;
        bool ignoreTeams = true;
    } settings;

private:
    void clearForcedTarget(JNIEnv* env);

    jobject forcedTarget = nullptr;
    jobject forcedMouseOver = nullptr;
    jobject previousHitType = nullptr;
    jobject previousEntityHit = nullptr;

    static bool rayIntersectsBox(double originX, double originY, double originZ,
                                 double directionX, double directionY, double directionZ,
                                 double minX, double minY, double minZ,
                                 double maxX, double maxY, double maxZ,
                                 double maxDistance, double& hitDistance);
};

extern Piercing* piercing;
