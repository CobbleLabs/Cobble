#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../sdk/core/minecraft.h"
#include "../module.h"

// Backtrack — real (Augustus-style) incoming packet-hold.
//
// While engaged on a target, the target's incoming S14/S18 movement packets are HELD
// by a blocking bytecode hook (BacktrackGate / IncomingHook), so the entity stays at
// its previous position on the client. You hit it there; on a lag-compensating
// anticheat (Grim/Intave) the extra latency makes the server rewind the target too, so
// the hit registers. Held packets are re-injected after the hold window, snapping the
// entity forward. Unlike a self-lag, only the TARGET lags — not you.

struct PositionSnapshot {
    double x, y, z;
    float yaw, pitch;
    int hurtTime;
    int tickAge;
    std::chrono::steady_clock::time_point timestamp;
};

class Backtrack : public Module {
public:
    Backtrack();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Per-tick on the CLIENT thread (from the pre-motion hook): pick target, decide
    // whether to hold, and drive packet release.
    void onClientTick(JNIEnv* env);

    // Read by BacktrackGate (the blocking incoming hook) to decide whether to hold a
    // given entity's movement packet. Lock-free.
    bool wantsHold(int entityId) const {
        return blocking_.load(std::memory_order_acquire) &&
               entityId == holdTargetId_.load(std::memory_order_acquire);
    }
    int getHoldMs() const;

    // Consumed by Piercing's backtrack check.
    const std::deque<PositionSnapshot>* getPositionHistory(const std::string& uuid) const;

    // Normal: hold only when engaging (respects Only On Hit). Strong: hold constantly
    // while a target is in range (uniform added latency — harder for strong ACs that
    // compare combat-vs-idle latency to flag).
    enum class Mode : int { Normal = 0, Strong = 1 };

    struct Settings {
        Mode mode = Mode::Normal;
        int ticks = 3;             // hold depth (each ≈ 50 ms)
        float maxDistance = 4.0f;  // engage range
        bool weaponOnly = false;
        bool onlyOnHit = true;     // Normal mode: hold only while attacking (LMB)
        bool showStatus = true;
    } settings;

private:
    bool isHoldingWeapon();
    void recordPosition(const PlayerInfo& target);
    void clearHistory();

    // Resolve the JNI handles used by the position-rollback (lazy, once).
    void resolveHandles(JNIEnv* env);
    // Freeze the target entity at a state `ticks` behind: move its posX/Y/Z AND its
    // bounding box to a delayed position and stop its interpolation, so it renders
    // and raytraces there. Real (server) positions are sourced from the entity's
    // interpolation target (otherPlayerMP*) which we never overwrite.
    void freezeTarget(JNIEnv* env, jobject entity, int entityId, int depth);

    std::atomic<int> holdTargetId_{-1};
    std::atomic<bool> blocking_{false};
    std::atomic<int> heldShown_{0};  // rollback depth (ticks), for the HUD

    std::unordered_map<std::string, std::deque<PositionSnapshot>> entityHistory;
    std::string currentTargetUUID;

    struct Vec3d { double x = 0.0, y = 0.0, z = 0.0; };
    std::unordered_map<int, std::deque<Vec3d>> realTrail_;  // entityId -> real-position history
};

extern Backtrack* backtrack;
