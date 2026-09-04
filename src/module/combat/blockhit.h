#pragma once

#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>

#include "../module.h"

// BlockHit — sword blocking placed where a block can actually do something,
// expressed entirely through the vanilla client code path.
//
// WHY THIS IS ANTICHEAT-AGNOSTIC
// ------------------------------
// The module never builds or sends a packet. It only writes
// GameSettings.keyBindUseItem.pressed (plus the queued-press counter, exactly like
// a real mouse-down does) and lets Minecraft.runTick() do the rest. Verified
// against the live 1.8.9 client's own bytecode, runTick contains:
//
//     if (thePlayer.isUsingItem() && !keyBindUseItem.isKeyDown())
//         playerController.onStoppedUsingItem(thePlayer);      // C07 RELEASE_USE_ITEM
//     while (keyBindAttack.isPressed())  clickMouse();         // C02 ATTACK
//     while (keyBindUseItem.isPressed()) rightClickMouse();    // C08 -> block
//     if (keyBindUseItem.isKeyDown() && rightClickDelayTimer == 0 && !isUsingItem())
//         rightClickMouse();
//
// So `pressed = true` makes the game itself emit the block and `pressed = false`
// makes the game itself emit the release — vanilla order, vanilla
// rightClickDelayTimer cadence, vanilla animation, vanilla C07 on release. There is
// no synthetic packet and no state the server can see that a human holding RMB
// would not produce.
//
// Two consequences of that same bytecode constrain everything below:
//   1. runTick releases the item whenever the use key is up, so owning the keybind
//      is the only stable way to hold a block.
//   2. This module's hook (EntityLivingBase.onLivingUpdate, reached from
//      WorldClient.updateEntities at runTick offset 2468) runs AFTER the input block
//      above, so a keybind write lands on the NEXT tick. Every number here is in
//      whole ticks and accounts for that one-tick pipeline.
//
// WHAT THE STRATEGY IS
// --------------------
// A block is worth its cost only when an incoming hit can actually be reduced, and
// 1.8 says out loud when that is. EntityLivingBase.attackEntityFrom sets
// hurtResistantTime = maxHurtResistantTime (20) on every hit that lands, and while
// hurtResistantTime > 10 a new hit is processed only `if (amount > lastDamage)`, and
// then only for the difference. Entity.onEntityUpdate decrements it once per tick,
// before this hook runs. So for ~10 ticks after every hit taken we are effectively
// immune, and a block held across those ticks reduces nothing whatsoever.
//
// It is not free either. isUsingItem() scales moveStrafe/moveForward by 0.2 in
// EntityPlayerSP.onLivingUpdate, which trips its own `moveForward < 0.8` test and
// calls setSprinting(false), and a non-sprint attack loses a knockback level
// (`if (isSprinting()) ++knockback`). Blocking inside the immunity window pays the
// full movement and knockback price for exactly zero damage reduction — which is
// where the previous schedule spent most of its ticks.
//
// Hence the one rule the whole module is built on: block while vulnerable, let go
// while immune. The fight itself then drives the rhythm instead of a timer, and
// every hit you take buys ten ticks of full-speed, full-knockback counter-attacking.
//
// THE SPRINT GAP
// --------------
// An attack at tick A keeps its sprint bonus only if isUsingItem() was already false
// when onLivingUpdate evaluated sprinting at A-1. Writing the key up at A-2 makes
// runTick call onStoppedUsingItem at A-1 (C07, before updateEntities), so sprint is
// restored in time; writing it back down at A-1 re-presses inside runTick A, where
// offset 2184 runs after the attack at 2174 — the hit lands sprinting and the block
// is back up in the same tick. The gap is therefore exactly two ticks wide and must
// start two ticks BEFORE the attack. Only Trade spends uptime on it.
//
// Note for double-tap sprinters: EntityPlayerSP restores sprint immediately only
// while keyBindSprint is held. Without a sprint key it sets sprintToggleTimer = 7
// and no schedule can win that back.
//
// WHY IT CANNOT STUTTER
// ---------------------
// Whatever a mode asks for, the key cannot change state faster than a floor of
// three ticks down / two ticks up. Both numbers are physical, not taste: two ticks
// up is precisely what the sprint rule above needs, and three ticks down is the
// shortest block worth a C08/C07 round trip. That caps the transition rate at one
// cycle per five ticks even in the worst case, and the immunity rule keeps the real
// rate far below it.
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

    // Five intents, not five timings. Each one answers "what is a block for" and the
    // module works out the ticks; there is nothing to tune.
    enum class Mode {
        // Your swings come first. Blocks only in the gaps your own cadence leaves —
        // if the next attack is closer than four ticks it does not block at all, so a
        // held block never costs you a sprint hit. What it covers is the silence:
        // backing off, waiting out a crit, closing distance.
        Aggressive = 0,
        // The even trade, and the one to leave on. Blocks whenever a hit can land,
        // and once a block has been up long enough to have earned it, opens the
        // two-tick sprint gap in front of your next attack so the hit keeps its
        // knockback. Gives away a little uptime for a lot of pressure.
        Trade = 1,
        // Uptime above all. Blocks whenever a hit can land and never spends a tick on
        // sprint, so every incoming hit is halved; the only time the key comes up is
        // the immunity window, where blocking cannot help anyway and the movement is
        // worth more than an animation.
        Defensive = 2,
        // Never lets go while the fight is on. Halves everything, including the hits
        // the other modes correctly ignore, and costs 80% of your movement speed for
        // as long as it is held — that is the actual price, not a caveat. For eating
        // a burst you cannot outrun. Adaptive never chooses it for you.
        Turtle = 3,
        // The default. Picks between Aggressive, Trade and Defensive every tick from
        // health and whether anything is actually hitting you: healthy and untouched
        // plays Aggressive, hurt plays Trade, low and under fire plays Defensive.
        Adaptive = 4
    };

    struct Settings {
        int mode = (int)Mode::Adaptive;
        int modeKeybind = 0;  // cycles Aggressive -> Trade -> Defensive -> Turtle -> Adaptive
    } settings;
