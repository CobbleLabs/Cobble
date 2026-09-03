#include "autopot.h"

#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "throwpots.h"

#include <algorithm>
#include <cmath>

AutoPot* autoPot = nullptr;

namespace {

// Own copy of the C03 field handles. ThrowPots resolves the same class, but the
// two modules are independent and a shared cache would only save a few bytes.
jclass apC03Class = nullptr;
jfieldID apYawField = nullptr;
jfieldID apPitchField = nullptr;
jfieldID apRotatingField = nullptr;
bool apC03Resolved = false;

void resolveC03(JNIEnv* env) {
    if (apC03Resolved)
        return;
    apC03Resolved = true;

    jclass local = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", local))
        return;

    apC03Class = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);

    apYawField = env->GetFieldID(apC03Class, "yaw", "F");
    if (!apYawField) {
        env->ExceptionClear();
        apYawField = env->GetFieldID(apC03Class, "field_149476_e", "F");
    }
    apPitchField = env->GetFieldID(apC03Class, "pitch", "F");
    if (!apPitchField) {
        env->ExceptionClear();
        apPitchField = env->GetFieldID(apC03Class, "field_149473_f", "F");
    }
    apRotatingField = env->GetFieldID(apC03Class, "rotating", "Z");
    if (!apRotatingField) {
        env->ExceptionClear();
        apRotatingField = env->GetFieldID(apC03Class, "field_149480_h", "Z");
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

// Instant Health II carries bit 0x20 in its damage value and heals 8 HP; the
// plain version heals 4.
bool isStrongHealingPotion(int damage) {
    return (damage & 0x20) != 0;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// How long health has to sit near the threshold before the player counts as
// "already watching the bar" and reacts faster than from a standing start.
constexpr int64_t kPrimedAfterMs = 700;
constexpr float kPrimedReactionScale = 0.55f;

}  // namespace

AutoPot::AutoPot() : Module("AutoPot") {
    keybind = 0;
    rng_.seed(static_cast<uint64_t>(nowMs()) ^ (reinterpret_cast<uint64_t>(this) * 0x9E3779B97F4A7C15ull));
}

void AutoPot::applyPreset(AutoPotPreset preset) {
    settings.preset = static_cast<int>(preset);
    switch (preset) {
        case AutoPotPreset::Instant:
            // No human noise at all: the moment health crosses the line, the pot
            // is already in the air. The gap is not zero because the throw is
            // confirmed against the hotbar first either way.
            settings.reactionMs = 0;
            settings.reactionJitter = 0;
            settings.switchMs = 0;
            settings.throwGapMs = 60;
            settings.restoreMs = 0;
            settings.flickMs = 0;
            settings.misclickChance = 0;
            settings.fatigue = false;
            break;
        case AutoPotPreset::Rapid:
            settings.reactionMs = 55;
            settings.reactionJitter = 20;
            settings.switchMs = 20;
            settings.throwGapMs = 130;
            settings.restoreMs = 30;
            settings.flickMs = 40;
            settings.misclickChance = 0;
            settings.fatigue = false;
            break;
        case AutoPotPreset::Human:
            settings.reactionMs = 140;
            settings.reactionJitter = 45;
            settings.switchMs = 55;
            settings.throwGapMs = 210;
            settings.restoreMs = 90;
            settings.flickMs = 90;
            settings.misclickChance = 0;
            settings.fatigue = true;
            break;
        case AutoPotPreset::Legit:
            settings.reactionMs = 260;
            settings.reactionJitter = 70;
            settings.switchMs = 110;
            settings.throwGapMs = 340;
            settings.restoreMs = 170;
            settings.flickMs = 160;
            settings.misclickChance = 4;
            settings.fatigue = true;
            break;
        case AutoPotPreset::Custom:
            break;
    }
}

int64_t AutoPot::jitter(int baseMs) const {
    return jitter(baseMs, settings.reactionJitter);
}

int64_t AutoPot::jitter(int baseMs, int spreadPercent) const {
    if (baseMs <= 0)
        return 0;
    const int spread = (std::clamp)(spreadPercent, 0, 100);
    if (spread == 0)
        return baseMs;

    // Two uniform draws averaged give a soft bell instead of a flat band, so most
    // delays land near the base and the extremes stay rare -- which is what real
    // reaction times look like when you plot them.
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const float noise = (dist(rng_) + dist(rng_)) * 0.5f;
    const float scaled = static_cast<float>(baseMs) * (1.0f + noise * (static_cast<float>(spread) / 100.0f));
    return static_cast<int64_t>(std::lround((std::max)(0.0f, scaled)));
}

bool AutoPot::roll(int percent) const {
    if (percent <= 0)
        return false;
    if (percent >= 100)
        return true;
    std::uniform_int_distribution<int> dist(1, 100);
    return dist(rng_) <= percent;
}

float AutoPot::fatigueScale() const {
    if (!settings.fatigue || burstFatigue_ <= 0)
        return 1.0f;
    // Hands get a little slower as a fight drags on. Capped so a long game never
    // turns into a liability.
    return 1.0f + 0.055f * static_cast<float>((std::min)(burstFatigue_, 6));
}

bool AutoPot::pickPotion(PotionChoice& out) const {
    PotionChoice best{};
    bool found = false;
    int count = 0;

    for (const auto& slot : mc.getInventorySlots()) {
        if (slot.slotId < 0 || slot.slotId > 8)
            continue;  // hotbar only: reaching into the inventory is a different module
        if (slot.itemId != kPotionItemId || !slot.isSplashPotion || !slot.isHealthPotion || slot.stackSize <= 0)
            continue;

        const bool strong = isStrongHealingPotion(slot.damage);
        if (!strong && !settings.allowHealI)
            continue;

        count += slot.stackSize;
        PotionChoice choice{slot.slotId, slot.damage, slot.stackSize, strong ? 8.0f : 4.0f};
        if (!found) {
            best = choice;
            found = true;
            continue;
        }
        // Prefer Heal II to end the burst in one throw, or Heal I to keep the
        // strong ones for when they are actually needed.
        const bool better = settings.preferStrong ? (choice.heal > best.heal) : (choice.heal < best.heal);
        if (better)
            best = choice;
    }

    potsAvailable_.store(count, std::memory_order_relaxed);
    if (found)
        out = best;
    return found;
}

int AutoPot::countPotions() const {
    int count = 0;
    for (const auto& slot : mc.getInventorySlots()) {
        if (slot.slotId < 0 || slot.slotId > 8)
            continue;
        if (slot.itemId != kPotionItemId || !slot.isSplashPotion || !slot.isHealthPotion || slot.stackSize <= 0)
            continue;
        if (!settings.allowHealI && !isStrongHealingPotion(slot.damage))
            continue;
        count += slot.stackSize;
    }
    potsAvailable_.store(count, std::memory_order_relaxed);
    return count;
}

bool AutoPot::targetStillThere(bool requireFullStack) const {
    for (const auto& slot : mc.getInventorySlots()) {
        if (slot.slotId != target_.slot)
            continue;
        if (slot.itemId != kPotionItemId || !slot.isSplashPotion || !slot.isHealthPotion ||
            slot.damage != target_.damage) {
            return false;
        }
        return !requireFullStack || slot.stackSize >= target_.stackSize;
    }
    return false;
}

AutoPotAim AutoPot::effectiveAim() const {
    const AutoPotAim mode = static_cast<AutoPotAim>((std::clamp)(settings.aim, 0, 3));
    // If the C03 fields never resolved, silent aim would throw the potion at
    // whatever the camera happens to face. A real flick still works, so use that
    // instead of quietly wasting potions.
    if (mode == AutoPotAim::Silent && !silentAimOk_.load(std::memory_order_acquire))
        return AutoPotAim::Snap;
    return mode;
}

bool AutoPot::wantsPot(float predicted) const {
    if (maxHealth_ <= 0.0f)
        return false;
    const float trigger = maxHealth_ * (static_cast<float>(settings.healthPercent) / 100.0f);
    if (predicted > trigger)
        return false;
    if (settings.requireGround && !player.isOnGround())
        return false;
    if (settings.onlyInCombat && (lastDamageMs_ == 0 || nowMs() - lastDamageMs_ > settings.combatWindowMs))
        return false;
    return true;
}

void AutoPot::enterState(State next, int64_t delayMs) {
    state_ = next;
    stateStartMs_ = nowMs();
    stateDelayMs_ = delayMs > 0 ? delayMs : 0;
}

void AutoPot::resetSequence() {
    state_ = State::Idle;
    stateStartMs_ = nowMs();
    stateDelayMs_ = 0;
    target_ = {};
    originalSlot_ = -1;
    pitchModified_ = false;
    thrown_ = 0;
    retries_ = 0;
    misclickSlot_ = -1;
    inFlightHeal_ = 0.0f;
    inFlightUntilMs_ = 0;
    lowSinceMs_ = 0;
    burstFatigue_ = 0;
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);
}

void AutoPot::onEnable() {
    resetSequence();
    settings.preset = (std::clamp)(settings.preset, 0, 4);
    settings.aim = (std::clamp)(settings.aim, 0, 3);
    if (settings.targetPercent < settings.healthPercent)
        settings.targetPercent = settings.healthPercent;

    lastHealth_ = player.isValid() ? player.getHealth() : 20.0f;
    maxHealth_ = player.isValid() ? player.getMaxHealth() : 20.0f;
    if (maxHealth_ <= 0.0f)
        maxHealth_ = 20.0f;
    lastMaxHealthMs_ = nowMs();
    lastDamageMs_ = 0;
    potsAvailable_.store(-1, std::memory_order_relaxed);
}

void AutoPot::onDisable() {
    // Never leave the hand holding a potion because the module was toggled off
    // mid-burst.
    if (state_ != State::Idle)
        finishBurst();
    resetSequence();
    potsAvailable_.store(-1, std::memory_order_relaxed);
}

void AutoPot::onWorldChange() {
    resetSequence();
    potsAvailable_.store(-1, std::memory_order_relaxed);
}

void AutoPot::beginBurst() {
    originalSlot_ = player.getCurrentSlot();
    originalPitch_ = player.getPitch();
    pitchModified_ = false;
    thrown_ = 0;
    retries_ = 0;

    int64_t reaction = jitter(settings.reactionMs, settings.reactionJitter);
    // Someone who has been staring at a low health bar for a moment moves before
    // someone caught at full health does.
    if (lowSinceMs_ != 0 && nowMs() - lowSinceMs_ >= kPrimedAfterMs)
        reaction = static_cast<int64_t>(static_cast<float>(reaction) * kPrimedReactionScale);
    reaction = static_cast<int64_t>(static_cast<float>(reaction) * fatigueScale());
    enterState(State::Arming, reaction);
}

void AutoPot::beginSwitch(int slot) {
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);

