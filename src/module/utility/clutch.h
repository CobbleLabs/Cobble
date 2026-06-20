#pragma once

#include "../module.h"

#include <cstdint>

class Clutch : public Module {
public:
    struct Settings {
        int predictionMs = 300;
        int searchRadius = 5;
        float reach = 4.2f;
        int placeDelayMs = 70;
        bool restoreSlot = true;
    } settings;

    Clutch();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;

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
    Placement activePlacement_;
};

extern Clutch* clutch;