private:
    // ---- Client-thread state (touched only inside onClientTick). ----
    std::uint32_t tick_ = 0;               // monotonic client-tick counter
    std::uint32_t blockedSinceTick_ = 0;   // tick the current press began
    std::uint32_t releasedSinceTick_ = 0;  // tick the current release began
    std::uint32_t lastAttackTick_ = 0;     // our own last swing; anchors the cadence model
    std::uint32_t lastHitTakenTick_ = 0;   // last tick hurtTime rose, i.e. we were hit
    std::uint32_t gapTicksLeft_ = 0;       // remaining ticks of an open sprint gap
    float attackIntervalTicks_ = 0.0f;     // EMA of our measured attack cadence
    bool haveAttackHistory_ = false;
    bool haveTakenHit_ = false;
    bool userOwnsKey_ = false;  // physical RMB is down; hands off entirely
    int prevHurtTime_ = 0;      // edge detector for hits taken
    std::uint64_t observedAuraAttackMs_ = 0;
    std::mt19937 rng_;

    // ---- Module-thread state (attack-intent latch only). ----
    bool prevAttackDown_ = false;
    bool prevModeKey_ = false;
    std::chrono::steady_clock::time_point observedAutoClick_{};

    // Hand-off: incremented on the module thread, drained on the client thread. A
    // counter rather than a flag so a burst of clicks between two ticks is not lost.
    std::atomic<std::uint32_t> attackSignals_{0};
    // When the last real click was latched, from either source. This is what makes
    // "an attack is happening" a statement about now instead of about which modules
    // are armed; see attackSourceActive().
    std::atomic<std::uint64_t> lastAttackSignalMs_{0};
    std::atomic<bool> attackHeld_{false};
    // True while WE hold the keybind down. Read by needsClientTick/getDisplayInfo.
    std::atomic<bool> engaged_{false};
    std::atomic<bool> serverBlocking_{false};
    // Why the schedule stood down on the last client tick, or null while it is running,
    // and which mode Adaptive resolved to. Both are always string literals (static
    // storage), so publishing the bare pointer across threads is safe and the ArrayList
    // renders it without a copy.
    std::atomic<const char*> gateReason_{nullptr};
    std::atomic<const char*> activeMode_{nullptr};
    static bool isSwordId(int itemId);
    bool isHoldingSword() const;
    // True when an exchange is actually running: LMB held, clicks landing from either
    // source, or KillAura holding a target.
    bool attackSourceActive() const;

    // 1.0 when the health fields cannot be read, so an unreadable player never gets
    // mistaken for a dying one.
    float healthFraction() const;
    // hurtResistantTime is still above half: a follow-up hit of the same strength
    // cannot touch us, so a block would reduce nothing.
    bool damageImmune() const;
    // Something has hit us within the last two seconds.
    bool underPressure() const;
    // Ticks until our own next swing, from the phase-locked cadence model
    // (anchor + round(interval * k)). kNoAttackDue when there is no usable cadence.
    std::uint32_t ticksUntilOurAttack() const;

    // Adaptive resolved against this tick. Never returns Adaptive or Turtle.
    Mode resolveMode() const;
    // The mode's own answer, before the gap logic and the hysteresis floors.
    bool wantsBlock(Mode effective) const;

    // Right-clicking one of these opens a screen or flips a state instead of raising
    // the sword, so blocking is impossible while the crosshair is on it. Detected
    // rather than configured: plain terrain blocks fine and always did.
    static bool isInteractiveBlockId(int blockId);
    bool crosshairWouldInteract() const;

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