    misclickSlot_ = -1;
    int first = slot;
    if (roll(settings.misclickChance)) {
        // The classic fumble: a neighbouring hotbar key, noticed, then corrected.
        // Costs one extra switch delay and nothing else.
        const int dir = roll(50) ? -1 : 1;
        int wrong = slot + dir;
        if (wrong < 0 || wrong > 8)
            wrong = slot - dir;
        if (wrong >= 0 && wrong <= 8 && wrong != slot) {
            first = wrong;
            misclickSlot_ = wrong;
        }
    }

    player.setCurrentSlot(first);
    int64_t delay = jitter(settings.switchMs);
    if (misclickSlot_ >= 0)
        delay += jitter(settings.switchMs);  // the wrong slot is held a beat longer
    enterState(State::Switching, static_cast<int64_t>(static_cast<float>(delay) * fatigueScale()));
}

void AutoPot::beginAim() {
    float pitch = static_cast<float>(settings.pitch);
    if (settings.pitchJitter > 0) {
        // Nobody hits the same pitch twice. A couple of degrees is invisible in the
        // splash radius but very visible in a rotation log.
        std::uniform_real_distribution<float> dist(-static_cast<float>(settings.pitchJitter),
                                                   static_cast<float>(settings.pitchJitter));
        pitch += dist(rng_);
    }
    aimPitch_ = (std::clamp)(pitch, 45.0f, 90.0f);

    switch (effectiveAim()) {
        case AutoPotAim::Smooth:
            if (settings.flickMs > 0) {
                flickFrom_ = player.getPitch();
                enterState(State::Aiming, static_cast<int64_t>(static_cast<float>(jitter(settings.flickMs)) *
                                                               fatigueScale()));
                return;
            }
            [[fallthrough]];
        case AutoPotAim::Snap:
            player.setPitch(aimPitch_);
            player.setPrevPitch(aimPitch_);
            mc.updateMouseOver();
            pitchModified_ = true;
            break;
        case AutoPotAim::Silent:
        case AutoPotAim::None:
            break;
    }
    queueUse();
}

