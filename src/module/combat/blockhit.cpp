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
    if (!fg)
        return false;
    // Compare process IDs, not window handles. Lunar and borderless-fullscreen can
    // hand the foreground to a different top-level window inside the same process,
    // and Overlay::gameWindow is only ever one of them -- an HWND equality test then
    // reads as "unfocused" forever and silently disables the whole schedule. This is
    // the same test the keybind dispatcher already trusts
    // (ModuleManager::isMinecraftFocused).
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid != 0 && fgPid == GetCurrentProcessId();
}

const char* modeName(BlockHit::Mode mode) {
    switch (mode) {
        case BlockHit::Mode::Aggressive:
            return "Aggressive";
        case BlockHit::Mode::Trade:
            return "Trade";
        case BlockHit::Mode::Defensive:
            return "Defensive";
        case BlockHit::Mode::Turtle:
            return "Turtle";
        default:
            return "Adaptive";
    }
}

// A cadence outside this range is not a fight (or is a measurement artifact) and must
// never anchor the schedule. 1 tick = 50 ms, so 40 ticks = 2 s.
constexpr float kMinIntervalTicks = 0.5f;
constexpr std::uint32_t kMaxIntervalTicks = 40;

// How long a click keeps "an attack is happening" true. It has to outlast the gap
// between two clicks -- at 10 CPS that is 100 ms -- while still noticing that the
// fight ended within half a second.
constexpr std::uint64_t kAttackActiveWindowMs = 400;

// The anti-stutter floors. Neither is a preference: two ticks up is exactly what the
// sprint rule needs (the key has to be up when onLivingUpdate evaluates sprinting at
// A-1, which means writing it up at A-2), and three ticks down is the shortest block
// worth a C08 out and a C07 back. Together they cap the key at one state change per
// five ticks in the very worst case, whatever a mode asks for.
constexpr std::uint32_t kMinBlockedRunTicks = 3;
constexpr std::uint32_t kMinReleasedRunTicks = 2;

// Trade only carves a sprint gap out of a block that has already been up this long,
// so the trade is uptime it has actually banked rather than a block that never
// existed. Jittered by up to two ticks on top, because a fixed period is the one
// thing about a block pattern that no human produces.
constexpr std::uint32_t kTradeRunBeforeGap = 6;
// Aggressive blocks only when its own next swing is at least this far out. Four ticks
// is the smallest lull that fits a three-tick block plus the tick it takes to raise
// it; anything tighter and the block would be costing a swing, which is the one thing
// this mode will not do.
constexpr std::uint32_t kAggressiveLullTicks = 4;

// A hit taken means someone is genuinely on us, and stays true this long afterwards.
// 40 ticks = 2 s, which is about one full exchange.
constexpr std::uint32_t kPressureTicks = 40;

// "The cadence model has nothing to say." Deliberately huge so every `<=` test
// against it is false and every `>=` test is true without a special case.
constexpr std::uint32_t kNoAttackDue = 0xFFFFFFFFu;

// Adaptive's two thresholds, as a fraction of max health.
constexpr float kLowHealth = 0.30f;
constexpr float kHurtHealth = 0.55f;

}  // namespace

BlockHit::BlockHit() : Module("BlockHit"), rng_((unsigned)nowMs()) {
    keybind = 0;
}

void BlockHit::onEnable() {
    prevAttackDown_ = false;
    observedAutoClick_ =
        autoClicker ? autoClicker->lastAutoClick : std::chrono::steady_clock::time_point{};
    observedAuraAttackMs_ = killAura ? killAura->lastAttackMs() : 0;
    attackSignals_.store(0, std::memory_order_release);
    attackHeld_.store(false, std::memory_order_release);
    lastAttackSignalMs_.store(0, std::memory_order_release);
    serverBlocking_.store(false, std::memory_order_release);
    gateReason_.store(nullptr, std::memory_order_release);
    activeMode_.store(nullptr, std::memory_order_release);
    prevHurtTime_ = 0;
    haveTakenHit_ = false;
    lastHitTakenTick_ = 0;
    gapTicksLeft_ = 0;
    resetCadence();
}

