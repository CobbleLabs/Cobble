#pragma once

#include <jni.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include "../module.h"

// KillAura — auto-targets and attacks nearby players.
//
// Everything that touches the wire (rotation + the attack itself) is driven from the
// outgoing packet gate, ONCE per flying packet, so it stays tick-synchronised and the
// attack is queued BEFORE the flying packet (vanilla order). Doing it on the async
// module-update loop instead is what makes Grim scream Post / AimModulo360 / Simulation.
// onUpdate only SELECTS the target; onOutgoingPacket rotates, fires and re-syncs.
class KillAura : public Module {
public:
    KillAura();
    ~KillAura() override;

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Runs PRE-MOTION each tick (from the onLivingUpdate hook, before the game moves
    // the player). This is where the aura rotation is computed and applied to the
    // REAL client rotation, so the movement the game simulates matches the rotation
    // we send — the only way to pass Grim's MoveFix/Simulation. The attack fires here
    // too (before the flying packet). `playerInstance` is the local EntityPlayerSP.
    void onPreMotion(JNIEnv* env, jobject playerInstance);

    // Runs on the outgoing flying packet: just pins the packet's yaw/pitch to the
    // rotation onPreMotion already set (belt-and-suspenders) so nothing else perturbs it.
    void onOutgoingPacket(JNIEnv* env, jobject packet);
    bool isRewritingRotation() const { return enabled && hasAim_.load(std::memory_order_acquire); }

    // Steady-clock milliseconds of the last attack this module queued. Written in
    // onPreMotion, so any other client-tick consumer dispatched after KillAura sees
    // the attack from the SAME tick. BlockHit uses it to time its release window.
    std::uint64_t lastAttackMs() const { return lastAttackMs_; }

    enum class TargetMode : int { Distance = 0, Health = 1, Angle = 2 };
    enum class SwitchMode : int { Single = 0, Switch = 1, Multi = 2 };

    struct Settings {
        // Targeting
        float reach = 3.4f;
        int fov = 120;
        TargetMode targetMode = TargetMode::Distance;
        SwitchMode switchMode = SwitchMode::Single;
        int multiTargets = 3;
        bool throughWalls = true;
        bool invisibles = false;
        bool weaponOnly = false;
        // antiBot lived here but was never read — NPCs were skipped unconditionally. Bot
        // filtering is now the AntiBot module (src/module/combat/antibot.h).
        int maxHurtTime = 10;  // attack only when target.hurtTime <= this (0..10; 10 = always)

        // Rotations
        bool moveFix = true;          // keep movement in your real look direction (Grim-safe)
        bool silent = true;           // restore the render camera so it doesn't snap to the target
        float rotationSpeed = 40.0f;  // deg/tick cap (humanized)
        bool randomization = true;    // GCD-quantized deltas + per-tick speed variance
        float jitter = 1.0f;          // extra random deg (kept on the GCD grid)
        bool rotateOnHitOnly = false; // only rotate in the window around an attack

        // Clicking
        int minCPS = 8;
        int maxCPS = 12;
        bool clickRandomization = true;  // humanized (non-uniform) click interval
        int hitChance = 100;             // % chance to actually attack when otherwise ready
    } settings;

private:
    static std::uint64_t nowMs();
    static float wrapDegrees(float a);
    bool isHoldingWeapon();
    bool hasLineOfSight(double ex, double ey, double ez, double tx, double ty, double tz) const;
    double unit();          // uniform [0,1) — network thread only
    void rollNextDelay();   // pick nextDelayMs_ from the CPS range — network thread only
    void clearTarget(JNIEnv* env);  // release the held target GlobalRefs
    // Rotate the movement input by diffDeg so the world movement stays in the real
    // look direction even though rotationYaw was set to the aura yaw (MoveFix).
    void applyMoveFix(JNIEnv* env, jobject playerInstance, float diffDeg);
    // Recover the user's real (mouse) rotation from the client, by delta against what we
    // last wrote. Immune to a tick where nothing restored the camera.
    void syncRealRotation();
    // Give the camera back to the user if we still hold it (missed restore, disengage,
    // module toggled off). Safe to call when we don't own it.
    void releaseCamera();
    // Drop the target, stop aiming and return the camera. Used by every "not fighting
    // right now" path (GUI open, no weapon, module disabled).
    void standDown();

    // ---- Target hand-off: written by onUpdate (module thread), read by
    //      onOutgoingPacket (network thread), under targetMutex_. ----
    std::mutex targetMutex_;
    jobject targetInstance_ = nullptr;        // GlobalRef of the primary target
    std::vector<jobject> multiInstances_;     // GlobalRefs of extra Multi targets
    float aimYaw_ = 0.0f, aimPitch_ = 0.0f;   // raw angles to the aim point
    float targetDist_ = 0.0f;                 // reach distance to the aim point
    int targetHurtTime_ = 0;
    bool haveTarget_ = false;

    std::string targetUuid_;  // sticky selection (module thread)
    int switchIndex_ = 0;

    std::atomic<bool> hasAim_{false};

    // ---- Network-thread-only rotation + click state. ----
    std::mt19937 rng_;
    std::uint64_t lastAttackMs_ = 0;
    float nextDelayMs_ = 100.0f;
    int rotationSentTicks_ = 0;
    bool haveCur_ = false;
    float curYaw_ = 0.0f, curPitch_ = 0.0f;
    bool jitterToggle_ = false;

    // Real (mouse-driven) rotation, captured pre-motion so we can restore the render
    // camera to it (silent) and rotate movement to it (movefix).
    float realYaw_ = 0.0f, realPitch_ = 0.0f;

    // Rotation bookkeeping. realYaw_ used to be re-read straight out of the client every
    // tick, which silently assumed the restore in onOutgoingPacket always runs. It does
    // not: Clutch/Scaffold/ThrowPots take over rotation, and FakeLag/Blink/Piercing can
    // drop the C03 entirely. On those ticks the camera was left on the aura yaw, the next
    // tick read that back as "real", and the error accumulated a chunk of aura sweep per
    // miss until the view had wrapped a full turn — the instant-360 spin.
    //
    // Instead, remember exactly what we wrote and recover the real rotation by delta:
    // whatever the client reports minus our own last write is, by construction, mouse
    // input and nothing else. cameraOwned_ says the aura rotation is still on the camera
    // and has to be handed back.
    bool haveWritten_ = false;
    bool cameraOwned_ = false;
    float lastWrittenYaw_ = 0.0f, lastWrittenPitch_ = 0.0f;

    // MoveFix field access (resolved lazily, like NoSlowDown).
    //
    // These are EntityLivingBase.moveStrafing/moveForward, NOT MovementInput's.
    // The pre-motion hook is injected at the ENTRY of EntityLivingBase.onLivingUpdate,
    // and EntityPlayerSP has already copied MovementInput into these fields by then;
    // moveEntityWithHeading() — still ahead of us in that same method — reads THESE.
    // Writing MovementInput at this point changed nothing for the current tick (the
    // old MoveFix was a silent no-op) and only leaked a rotated input into the next
    // tick's sprint check.
    jfieldID entStrafe_ = nullptr, entForward_ = nullptr;
    bool moveFieldsResolved_ = false, moveFieldsOk_ = false;
};

extern KillAura* killAura;