void AutoPot::queueUse() {
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);

    if (effectiveAim() == AutoPotAim::Silent) {
        // Arm the rewrite and the use together: the gate writes the pitch onto the
        // flying packet and enqueues the item use behind it in one pass, so the
        // server sees "looking down" then "use item" with no wasted tick.
        aimYaw_.store(player.getYaw(), std::memory_order_release);
        aimPitchPacket_.store(aimPitch_, std::memory_order_release);
        rotationSent_.store(false, std::memory_order_release);
        rotationRequested_.store(true, std::memory_order_release);
    }

    pendingUse_.store(true, std::memory_order_release);
    enterState(State::Throwing, kUseTimeoutMs);
}

void AutoPot::failStep() {
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);

    if (settings.retryOnFail && retries_ < kMaxRetries && targetStillThere(true)) {
        ++retries_;
        beginSwitch(target_.slot);
        return;
    }
    beginRestore();
}

void AutoPot::beginRestore() {
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    pendingUse_.store(false, std::memory_order_release);

    const bool needsSlot = settings.switchBack && originalSlot_ >= 0 && originalSlot_ <= 8;
    if (!needsSlot && !pitchModified_) {
        finishBurst();
        return;
    }
    enterState(State::Restoring, static_cast<int64_t>(static_cast<float>(jitter(settings.restoreMs)) * fatigueScale()));
}