void BlockHit::onDisable() {
    // No JNI from the module thread. needsClientTick() stays true while engaged_ is
    // set, so the next client tick performs the release for us.
    gapTicksLeft_ = 0;
    gateReason_.store(nullptr, std::memory_order_release);
    activeMode_.store(nullptr, std::memory_order_release);
}
void BlockHit::resetCadence() {
    attackIntervalTicks_ = 0.0f;
    haveAttackHistory_ = false;
    lastAttackTick_ = 0;
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
            settings.mode = (settings.mode + 1) % ((int)Mode::Adaptive + 1);
            NotificationManager::addSystem(std::string("BlockHit: ") +
                                           modeName((Mode)settings.mode));
        }
        prevModeKey_ = down;
    }

    if (!enabled)
        return;

    // Attack edges. Vanilla only attacks an entity on a fresh keyBindAttack press
    // (Minecraft.runTick drains the queued presses through clickMouse), so an edge is
    // a one-to-one stand-in for an outgoing C02 ATTACK.
    const bool attackDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && gameFocused();
    if (attackDown && !prevAttackDown_) {
        attackSignals_.fetch_add(1, std::memory_order_acq_rel);
        lastAttackSignalMs_.store(nowMs(), std::memory_order_release);
    }
    prevAttackDown_ = attackDown;
    attackHeld_.store(attackDown, std::memory_order_release);

    if (autoClicker && autoClicker->lastAutoClick != observedAutoClick_) {
        observedAutoClick_ = autoClicker->lastAutoClick;
        attackSignals_.fetch_add(1, std::memory_order_acq_rel);
        lastAttackSignalMs_.store(nowMs(), std::memory_order_release);
    }
}
// ---------------------------------------------------------------------------
// What the fight looks like this tick. Everything the strategy decides comes from
// these four reads and nothing else -- no settings, no timers.
// ---------------------------------------------------------------------------

bool BlockHit::attackSourceActive() const {
    // The attack button being down is the start of an exchange, before any click has
    // resolved -- without this the first hit of every fight would go unblocked.
    if (attackHeld_.load(std::memory_order_acquire))
        return true;
    // Clicks that actually landed, from either source. This used to be
    // `autoClicker->enabled`, which is a statement about the module being armed and not
    // about anything happening: AutoClicker is normally left on and only clicks while
    // LMB is held, so that test made the whole gate a no-op and the block sat up
    // permanently whenever a sword was in hand -- blocking with nothing to hit.
    const std::uint64_t last = lastAttackSignalMs_.load(std::memory_order_acquire);
    if (last != 0 && nowMs() - last <= kAttackActiveWindowMs)
        return true;
    // KillAura only rewrites rotation while it actually holds a target, which makes it
    // a precise "a fight is happening" signal rather than just "the module is on".
    if (killAura && killAura->isRewritingRotation())
        return true;
    return false;
}

float BlockHit::healthFraction() const {
    const float health = player.getHealth();
    const float maxHealth = player.getMaxHealth();
    // getHealth() returns 0 when the method could not be resolved, and a player who is
    // actually at zero is already gated out by isValid(). Reading an unresolvable field
    // as "dying" would pin Adaptive to Defensive for the whole session, so unknown
    // health is full health.
    if (health <= 0.0f || maxHealth <= 0.0f)
        return 1.0f;
    return std::clamp(health / maxHealth, 0.0f, 1.0f);
}

bool BlockHit::damageImmune() const {
    // attackEntityFrom sets hurtResistantTime to maxHurtResistantTime on every hit that
    // lands and Entity.onEntityUpdate has already decremented it by the time this hook
    // runs. Above half, a same-strength follow-up is discarded outright, so a block
    // would halve nothing while still killing the sprint. Unreadable field -> 0 -> never
    // immune, which errs towards blocking more rather than less.
    const int resist = player.getHurtResistantTime();
    if (resist <= 0)
        return false;
    return resist > player.getMaxHurtResistantTime() / 2;
}

bool BlockHit::underPressure() const {
    return haveTakenHit_ && (tick_ - lastHitTakenTick_) <= kPressureTicks;
}
// Attack ticks come from a phase-locked model -- anchor + round(interval * k) -- rather
// than a single "next attack" estimate. Rounding a fractional interval reproduces the
// real beat of a steady clicker (1.67 ticks yields +2, +3, +5, +7, +8, +10, which is
// exactly what 12 CPS looks like on a 20 TPS clock), and anchoring on the last observed
// attack means a missed prediction rolls into the next slot instead of drifting.
std::uint32_t BlockHit::ticksUntilOurAttack() const {
    if (!haveAttackHistory_ || attackIntervalTicks_ <= 0.0f)
        return kNoAttackDue;
    if (tick_ - lastAttackTick_ > kMaxIntervalTicks)
        return kNoAttackDue;  // the fight went quiet; the anchor is stale

    // Sub-tick cadences cannot be scheduled -- every tick carries an attack -- and
    // clamping to one tick makes the walk below return 1 immediately, which correctly
    // reads as "there is no lull here" everywhere it is used.
    const float interval = std::max(attackIntervalTicks_, 1.0f);
    float slot = std::floor((float)(tick_ - lastAttackTick_) / interval);
    if (slot < 1.0f)
        slot = 1.0f;
    for (int guard = 0; guard < 32; ++guard, slot += 1.0f) {
        const std::uint32_t attackTick =
            lastAttackTick_ + (std::uint32_t)std::lround(interval * slot);
        if (attackTick > tick_)
            return attackTick - tick_;
    }
    return kNoAttackDue;
}

