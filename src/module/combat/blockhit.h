#pragma once

#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>

#include "../module.h"

// BlockHit — sword blocking between hits, driven entirely through the vanilla
// client code path.
//
// WHY THIS IS ANTICHEAT-AGNOSTIC
// ------------------------------
// The module never builds or sends a packet. It only writes
// GameSettings.keyBindUseItem.pressed (plus the queued-press counter, exactly
// like a real mouse-down does) and lets Minecraft.runTick() do the rest.
// Verified against the live 1.8.9 client's own bytecode, runTick contains:
//
//     if (thePlayer.isUsingItem() && !keyBindUseItem.isKeyDown())
//         playerController.onStoppedUsingItem(thePlayer);      // C07 RELEASE_USE_ITEM
//     while (keyBindAttack.isPressed())  clickMouse();         // C02 ATTACK
//     while (keyBindUseItem.isPressed()) rightClickMouse();    // C08 -> block
//     if (keyBindUseItem.isKeyDown() && rightClickDelayTimer == 0 && !isUsingItem())
//         rightClickMouse();
//
// So `pressed = true` makes the game itself emit the block, and `pressed = false`
// makes the game itself emit the release — in vanilla order, with vanilla
// rightClickDelayTimer cadence, vanilla item animation and vanilla C07 on release.
// There is no synthetic packet, no ordering anomaly and no state the server can see
// that a human holding RMB would not produce. Nothing is left to detect.
//
// Two consequences of that same bytecode drive the whole design:
//   1. runTick releases the item whenever the use key is up, so any approach that
//      sets itemInUse without holding the key alive gets cancelled a tick later.
//      Owning the keybind is the only stable way to hold a block.
//   2. The pre-motion hook this module runs from (EntityLivingBase.onLivingUpdate,
//      reached via WorldClient.updateEntities at runTick offset 2468) executes AFTER
//      the input block above. A keybind write therefore takes effect on the NEXT
//      tick. Every timing decision here is expressed in whole ticks and accounts
//      for that one-tick pipeline.
//
// WHY IT IS GOOD FOR PVP
// ----------------------
// A held block costs the sprint reset. EntityPlayerSP.onLivingUpdate scales the
// movement input by 0.2 while isUsingItem() is true, which trips its own
// `moveForward < 0.8` test and calls setSprinting(false); a non-sprint hit loses its
// knockback bonus. That sprint decision is taken one tick BEFORE the click it
// affects, and that is what pins the whole schedule:
//
//   tick A-2 : write pressed = false.
//   tick A-1 : runTick sees the key up and calls onStoppedUsingItem (C07), so
//              isUsingItem() is already false when onLivingUpdate re-evaluates
//              sprinting -> sprint is restored. Write pressed = true again here.
//   tick A   : clickMouse() (offset 2174) attacks WITH sprint, then the queued use
//              press (2184) re-engages the block in the same tick, after the hit.
//
// So the release has to be scheduled TWO ticks ahead of the attack, not one, and the
// block only has to be down for a single tick per attack. Perfect mode derives the
// attack ticks from a phase-locked model of the measured cadence and opens exactly
// that one-tick gap; every other tick stays blocked. Hold mode never releases —
// maximum damage reduction, no sprint resets — and Legit mode releases reactively
// with jitter.
//
// One consequence of the arithmetic is worth stating outright: a gap per attack is only
// affordable while the attacks are more than a couple of ticks apart. At 12 CPS they are
// ~1.7 ticks apart, so an unconditional gap-per-attack keeps the key up on every tick and
// the block never exists. The schedule therefore refuses to open a gap in a block that is
// not currently up, and holds a minimum blocked run between gaps; what that buys back is
// sprint on the attack after each gap rather than on every attack.
//
// Note for double-tap sprinters: EntityPlayerSP only restores sprint immediately
// when keyBindSprint is held. Without a sprint key it sets sprintToggleTimer = 7
// instead, and no blockhit schedule can win that back.
class BlockHit : public Module {
public:
    BlockHit();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Client thread, pre-motion, once per tick (from the SlowDownGate hook). Every
    // keybind write happens here; the module loop only latches attack intent.
    void onClientTick(JNIEnv* env);
    // Stays true while we still own the keybind so a disable can always hand it back.
    bool needsClientTick() const { return enabled || engaged_.load(std::memory_order_acquire); }