void AutoPot::finishBurst() {
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);

    if (player.isValid()) {
        if (pitchModified_) {
            player.setPitch(originalPitch_);
            player.setPrevPitch(originalPitch_);
            mc.updateMouseOver();
        }
        if (settings.switchBack && originalSlot_ >= 0 && originalSlot_ <= 8)
            player.setCurrentSlot(originalSlot_);
    }

    pitchModified_ = false;
    originalSlot_ = -1;
    target_ = {};
    thrown_ = 0;
    retries_ = 0;
    misclickSlot_ = -1;
    state_ = State::Idle;
    stateStartMs_ = nowMs();
    stateDelayMs_ = 0;
    // inFlightHeal_ deliberately survives: the heal is still on its way and the
    // next trigger check has to know about it.
}

void AutoPot::onUpdate() {
    const int64_t now = nowMs();
    // Idle is the always-on path, so throttle it before touching the JVM at all.
    if (state_ == State::Idle && now - lastPollMs_ < kPollIntervalMs)
        return;
    lastPollMs_ = now;

    const bool valid = player.isValid();
    const float health = valid ? player.getHealth() : 0.0f;
    // ThrowPots owns the hand while it runs; two modules driving the hotbar at
    // once would fight over the slot and waste potions.
    const bool blocked =
        !valid || health <= 0.0f || Menu::isOpen || mc.isInGui() || (throwPots && throwPots->enabled);
    if (blocked) {
        if (state_ != State::Idle)
            finishBurst();
        lowSinceMs_ = 0;
        // Keep the settings page honest about the hotbar while it is being read.
        if (valid && Menu::isOpen && now - lastUiScanMs_ >= 1000) {
            lastUiScanMs_ = now;
            countPotions();
        }
        return;
    }

    if (now - lastMaxHealthMs_ >= 500) {
        const float reported = player.getMaxHealth();
        maxHealth_ = reported > 0.0f ? reported : 20.0f;
        lastMaxHealthMs_ = now;
    }

    if (health < lastHealth_ - 0.01f)
        lastDamageMs_ = now;

    // Consume the inbound-heal credit as the server actually reports it, so a burst
    // that only half landed does not keep pretending the whole heal is pending.
    if (inFlightHeal_ > 0.0f) {
        if (health < healthAtThrow_) {
            healthAtThrow_ = health;  // took a hit mid-flight: measure from the new floor
        } else if (health >= healthAtThrow_ + 0.5f) {
            inFlightHeal_ = (std::max)(0.0f, inFlightHeal_ - (health - healthAtThrow_));
            healthAtThrow_ = health;
        }
        if (now >= inFlightUntilMs_)
            inFlightHeal_ = 0.0f;  // it missed, or the heal was cancelled by damage
    }
    lastHealth_ = health;

    const float predicted = health + inFlightHeal_;

    // "Already watching the bar" tracking, used to shorten the reaction delay.
    const float watchLevel =
        maxHealth_ * (static_cast<float>((std::min)(100, settings.healthPercent + 20)) / 100.0f);
    if (predicted <= watchLevel) {
        if (lowSinceMs_ == 0)
            lowSinceMs_ = now;
    } else {
        lowSinceMs_ = 0;
    }

    if (burstFatigue_ > 0 && lastThrowMs_ != 0 && now - lastThrowMs_ > 8000)
        burstFatigue_ = 0;

    const int64_t elapsed = now - stateStartMs_;
    const float healTarget = maxHealth_ * (static_cast<float>(settings.targetPercent) / 100.0f);

    switch (state_) {
        case State::Idle: {
            if (!wantsPot(predicted)) {
                lastScanMs_ = 0;  // so the next real trigger gets to scan immediately
                break;
            }
            if (lastScanMs_ != 0 && now - lastScanMs_ < kScanIntervalMs)
                break;
            lastScanMs_ = now;
            PotionChoice probe{};
            if (pickPotion(probe))
                beginBurst();
            break;
        }

        case State::Arming:
            if (elapsed < stateDelayMs_)
                break;
            // Health can recover during the reaction window. Nobody throws a potion
            // they stopped needing halfway through reaching for it.
            if (!wantsPot(predicted) || !pickPotion(target_)) {
                finishBurst();
                break;
            }
            beginSwitch(target_.slot);
            break;

        case State::Switching:
            if (elapsed < stateDelayMs_)
                break;
            if (misclickSlot_ >= 0) {
                // Noticed the wrong slot; correct it and take the gap again.
                misclickSlot_ = -1;
                player.setCurrentSlot(target_.slot);
                enterState(State::Switching, jitter(settings.switchMs));
                break;
            }
            if (player.getCurrentSlot() != target_.slot || !targetStillThere(true)) {
                failStep();
                break;
            }
            beginAim();
            break;

        case State::Aiming: {
            // Smooth mode only: ease the real pitch down the way a mouse pull does.
            const float t = stateDelayMs_ <= 0 ? 1.0f
                                               : (std::clamp)(static_cast<float>(elapsed) /
                                                                  static_cast<float>(stateDelayMs_),
                                                              0.0f, 1.0f);
            const float eased = t * t * (3.0f - 2.0f * t);
            const float current = flickFrom_ + (aimPitch_ - flickFrom_) * eased;
            player.setPitch(current);
            player.setPrevPitch(current);
            pitchModified_ = true;
            if (t >= 1.0f) {
                mc.updateMouseOver();
                queueUse();
            }
            break;
        }

        case State::Throwing:
            if (useDispatched_.load(std::memory_order_acquire)) {
                pendingUse_.store(false, std::memory_order_release);
                rotationRequested_.store(false, std::memory_order_release);
                rotationSent_.store(false, std::memory_order_release);
                if (!useSucceeded_.load(std::memory_order_acquire)) {
                    failStep();
                    break;
                }
                enterState(State::Confirming, kConfirmTimeoutMs);
                break;
            }
            if (elapsed >= stateDelayMs_)
                failStep();
            break;

        case State::Confirming: {
            // The hotbar stack dropping is the only honest proof the throw happened.
            // Polled, not assumed, but not every 6 ms either.
            bool didThrow = false;
            if (now - lastScanMs_ >= 20) {
                lastScanMs_ = now;
                didThrow = !targetStillThere(true);
            }
            if (!didThrow) {
                if (elapsed >= stateDelayMs_)
                    failStep();
                break;
            }

            ++thrown_;
            retries_ = 0;
            lastThrowMs_ = now;
            ++burstFatigue_;
            inFlightHeal_ += target_.heal;
            healthAtThrow_ = health;
            inFlightUntilMs_ = now + settings.inFlightMs;

            if (health + inFlightHeal_ >= healTarget || thrown_ >= settings.maxPots) {
                beginRestore();
                break;
            }
            enterState(State::Cadence,
                       static_cast<int64_t>(static_cast<float>(jitter(settings.throwGapMs)) * fatigueScale()));
            break;
        }

        case State::Cadence:
            if (elapsed < stateDelayMs_)
                break;
            // A heal that landed early cancels the rest of the burst.
            if (predicted >= healTarget || thrown_ >= settings.maxPots || !pickPotion(target_)) {
                beginRestore();
                break;
            }
            beginSwitch(target_.slot);
            break;

        case State::Restoring:
            if (elapsed < stateDelayMs_)
                break;
            finishBurst();
            break;
    }
}

