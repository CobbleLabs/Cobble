#include "blockhit.h"

#include <algorithm>
#include <cmath>

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../sdk/player/player.h"
#include "../../ui/core/overlay.h"
#include "../../ui/hud/notifications.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "autoclicker.h"
#include "autorod.h"
#include "killaura.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

BlockHit* blockHit = nullptr;

namespace {

bool clearEx(JNIEnv* env) {
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

std::uint64_t nowMs() {
    return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool gameFocused() {
    const HWND fg = GetForegroundWindow();
    return fg != nullptr && (Overlay::gameWindow == nullptr || fg == Overlay::gameWindow);
}

// A cadence outside this range is not a fight (or is a measurement artifact) and must
// never anchor the schedule. 1 tick = 50 ms, so 40 ticks = 2 s.
constexpr float kMinIntervalTicks = 0.5f;
constexpr std::uint32_t kMaxIntervalTicks = 40;

}  // namespace

BlockHit::BlockHit() : Module("BlockHit"), rng_((unsigned)nowMs()) {
    keybind = 0;
}

void BlockHit::onEnable() {
    prevAttackDown_ = false;
    observedAutoClick_ = autoClicker ? autoClicker->lastAutoClick : std::chrono::steady_clock::time_point{};
    observedAuraAttackMs_ = killAura ? killAura->lastAttackMs() : 0;
    attackSignals_.store(0, std::memory_order_release);
    attackHeld_.store(false, std::memory_order_release);
    serverBlocking_.store(false, std::memory_order_release);
    resetCadence();
}

void BlockHit::onDisable() {
    // No JNI from the module thread. needsClientTick() stays true while engaged_ is
    // set, so the next client tick performs the release for us.
    reactiveDownUntil_ = 0;
}

void BlockHit::resetCadence() {
    attackIntervalTicks_ = 0.0f;
    haveAttackHistory_ = false;
    lastAttackTick_ = 0;
    reactiveDownUntil_ = 0;
}

bool BlockHit::isSwordId(int itemId) {
    // wood / stone / iron / diamond / gold. ItemSword is the only vanilla item whose
    // use-action is BLOCK, so this list is exactly the set that can block.
    return itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283;
}

bool BlockHit::isHoldingSword() const {
    return isSwordId(player.getHeldItemId());
}

// ---------------------------------------------------------------------------
// Module thread: latch intent only. Nothing here touches the game.
// ---------------------------------------------------------------------------

void BlockHit::onUpdate() {
    if (settings.modeKeybind != 0) {
        const bool down = (GetAsyncKeyState(settings.modeKeybind) & 0x8000) != 0;
        if (down && !prevModeKey_) {
            settings.mode = (settings.mode + 1) % 3;
            const char* label = settings.mode == (int)Mode::Hold      ? "BlockHit: Hold"
                                : settings.mode == (int)Mode::Perfect ? "BlockHit: Perfect"
                                                                      : "BlockHit: Legit";
            NotificationManager::addSystem(label);
        }
        prevModeKey_ = down;
    }

    if (!enabled)
        return;

    // Attack edges. Vanilla only attacks an entity on a fresh keyBindAttack press
    // (Minecraft.runTick drains the queued presses through clickMouse), so an edge is
    // a one-to-one stand-in for an outgoing C02 ATTACK.
    const bool attackDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && gameFocused();
    if (attackDown && !prevAttackDown_)
        attackSignals_.fetch_add(1, std::memory_order_acq_rel);
    prevAttackDown_ = attackDown;
    attackHeld_.store(attackDown, std::memory_order_release);

    if (autoClicker && autoClicker->lastAutoClick != observedAutoClick_) {
        observedAutoClick_ = autoClicker->lastAutoClick;
        attackSignals_.fetch_add(1, std::memory_order_acq_rel);
    }
}

bool BlockHit::attackSourceActive() const {
    if (attackHeld_.load(std::memory_order_acquire))
        return true;
    if (autoClicker && autoClicker->enabled)
        return true;
    // KillAura only rewrites rotation while it actually holds a target, which makes it
    // a precise "a fight is happening" signal rather than just "the module is on".
    if (killAura && killAura->isRewritingRotation())
        return true;
    return false;
}

// ---------------------------------------------------------------------------
// JNI plumbing.
// ---------------------------------------------------------------------------

jobject BlockHit::useItemKeyBind(JNIEnv* env) const {
    if (!env || !mc.instance || !JniCache::fid_gameSettings || !JniCache::fid_keyBindUseItem)
        return nullptr;

    jobject settingsObj = env->GetObjectField(mc.instance, JniCache::fid_gameSettings);
    if (clearEx(env) || !settingsObj)
        return nullptr;

    jobject keyBind = env->GetObjectField(settingsObj, JniCache::fid_keyBindUseItem);
    env->DeleteLocalRef(settingsObj);
    if (clearEx(env))
        return nullptr;
    return keyBind;
}

bool BlockHit::readPressed(JNIEnv* env, jobject keyBind) {
    if (!JniCache::fid_pressed)
        return false;
    const jboolean value = env->GetBooleanField(keyBind, JniCache::fid_pressed);
    if (clearEx(env))
        return false;
    return value == JNI_TRUE;
}

void BlockHit::writePressed(JNIEnv* env, jobject keyBind, bool value, bool queuePress) {
    if (!JniCache::fid_pressed)
        return;
    env->SetBooleanField(keyBind, JniCache::fid_pressed, value ? JNI_TRUE : JNI_FALSE);
    clearEx(env);

    // A real mouse-down bumps pressTime (KeyBinding.onTick) in addition to setting
    // pressed. That queued press is what lets runTick's
    // `while (keyBindUseItem.isPressed()) rightClickMouse()` fire immediately, instead
    // of waiting for rightClickDelayTimer to reach 0 — which is up to 4 ticks after the
    // previous use and would otherwise stall every re-block.
    if (value && queuePress && JniCache::fid_pressTime) {
        const jint queued = env->GetIntField(keyBind, JniCache::fid_pressTime);
        if (!clearEx(env)) {
            env->SetIntField(keyBind, JniCache::fid_pressTime, queued + 1);
            clearEx(env);
        }
    }
}

void BlockHit::press(JNIEnv* env, jobject keyBind) {
    if (engaged_.load(std::memory_order_acquire))
        return;
    writePressed(env, keyBind, true, true);
    engaged_.store(true, std::memory_order_release);
    blockedSinceTick_ = tick_;
}

void BlockHit::release(JNIEnv* env, jobject keyBind) {
    if (!engaged_.load(std::memory_order_acquire))
        return;
    writePressed(env, keyBind, false, false);
    engaged_.store(false, std::memory_order_release);
}

void BlockHit::handBackKey(JNIEnv* env) {
    if (!engaged_.load(std::memory_order_acquire)) {
        serverBlocking_.store(false, std::memory_order_release);
        return;
    }
    jobject keyBind = useItemKeyBind(env);
    if (keyBind) {
        release(env, keyBind);
        env->DeleteLocalRef(keyBind);
    } else {
        // Cannot reach the keybind: drop ownership anyway so needsClientTick() stops
        // pinning this module alive. The user's next RMB press resyncs the state.
        engaged_.store(false, std::memory_order_release);
    }
    serverBlocking_.store(false, std::memory_order_release);
    reactiveDownUntil_ = 0;
}

// ---------------------------------------------------------------------------
// The schedule.
//
// An attack at tick A keeps its sprint bonus only if isUsingItem() was already false
// when EntityPlayerSP.onLivingUpdate evaluated sprinting at tick A-1, and a keybind
// write only reaches runTick on the following tick. So the block must be written down
// at A-2 and written back up at A-1: the gap this tick belongs to is decided by
// whether an attack is due in [tick+2, tick+1+releaseTicks].
//
// Attack ticks come from a phase-locked model — anchor + round(interval * k) — rather
// than a single "next attack" estimate. Rounding a fractional interval reproduces the
// real beat of a steady clicker (an interval of 1.67 ticks yields +2, +3, +5, +7, +8,
// +10, which is exactly what 12 CPS looks like on a 20 TPS clock), and anchoring on
// the last observed attack means a missed prediction rolls into the next slot instead
// of stranding the block.
// ---------------------------------------------------------------------------

bool BlockHit::sprintSafeGapDue(int releaseTicks) const {
    if (!haveAttackHistory_ || attackIntervalTicks_ <= 0.0f)
        return false;
    if (tick_ - lastAttackTick_ > kMaxIntervalTicks)
        return false;  // the fight went quiet; the anchor is stale

    // Sub-tick cadences cannot be scheduled (every tick carries an attack); clamping to
    // one tick makes the loop below match every tick, which correctly means "no gap to
    // give away" rather than a bogus schedule.
    const float interval = std::max(attackIntervalTicks_, 1.0f);
    const std::uint32_t earliest = tick_ + 2u;
    const std::uint32_t latest = tick_ + 1u + (std::uint32_t)releaseTicks;

    // Start on the slot at or before now, then walk forward. releaseTicks is capped at
    // 6, so the window is at most 6 wide and a handful of steps always covers it.
    float slot = std::floor((float)(tick_ - lastAttackTick_) / interval);
    if (slot < 1.0f)
        slot = 1.0f;
    for (int guard = 0; guard < 16; ++guard, slot += 1.0f) {
        const std::uint32_t attackTick =
            lastAttackTick_ + (std::uint32_t)std::lround(interval * slot);
        if (attackTick > latest)
            return false;
        if (attackTick >= earliest)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Client thread, pre-motion, once per tick.
// ---------------------------------------------------------------------------

void BlockHit::onClientTick(JNIEnv* env) {
    if (!env)
        return;
    // Without KeyBinding.pressed there is no channel to drive at all, and pretending
    // otherwise would leave engaged_ desynced and re-queue a use press every tick.
    if (!JniCache::fid_pressed)
        return;

    ++tick_;

    // Drain the attack latch first: it is needed by every branch below, and a bail-out
    // must not leave stale signals to be replayed on re-entry.
    std::uint32_t attacks = attackSignals_.exchange(0, std::memory_order_acq_rel);

    // KillAura fires inside this same hook and is dispatched ahead of us, so its
    // attack for THIS tick is already visible here.
    if (killAura && killAura->enabled) {
        const std::uint64_t auraMs = killAura->lastAttackMs();
        if (auraMs != observedAuraAttackMs_) {
            observedAuraAttackMs_ = auraMs;
            if (auraMs != 0)
                ++attacks;
        }
    }

    if (attacks > 0) {
        if (haveAttackHistory_ && tick_ > lastAttackTick_) {
            const std::uint32_t span = tick_ - lastAttackTick_;
            if (span <= kMaxIntervalTicks) {
                // Several attacks can be drained in one tick at high CPS, so divide the
                // span by how many landed instead of reading it as a single interval.
                const float measured =
                    std::max((float)span / (float)attacks, kMinIntervalTicks);
                attackIntervalTicks_ = attackIntervalTicks_ <= 0.0f
                                           ? measured
                                           : attackIntervalTicks_ * 0.6f + measured * 0.4f;
            }
        }
        lastAttackTick_ = tick_;
        haveAttackHistory_ = true;
    } else if (haveAttackHistory_ && tick_ - lastAttackTick_ > kMaxIntervalTicks) {
        // Two seconds without a hit: the anchor is worthless and a stale schedule would
        // open gaps at random. Fall back to the reactive path until the next attack.
        resetCadence();
    }

    if (!enabled) {
        handBackKey(env);
        return;
    }

    // A physical RMB press owns the keybind outright: the user is blocking, eating or
    // shooting, and vanilla will clear pressed itself on the mouse-up event. Drop our
    // ownership without writing anything so the two never fight.
    const bool physicalUse = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (physicalUse) {
        if (engaged_.load(std::memory_order_acquire)) {
            engaged_.store(false, std::memory_order_release);
            reactiveDownUntil_ = 0;
        }
        userOwnsKey_ = true;
        serverBlocking_.store(true, std::memory_order_release);
        return;
    }
    if (userOwnsKey_) {
        // Fresh start after the user let go: their mouse-up already cleared pressed.
        userOwnsKey_ = false;
        reactiveDownUntil_ = 0;
    }

    // AutoRod owns the use-item channel for its whole sequence. Letting BlockHit re-arm
    // between rod phases produces a zero-tick press/release and corrupts both modules.
    // isSequencing() covers a sequence still unwinding after the module was toggled off.
    const bool rodBusy = autoRod && (autoRod->enabled || autoRod->isSequencing());
    const bool blocked = !player.isValid() || Menu::isOpen || mc.isInGui() || !gameFocused() ||
                         rodBusy || !isHoldingSword() ||
                         (settings.skipOnBlockTarget && mc.isTargetingBlock()) ||
                         (settings.onlyWhileAttacking && !attackSourceActive());
    if (blocked) {
        handBackKey(env);
        return;
    }

    jobject keyBind = useItemKeyBind(env);
    if (!keyBind)
        return;

    // Someone else (or a stale frame) cleared pressed while we thought we owned it.
    // Re-sync rather than trusting our own flag.
    if (engaged_.load(std::memory_order_acquire) && !readPressed(env, keyBind))
        engaged_.store(false, std::memory_order_release);

    // Only the game can tell us whether the block actually took effect; used for the
    // minimum-uptime rule and the HUD.
    bool usingItem = false;
    if (player.mid_isUsingItem && player.instance) {
        usingItem = env->CallBooleanMethod(player.instance, player.mid_isUsingItem) == JNI_TRUE;
        if (clearEx(env))
            usingItem = false;
    }
    serverBlocking_.store(usingItem && engaged_.load(std::memory_order_acquire),
                          std::memory_order_release);

    const Mode mode = (Mode)settings.mode;
    const int releaseTicks = std::clamp(settings.releaseTicks, 1, 6);
    const int minBlockTicks = std::clamp(settings.minBlockTicks, 0, 10);
    const bool engaged = engaged_.load(std::memory_order_acquire);

    // Decide the desired key state for this tick, then reconcile. Exactly one JNI write
    // per transition and none at all while the state is already right — the previous
    // design chained release/press branches and could do both in one tick, which
    // cancelled out and left the block permanently on.
    bool wantDown = false;
    if (mode != Mode::Hold) {
        const bool usePredictor = mode == Mode::Perfect && settings.predictive &&
                                  haveAttackHistory_ && attackIntervalTicks_ > 0.0f;
        if (usePredictor) {
            wantDown = sprintSafeGapDue(releaseTicks);
            reactiveDownUntil_ = 0;
        } else {
            // No usable cadence yet (first hits of a fight, or Legit mode): release for
            // a fixed window once an attack is seen. The hit itself is already gone, so
            // this only buys the next one — it is the fallback, not the plan.
            if (attacks > 0) {
                int window = releaseTicks;
                if (mode == Mode::Legit)
                    window += (int)(rng_() % 2);  // no human releases for the same span twice
                reactiveDownUntil_ = tick_ + (std::uint32_t)window;
            }
            wantDown = tick_ < reactiveDownUntil_;
        }
    }

    // Escape hatch: never drop a block that has not been up for the configured floor.
    // Zero by default, since the schedule above is already the minimum gap.
    if (wantDown && engaged && usingItem && minBlockTicks > 0 &&
        (int)(tick_ - blockedSinceTick_) < minBlockTicks)
        wantDown = false;

    if (wantDown)
        release(env, keyBind);
    else
        press(env, keyBind);

    env->DeleteLocalRef(keyBind);
}

std::string BlockHit::getDisplayInfo() {
    if (userOwnsKey_)
        return "Manual";
    if (serverBlocking_.load(std::memory_order_acquire))
        return "Blocking";
    if (engaged_.load(std::memory_order_acquire))
        return "Engaging";
    switch ((Mode)settings.mode) {
        case Mode::Hold:
            return "Hold";
        case Mode::Legit:
            return "Legit";
        default:
            return "Ready";
    }
}

void BlockHit::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ImGui::Spacing();
            const char* modes[] = {"Hold", "Perfect", "Legit"};
            ui::Combo("Mode", &settings.mode, modes, IM_ARRAYSIZE(modes));
            ui::Binder("Switch Mode Key", &settings.modeKeybind);
            ui::Checkbox("Only While Attacking", &settings.onlyWhileAttacking);
        },
        "Tuning",
        [this]() {
            ui::SliderInt("Release Ticks", &settings.releaseTicks, 1, 6, "%d");
            ui::SliderInt("Min Block Ticks", &settings.minBlockTicks, 0, 10, "%d");
            ui::Checkbox("Predictive", &settings.predictive);
            ui::Checkbox("Skip On Block Target", &settings.skipOnBlockTarget);
        });
}

void BlockHit::saveSettings(json::Value& node) const {
    node["mode"] = settings.mode;
    node["modeKeybind"] = settings.modeKeybind;
    node["releaseTicks"] = settings.releaseTicks;
    node["minBlockTicks"] = settings.minBlockTicks;
    node["predictive"] = settings.predictive;
    node["onlyWhileAttacking"] = settings.onlyWhileAttacking;
    node["skipOnBlockTarget"] = settings.skipOnBlockTarget;
}

void BlockHit::loadSettings(const json::Value& node) {
    settings.mode = std::clamp(node["mode"].as_int(settings.mode), 0, 2);
    settings.modeKeybind = node["modeKeybind"].as_int(settings.modeKeybind);
    settings.releaseTicks = node["releaseTicks"].as_int(settings.releaseTicks);
    settings.minBlockTicks = node["minBlockTicks"].as_int(settings.minBlockTicks);
    settings.predictive = node["predictive"].as_bool(settings.predictive);
    settings.onlyWhileAttacking = node["onlyWhileAttacking"].as_bool(settings.onlyWhileAttacking);
    settings.skipOnBlockTarget = node["skipOnBlockTarget"].as_bool(settings.skipOnBlockTarget);
}
