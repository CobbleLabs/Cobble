#pragma once

#include "../module.h"

#include <cstdint>
#include <mutex>
#include <vector>

class Clutch : public Module {
public:
    struct Settings {
        int predictionMs = 300;
        int searchRadius = 5;
        float reach = 4.2f;
        int placeDelayMs = 70;
        bool restoreSlot = true;

        // Enhanced Prediction & Clutch settings
        bool showBps = true;
        bool showPath = true;
        bool showPlacement = true;
        bool cameraWeight = true;
        bool smoothRotation = true;
        float rotationSpeed = 20.0f;
        bool silentLook = true;
        bool allowForward = false;
    } settings;

    bool hasSilentRotation = false;
    bool silentRotationSent_ = false;
    float silentYaw = 0.0f;
    float silentPitch = 0.0f;


    Clutch();
    ~Clutch() override;

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    void onOutgoingPacket(JNIEnv* env, jobject packet);

    // True while clutch is actively rewriting the outgoing movement packet's
    // rotation, so other packet observers (e.g. AimAssist) can defer and not
    // corrupt the placement aim.
    bool isRewritingRotation() const { return enabled && settings.silentLook && hasSilentRotation; }

private:
    struct Placement {
        int targetX = 0;
        int targetY = 0;
        int targetZ = 0;
        int supportX = 0;
        int supportY = 0;
        int supportZ = 0;
        double hitX = 0.0;
        double hitY = 0.0;
        double hitZ = 0.0;
        double score = 0.0;
        bool valid = false;
    };

    struct PathPoint {
        double relX = 0.0;
        double relY = 0.0;
        double relZ = 0.0;
        bool hitBlock = false;
    };

    static std::uint64_t nowMs();
    static bool isPlaceableBlock(int id);
    static float wrapDegrees(float angle);
    static float approachAngle(float current, float target, float maxStep);
    int findBlockSlot() const;
    bool isDangerous(double predictedX, double predictedY, double predictedZ) const;
    bool isPlacementUsable(const Placement& placement) const;
    Placement findPlacement(double predictedX, double predictedY, double predictedZ) const;
    bool place(const Placement& placement, int slot);
    void resetPlacement();
    void restoreSlot();

    int originalSlot_ = -1;
    std::uint64_t nextPlaceMs_ = 0;
    std::uint64_t nextSearchMs_ = 0;
    Placement activePlacement_;
    Placement cachedPlacement_;

    // Rotation & manual camera tracking
    float lastSetYaw_ = 0.0f;
    float lastSetPitch_ = 0.0f;

    // Visual prediction path caching
    mutable std::mutex pathMutex_;
    std::vector<PathPoint> cachedPath_;

    // Interpolation caches for render thread
    double cachedLocalX_ = 0.0;
    double cachedLocalY_ = 0.0;
    double cachedLocalZ_ = 0.0;
    double cachedLocalLastX_ = 0.0;
    double cachedLocalLastY_ = 0.0;
    double cachedLocalLastZ_ = 0.0;
    double cachedRenderX_ = 0.0;
    double cachedRenderY_ = 0.0;
    double cachedRenderZ_ = 0.0;
    double cachedMotionX_ = 0.0;
    double cachedMotionY_ = 0.0;
    double cachedMotionZ_ = 0.0;
    bool cachedPlayerValid_ = false;
    float cachedPartialTicks_ = 0.0f;
    long long lastUpdateTick_ = 0;
};

extern Clutch* clutch;