bool AutoPot::needsOutgoingPacket() const {
    return rotationRequested_.load(std::memory_order_acquire) || pendingUse_.load(std::memory_order_acquire);
}

bool AutoPot::isRewritingRotation() const {
    return rotationRequested_.load(std::memory_order_acquire);
}

void AutoPot::onOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!enabled || !env || !packet || !needsOutgoingPacket())
        return;

    resolveC03(env);
    if (!apC03Class) {
        silentAimOk_.store(false, std::memory_order_release);
        return;
    }
    if (env->IsInstanceOf(packet, apC03Class) != JNI_TRUE)
        return;

    if (rotationRequested_.load(std::memory_order_acquire)) {
        if (!apYawField || !apPitchField || !apRotatingField) {
            // Silent aim cannot work without these; the next burst flicks for real.
            silentAimOk_.store(false, std::memory_order_release);
            return;
        }
        env->SetFloatField(packet, apYawField, aimYaw_.load(std::memory_order_acquire));
        env->SetFloatField(packet, apPitchField, aimPitchPacket_.load(std::memory_order_acquire));
        env->SetBooleanField(packet, apRotatingField, JNI_TRUE);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            silentAimOk_.store(false, std::memory_order_release);
            return;
        }
        rotationSent_.store(true, std::memory_order_release);
    }

    if (pendingUse_.exchange(false, std::memory_order_acq_rel)) {
        const bool used = mc.sendUseHeldItem();
        useSucceeded_.store(used, std::memory_order_release);
        useDispatched_.store(true, std::memory_order_release);
    }
}