// ---------------------------------------------------------------------------
// The strategy. Two functions: which intent applies, and what that intent wants.
// ---------------------------------------------------------------------------

BlockHit::Mode BlockHit::resolveMode() const {
    const float hp = healthFraction();
    const bool pressure = underPressure();
    // Low and being hit: nothing matters except how much of the next hit lands.
    if (pressure && hp <= kLowHealth)
        return Mode::Defensive;
    // Hurt: the fight is real and both halves of it matter.
    if (hp <= kHurtHealth)
        return Mode::Trade;
    // Healthy and untouched: whatever is happening, we are winning it. Do not spend a
    // single sprint hit on a block that nothing is testing.
    if (!pressure)
        return Mode::Aggressive;
    return Mode::Trade;
}

bool BlockHit::wantsBlock(Mode effective) const {
    if (effective == Mode::Turtle)
        return true;  // the whole point: no release, ever, while the fight is on
    // The one rule. Inside the immunity window a block cannot reduce anything, so
    // holding it is pure movement and knockback given away.
    if (damageImmune())
        return false;
    if (effective == Mode::Aggressive)
        return ticksUntilOurAttack() >= kAggressiveLullTicks;
    return true;  // Trade and Defensive: any tick a hit can land is a tick to block
}
// ---------------------------------------------------------------------------
// Interactive-block detection. rightClickMouse() runs onPlayerRightClick first, and
// ItemSword does not override onItemUse, so a sword falls through to sendUseItem and
// the block goes up -- with one exception. If the crosshair is on a block whose own
// onBlockActivated consumes the click, that runs instead and the sword never raises,
// so the press is a wasted C08 and, worse, opens a chest in the middle of a fight.
// This used to be a "Skip On Block Target" checkbox that had to be off to be usable
// (in an arena the crosshair is on terrain most of the time, and refusing to block
// there is what made the module look broken) and had to be on to be safe. It is not a
// preference -- it is a property of the block being looked at, so it is now read
// rather than configured.
// ---------------------------------------------------------------------------