    enum class Mode {
        Hold = 0,     // never release; attacks pass through the block (max damage reduction)
        Perfect = 1,  // predictive release on the attack tick only (sprint-safe, max uptime)
        Legit = 2     // reactive release window with jitter
    };

    struct Settings {
        int mode = (int)Mode::Perfect;
        int modeKeybind = 0;  // key to cycle Hold -> Perfect -> Legit

        // Width of the release gap in ticks (Perfect/Legit). 1 is both the minimum and
        // the optimum: it is exactly the gap the sprint rule above needs, and every extra
        // tick is block uptime given away for nothing.
        int releaseTicks = 1;
        // Floor on how long the block must have been up before a gap may be opened in it.
        // The module enforces two ticks internally whatever this says, because at any
        // realistic CPS a gap per attack is a gap per tick and the block stops existing;
        // this only raises that floor, for a server that needs a longer visible block.
        int minBlockTicks = 0;
        // Derive the attack ticks from the measured cadence and open the gap BEFORE
        // the hit. This is the only thing that preserves sprint hits; with it off,
        // Perfect degrades to a reactive release after the hit has already landed.
        bool predictive = true;
        // Require an attack source to be active (LMB held, AutoClicker, or KillAura
        // holding a target). Off = block whenever a sword is held.
        bool onlyWhileAttacking = true;

        // Sword blocking works with the crosshair on a block: activation fails for a
        // sword and rightClickMouse() falls through to sendUseItem. The only cost is a
        // C08 block-placement packet, which matters solely when the block is
        // interactive (chest, door, button) and would open instead. Off by default
        // because in an arena the crosshair sits on terrain most of the time, and
        // refusing to block there is what makes the module look broken.
        bool skipOnBlockTarget = false;
    } settings;

private:
    // ---- Client-thread state (touched only inside onClientTick). ----
    std::uint32_t tick_ = 0;              // monotonic client-tick counter
    std::uint32_t blockedSinceTick_ = 0;  // tick the current press began
    std::uint32_t lastAttackTick_ = 0;    // schedule anchor
    std::uint32_t reactiveDownUntil_ = 0;  // exclusive end of a reactive release window
    float attackIntervalTicks_ = 0.0f;  // EMA of the measured attack cadence
    bool haveAttackHistory_ = false;
    bool userOwnsKey_ = false;  // physical RMB is down; hands off entirely
    std::uint64_t observedAuraAttackMs_ = 0;
    std::mt19937 rng_;

    // ---- Module-thread state (attack-intent latch only). ----
    bool prevAttackDown_ = false;
    bool prevModeKey_ = false;
    std::chrono::steady_clock::time_point observedAutoClick_{};

    // Hand-off: incremented on the module thread, drained on the client thread. A
    // counter rather than a flag so a burst of clicks between two ticks is not lost.
    std::atomic<std::uint32_t> attackSignals_{0};
    std::atomic<bool> attackHeld_{false};
    // True while WE hold the keybind down. Read by needsClientTick/getDisplayInfo.
    std::atomic<bool> engaged_{false};
    std::atomic<bool> serverBlocking_{false};

    static bool isSwordId(int itemId);
    bool isHoldingSword() const;
    bool attackSourceActive() const;
    // True when this tick is inside the sprint-safe gap for an upcoming attack, i.e.
    // when the phase-locked cadence model places an attack in [tick+2, tick+1+release].
    bool sprintSafeGapDue(int releaseTicks) const;

    // JNI plumbing. keyBind is a local ref the caller owns.
    jobject useItemKeyBind(JNIEnv* env) const;
    static bool readPressed(JNIEnv* env, jobject keyBind);
    static void writePressed(JNIEnv* env, jobject keyBind, bool value, bool queuePress);

    void press(JNIEnv* env, jobject keyBind);
    void release(JNIEnv* env, jobject keyBind);
    void handBackKey(JNIEnv* env);  // unconditional release path for bail-outs
    void resetCadence();
};

extern BlockHit* blockHit;