void AutoPot::onRenderMenu() {
    ui::two_column(
        "Trigger",
        [this]() {
            bool enabledValue = enabled;
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ImGui::Spacing();

            ui::SliderInt("Pot Below (%)", &settings.healthPercent, 5, 95);
            ui::SliderInt("Heal Until (%)", &settings.targetPercent, 10, 100);
            if (settings.targetPercent < settings.healthPercent)
                settings.targetPercent = settings.healthPercent;
            ui::SliderInt("Max Pots", &settings.maxPots, 1, 9);
            ui::Checkbox("Prefer Heal II", &settings.preferStrong);
            ui::Checkbox("Allow Heal I", &settings.allowHealI);
            ui::Checkbox("Only On Ground", &settings.requireGround);
            ui::Checkbox("Only In Combat", &settings.onlyInCombat);
            if (settings.onlyInCombat)
                ui::SliderInt("Combat Window (ms)", &settings.combatWindowMs, 500, 10000);
        },
        "Timing",
        [this]() {
            static const char* presets[] = {"Instant", "Rapid", "Human", "Legit", "Custom"};
            int preset = (std::clamp)(settings.preset, 0, 4);
            if (ui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets)))
                applyPreset(static_cast<AutoPotPreset>(preset));

            // Touching any of these means the label no longer describes the timing,
            // so the combo drops to Custom rather than lying about it.
            bool touched = false;
            touched |= ui::SliderInt("Reaction (ms)", &settings.reactionMs, 0, 600);
            touched |= ui::SliderInt("Jitter (%)", &settings.reactionJitter, 0, 100);
            touched |= ui::SliderInt("Switch (ms)", &settings.switchMs, 0, 300);
            touched |= ui::SliderInt("Throw Gap (ms)", &settings.throwGapMs, 0, 600);
            touched |= ui::SliderInt("Restore (ms)", &settings.restoreMs, 0, 400);
            touched |= ui::Checkbox("Fatigue", &settings.fatigue);
            if (touched)
                settings.preset = static_cast<int>(AutoPotPreset::Custom);
        });

    ui::two_column(
        "Aim",
        [this]() {
            static const char* aims[] = {"Silent", "Snap", "Smooth", "None"};
            int aim = (std::clamp)(settings.aim, 0, 3);
            if (ui::Combo("Aim", &aim, aims, IM_ARRAYSIZE(aims)))
                settings.aim = aim;

            ui::SliderInt("Pitch", &settings.pitch, 45, 90);
            ui::SliderInt("Pitch Jitter", &settings.pitchJitter, 0, 10);
            if (settings.aim == static_cast<int>(AutoPotAim::Smooth)) {
                if (ui::SliderInt("Flick Time (ms)", &settings.flickMs, 0, 400))
                    settings.preset = static_cast<int>(AutoPotPreset::Custom);
            }
            ui::Checkbox("Switch Back", &settings.switchBack);
        },
        "Reliability",
        [this]() {
            ui::Checkbox("Retry On Fail", &settings.retryOnFail);
            ui::SliderInt("Heal Credit (ms)", &settings.inFlightMs, 100, 1500);
            if (ui::SliderInt("Misclick Chance (%)", &settings.misclickChance, 0, 25))
                settings.preset = static_cast<int>(AutoPotPreset::Custom);
        });

    ImGui::Spacing();

    const int pots = potsAvailable_.load(std::memory_order_relaxed);
    if (!enabled) {
        ImGui::TextDisabled("Off");
    } else if (state_ != State::Idle) {
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.45f, 1.0f), "Potting - %d thrown this burst", thrown_);
    } else if (pots == 0) {
        ImGui::TextColored(ImVec4(0.90f, 0.55f, 0.30f, 1.0f), "No splash healing potions in the hotbar");
    } else if (pots > 0) {
        ImGui::TextDisabled("Watching - %d potion%s ready", pots, pots == 1 ? "" : "s");
    } else {
        ImGui::TextDisabled("Watching");
    }

    if (inFlightHeal_ > 0.0f)
        ImGui::TextDisabled("Heal already in the air: %.0f HP", inFlightHeal_);
    if (settings.aim == static_cast<int>(AutoPotAim::Silent) && !silentAimOk_.load(std::memory_order_acquire))
        ImGui::TextColored(ImVec4(0.90f, 0.55f, 0.30f, 1.0f), "Silent aim unavailable - flicking for real instead");
}