bool BlockHit::isInteractiveBlockId(int blockId) {
    switch (blockId) {
        case 23:   // dispenser
        case 25:   // note block
        case 26:   // bed
        case 54:   // chest
        case 58:   // crafting table
        case 61:   // furnace
        case 62:   // lit furnace
        case 64:   // oak door
        case 69:   // lever
        case 71:   // iron door
        case 77:   // stone button
        case 84:   // jukebox
        case 92:   // cake
        case 93:   // unpowered repeater
        case 94:   // powered repeater
        case 96:   // trapdoor
        case 107:  // oak fence gate
        case 116:  // enchanting table
        case 117:  // brewing stand
        case 118:  // cauldron
        case 130:  // ender chest
        case 137:  // command block
        case 138:  // beacon
        case 143:  // wooden button
        case 145:  // anvil
        case 146:  // trapped chest
        case 149:  // unpowered comparator
        case 150:  // powered comparator
        case 151:  // daylight detector
        case 154:  // hopper
        case 158:  // dropper
        case 167:  // iron trapdoor
        case 178:  // inverted daylight detector
        case 183:  // spruce fence gate
        case 184:  // birch fence gate
        case 185:  // jungle fence gate
        case 186:  // dark oak fence gate
        case 187:  // acacia fence gate
        case 193:  // spruce door
        case 194:  // birch door
        case 195:  // jungle door
        case 196:  // acacia door
        case 197:  // dark oak door
            return true;
        default:
            return false;
    }
}
bool BlockHit::crosshairWouldInteract() const {
    if (!mc.isTargetingBlock())
        return false;
    const Minecraft::Vec3 hit = mc.getTargetedBlockPos();
    if (!std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z))
        return false;
    const int id = mc.getBlockId((int)std::floor(hit.x), (int)std::floor(hit.y),
                                 (int)std::floor(hit.z));
    return isInteractiveBlockId(id);
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
    // of waiting for rightClickDelayTimer to reach 0 -- up to 4 ticks after the previous
    // use, which would stall every re-block past the window it was aimed at.
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
    releasedSinceTick_ = tick_;
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
        releasedSinceTick_ = tick_;
    }
    serverBlocking_.store(false, std::memory_order_release);
    gapTicksLeft_ = 0;
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

    // Drain the attack latch first: it is needed below, and a bail-out must not leave
    // stale signals to be replayed on re-entry.
    std::uint32_t attacks = attackSignals_.exchange(0, std::memory_order_acq_rel);
    // KillAura fires inside this same hook and is dispatched ahead of us, so its attack
    // for THIS tick is already visible here.
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
                const float measured = std::max((float)span / (float)attacks, kMinIntervalTicks);
                attackIntervalTicks_ = attackIntervalTicks_ <= 0.0f
                                           ? measured
                                           : attackIntervalTicks_ * 0.6f + measured * 0.4f;
            }
        }
        lastAttackTick_ = tick_;
        haveAttackHistory_ = true;
    } else if (haveAttackHistory_ && tick_ - lastAttackTick_ > kMaxIntervalTicks) {
        // Two seconds without a swing: the anchor is worthless and a stale schedule
        // would open gaps at random.
        resetCadence();
    }

    // Hits taken, from hurtTime's rising edge. attackEntityFrom sets it to maxHurtTime
    // (10) on every hit that actually lands and Entity.onEntityUpdate counts it back
    // down, so a rise is one incoming hit and nothing else -- no packet reading, no
    // entity tracking, and it cannot be desynced from the immunity window because both
    // are set by the same method on the same tick.
    const int hurt = player.getHurtTime();
    if (hurt > prevHurtTime_) {
        lastHitTakenTick_ = tick_;
        haveTakenHit_ = true;
    }
    prevHurtTime_ = hurt;

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
            gapTicksLeft_ = 0;
        }
        userOwnsKey_ = true;
        serverBlocking_.store(true, std::memory_order_release);
        return;
    }
    if (userOwnsKey_) {
        // Fresh start after the user let go: their mouse-up already cleared pressed.
        userOwnsKey_ = false;
        releasedSinceTick_ = tick_;
        gapTicksLeft_ = 0;
    }

    // AutoRod owns the use-item channel while a sequence is unwinding; letting BlockHit
    // re-arm between rod phases produces a zero-tick press/release and corrupts both
    // modules. isSequencing() is the whole test -- it stays true after the module is
    // toggled off until the sequence finishes. Gating on autoRod->enabled as well made
    // BlockHit a no-op for the entire session whenever AutoRod was merely armed, which
    // is its normal state.
    //
    // "Idle" is what used to be the Only While Attacking setting. It is not optional any
    // more, and it is wider than the old test was: an exchange is running if we are
    // swinging OR if something has hit us in the last two seconds, so being jumped
    // raises the block before we have clicked once -- which the old setting got wrong in
    // exactly the situation that costs a game.
    //
    // Each bail-out records WHY, so a standing-down module says so in the ArrayList
    // instead of showing a cheerful mode name while doing nothing.
    const char* gate = nullptr;
    if (!player.isValid())
        gate = "No Player";
    else if (Menu::isOpen || mc.isInGui())
        gate = "GUI";
    else if (!gameFocused())
        gate = "Unfocused";
    else if (autoRod && autoRod->isSequencing())
        gate = "AutoRod";
    else if (!isHoldingSword())
        gate = "No Sword";
    else if (crosshairWouldInteract())
        gate = "Interact";
    else if (!attackSourceActive() && !underPressure())
        gate = "Idle";

    gateReason_.store(gate, std::memory_order_release);
    if (gate) {
        handBackKey(env);
        return;
    }
    jobject keyBind = useItemKeyBind(env);
    if (!keyBind)
        return;

    // Someone else (or a stale frame) cleared pressed while we thought we owned it.
    // Re-sync rather than trusting our own flag.
    if (engaged_.load(std::memory_order_acquire) && !readPressed(env, keyBind)) {
        engaged_.store(false, std::memory_order_release);
        releasedSinceTick_ = tick_;
    }

    // Only the game can tell us whether the block actually took effect; used for the HUD.
    bool usingItem = false;
    if (player.mid_isUsingItem && player.instance) {
        usingItem = env->CallBooleanMethod(player.instance, player.mid_isUsingItem) == JNI_TRUE;
        if (clearEx(env))
            usingItem = false;
    }
    const bool engaged = engaged_.load(std::memory_order_acquire);
    serverBlocking_.store(usingItem && engaged, std::memory_order_release);

    // ---- The decision. One question per stage, one JNI write at the end. ----

    const Mode selected = (Mode)std::clamp(settings.mode, 0, (int)Mode::Adaptive);
    const Mode effective = selected == Mode::Adaptive ? resolveMode() : selected;
    activeMode_.store(modeName(effective), std::memory_order_release);

    bool wantBlock = wantsBlock(effective);

    // The sprint gap, and only Trade pays for it. Carving it out of a block that does
    // not exist yet is what made the old schedule collapse: the gap was due on nearly
    // every tick, so the key spent the fight up and the block was never there to open.
    if (gapTicksLeft_ > 0) {
        --gapTicksLeft_;
        wantBlock = false;
    } else if (effective == Mode::Trade && wantBlock && engaged) {
        const std::uint32_t runFloor = kTradeRunBeforeGap + (std::uint32_t)(rng_() % 3u);
        if ((tick_ - blockedSinceTick_) >= runFloor && ticksUntilOurAttack() <= 2u) {
            // Two ticks wide: this one and the next, so the key is up for A-2 and A-1 and
            // back down inside runTick A, after the hit has already been thrown.
            gapTicksLeft_ = kMinReleasedRunTicks - 1;
            wantBlock = false;
        }
    }
    // The floors, applied last so nothing above can talk its way past them. This is the
    // whole answer to the stutter: a block that just went down stays down for three
    // ticks, and a key that just came up stays up for two, so the worst case any mode
    // can produce is one C08/C07 pair per five ticks instead of one per two.
    if (engaged) {
        if (!wantBlock && (tick_ - blockedSinceTick_) < kMinBlockedRunTicks)
            wantBlock = true;
    } else if (wantBlock && (tick_ - releasedSinceTick_) < kMinReleasedRunTicks) {
        wantBlock = false;
    }

    if (wantBlock)
        press(env, keyBind);
    else
        release(env, keyBind);

    env->DeleteLocalRef(keyBind);
}

