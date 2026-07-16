#pragma once

#include "../module.h"

#include <cstdint>
#include <mutex>
#include <vector>

// Placement engine shared by two modules:
//   Scaffold - bridges blocks under the spot just ahead of where you look.
//   Clutch   - a fork that only places when your predicted path would fall into
//              the void (an emergency save).
// Both run the same anticheat-safe silent placement, driven entirely from the
// outgoing packet gate (see onOutgoingPacket) so the rotation and the block
// placement are ordered correctly relative to the flying packet.
enum class PlaceMode { Scaffold = 0, Clutch = 1 };

class Clutch : public Module {
public:
    struct Settings {
        float scaffoldLead = 0.9f;  // Scaffold: blocks ahead (along your look) to place

        int predictionMs = 300;  // Clutch: how far ahead to simulate the fall
        int searchRadius = 5;    // Clutch: anchor search radius
        bool allowForward = false;  // Clutch: also save while on ground moving toward a ledge

        float reach = 4.2f;
        int placeDelayMs = 70;
        bool restoreSlot = true;
        bool cameraWeight = true;

        // HUD
        bool showBps = true;
        bool showPath = true;
        bool showPlacement = true;
    } settings;

    explicit Clutch(PlaceMode mode);
    ~Clutch() override;

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Called from the outgoing packet gate for every outgoing packet. When it sees
    // a flying (movement) packet and we have a placement queued, it rewrites that
    // packet's rotation to the placement aim and fires the block placement — both
    // BEFORE the flying packet reaches the wire, matching vanilla packet order.
    void onOutgoingPacket(JNIEnv* env, jobject packet);

    // True while we own the outgoing rotation for a placement, so other observers
    // (e.g. AimAssist) defer and don't corrupt the aim.
    bool isRewritingRotation() const { return enabled && haveTarget_; }

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
    int findBlockSlot() const;
    bool isDangerous(double predictedX, double predictedY, double predictedZ) const;
    bool isPlacementUsable(const Placement& placement) const;
    Placement findPlacement(double predictedX, double predictedY, double predictedZ) const;
    void resetPlacement();
    void restoreSlot();

    // Runs on the game thread from onOutgoingPacket: aim the client rotation at the
    // placement (silently, restored immediately), verify the raytrace and fire the
    // vanilla right-click. Returns true if the block placement was fired.
    bool fireSilentPlace(JNIEnv* env, const Placement& p, int slot, float aimYaw, float aimPitch);

    const PlaceMode placeMode_;
    bool isClutch() const { return placeMode_ == PlaceMode::Clutch; }

    int originalSlot_ = -1;
    std::uint64_t nextSearchMs_ = 0;
    Placement activePlacement_;
    Placement cachedPlacement_;

    // Pending placement handed from onUpdate (background) to onOutgoingPacket (game
    // thread) under this mutex.
    std::mutex targetMutex_;
    Placement gatePlacement_;
    int gateSlot_ = -1;
    bool haveTarget_ = false;

    // Gate-side rotation/placement bookkeeping.
    int rotationShippedTicks_ = 0;   // flying packets that have carried the aim
    bool jitterToggle_ = false;      // ±1 GCD micro-jitter so no two sent rotations duplicate
    std::uint64_t lastPlaceMs_ = 0;  // throttle between fired placements

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

// Two instances: the Scaffold module and its Clutch fork.
extern Clutch* scaffold;
extern Clutch* clutch;