std::string AutoPot::getDisplayInfo() {
    static const char* names[] = {"Instant", "Rapid", "Human", "Legit", "Custom"};
    const int idx = (std::clamp)(settings.preset, 0, 4);
    return std::string(names[idx]) + " " + std::to_string(settings.healthPercent) + "%";
}

void AutoPot::saveSettings(json::Value& node) const {
    node["healthPercent"] = settings.healthPercent;
    node["targetPercent"] = settings.targetPercent;
    node["maxPots"] = settings.maxPots;
    node["allowHealI"] = settings.allowHealI;
    node["preferStrong"] = settings.preferStrong;
    node["requireGround"] = settings.requireGround;
    node["onlyInCombat"] = settings.onlyInCombat;
    node["combatWindowMs"] = settings.combatWindowMs;
    node["preset"] = settings.preset;
    node["reactionMs"] = settings.reactionMs;
    node["reactionJitter"] = settings.reactionJitter;
    node["switchMs"] = settings.switchMs;
    node["throwGapMs"] = settings.throwGapMs;
    node["restoreMs"] = settings.restoreMs;
    node["fatigue"] = settings.fatigue;
    node["aim"] = settings.aim;
    node["pitch"] = settings.pitch;
    node["pitchJitter"] = settings.pitchJitter;
    node["flickMs"] = settings.flickMs;
    node["switchBack"] = settings.switchBack;
    node["retryOnFail"] = settings.retryOnFail;
    node["inFlightMs"] = settings.inFlightMs;
    node["misclickChance"] = settings.misclickChance;
}