std::string BlockHit::getDisplayInfo() {
    if (userOwnsKey_)
        return "Manual";
    // A recorded gate outranks the mode: "No Sword" or "AutoRod" tells you why nothing
    // is happening, where a mode name would just look like a working module.
    if (const char* gate = gateReason_.load(std::memory_order_acquire))
        return gate;
    const char* mode = activeMode_.load(std::memory_order_acquire);
    if (!mode)
        mode = modeName((Mode)std::clamp(settings.mode, 0, (int)Mode::Adaptive));
    // Which intent is live, then what it is doing with the key right now. With Adaptive
    // selected the first half is the interesting one -- it is the only way to see the
    // strategy change under you.
    if (serverBlocking_.load(std::memory_order_acquire))
        return std::string(mode) + " / Block";
    if (engaged_.load(std::memory_order_acquire))
        return std::string(mode) + " / Raise";
    return std::string(mode) + " / Sprint";
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
            const char* modes[] = {"Aggressive", "Trade", "Defensive", "Turtle", "Adaptive"};
            ui::Combo("Mode", &settings.mode, modes, IM_ARRAYSIZE(modes));
        },
        "Binds",
        [this]() { ui::Binder("Switch Mode Key", &settings.modeKeybind); });
}
// The mode is persisted under a new key. The old one held Hold/Perfect/Legit, whose
// numbers now mean Aggressive/Trade/Defensive -- reading them back would silently hand
// a Hold user the mode that blocks least. There is no way to tell an old 2 from a new
// one, so the legacy key is simply left where it is and anyone upgrading lands on
// Adaptive, which is both the new default and the better answer than whatever they had.
void BlockHit::saveSettings(json::Value& node) const {
    node["strategy"] = settings.mode;
    node["modeKeybind"] = settings.modeKeybind;
}

void BlockHit::loadSettings(const json::Value& node) {
    settings.mode =
        std::clamp(node["strategy"].as_int((int)Mode::Adaptive), 0, (int)Mode::Adaptive);
    settings.modeKeybind = node["modeKeybind"].as_int(settings.modeKeybind);
}