void AutoPot::loadSettings(const json::Value& node) {
    settings.healthPercent = (std::clamp)(node["healthPercent"].as_int(settings.healthPercent), 5, 95);
    settings.targetPercent = (std::clamp)(node["targetPercent"].as_int(settings.targetPercent), 10, 100);
    if (settings.targetPercent < settings.healthPercent)
        settings.targetPercent = settings.healthPercent;
    settings.maxPots = (std::clamp)(node["maxPots"].as_int(settings.maxPots), 1, 9);
    settings.allowHealI = node["allowHealI"].as_bool(settings.allowHealI);
    settings.preferStrong = node["preferStrong"].as_bool(settings.preferStrong);
    settings.requireGround = node["requireGround"].as_bool(settings.requireGround);
    settings.onlyInCombat = node["onlyInCombat"].as_bool(settings.onlyInCombat);
    settings.combatWindowMs = (std::clamp)(node["combatWindowMs"].as_int(settings.combatWindowMs), 500, 10000);
    settings.preset = (std::clamp)(node["preset"].as_int(settings.preset), 0, 4);
    settings.reactionMs = (std::clamp)(node["reactionMs"].as_int(settings.reactionMs), 0, 600);
    settings.reactionJitter = (std::clamp)(node["reactionJitter"].as_int(settings.reactionJitter), 0, 100);
    settings.switchMs = (std::clamp)(node["switchMs"].as_int(settings.switchMs), 0, 300);
    settings.throwGapMs = (std::clamp)(node["throwGapMs"].as_int(settings.throwGapMs), 0, 600);
    settings.restoreMs = (std::clamp)(node["restoreMs"].as_int(settings.restoreMs), 0, 400);
    settings.fatigue = node["fatigue"].as_bool(settings.fatigue);
    settings.aim = (std::clamp)(node["aim"].as_int(settings.aim), 0, 3);
    settings.pitch = (std::clamp)(node["pitch"].as_int(settings.pitch), 45, 90);
    settings.pitchJitter = (std::clamp)(node["pitchJitter"].as_int(settings.pitchJitter), 0, 10);
    settings.flickMs = (std::clamp)(node["flickMs"].as_int(settings.flickMs), 0, 400);
    settings.switchBack = node["switchBack"].as_bool(settings.switchBack);
    settings.retryOnFail = node["retryOnFail"].as_bool(settings.retryOnFail);
    settings.inFlightMs = (std::clamp)(node["inFlightMs"].as_int(settings.inFlightMs), 100, 1500);
    settings.misclickChance = (std::clamp)(node["misclickChance"].as_int(settings.misclickChance), 0, 25);
}
