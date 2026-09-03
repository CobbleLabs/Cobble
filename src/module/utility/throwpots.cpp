#include "throwpots.h"

#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#include <algorithm>
#include <cmath>
#include <random>

ThrowPots* throwPots = nullptr;

namespace {

jclass tpC03Class = nullptr;
jfieldID tpYawField = nullptr;
jfieldID tpPitchField = nullptr;
jfieldID tpRotatingField = nullptr;
bool tpC03Resolved = false;

void resolveC03(JNIEnv* env) {
    if (tpC03Resolved)
        return;
    tpC03Resolved = true;

    jclass local = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", local))
        return;

    tpC03Class = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);

    tpYawField = env->GetFieldID(tpC03Class, "yaw", "F");
    if (!tpYawField) {
        env->ExceptionClear();
        tpYawField = env->GetFieldID(tpC03Class, "field_149476_e", "F");
    }
    tpPitchField = env->GetFieldID(tpC03Class, "pitch", "F");
    if (!tpPitchField) {
        env->ExceptionClear();
        tpPitchField = env->GetFieldID(tpC03Class, "field_149473_f", "F");
    }
    tpRotatingField = env->GetFieldID(tpC03Class, "rotating", "Z");
    if (!tpRotatingField) {
        env->ExceptionClear();
        tpRotatingField = env->GetFieldID(tpC03Class, "field_149480_h", "Z");
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

bool isStrongHealingPotion(int damage) {
    return (damage & 0x20) != 0;
}

// Cubic-free smoothstep: enough to kill the linear-ramp look without the
// overshoot a back-ease would add, which on a pitch sweep would read as a
// jitter past the floor and back.
float smoothstep(float t) {
    t = (std::clamp)(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

ThrowPots::ThrowPots() : Module("ThrowPots") {
    keybind = 0;
    rng_.seed(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
              (reinterpret_cast<uint64_t>(this) * 0x9E3779B97F4A7C15ull));
}

void ThrowPots::applyPreset(ThrowPotsPreset preset) {
    settings.preset = static_cast<int>(preset);
    switch (preset) {
        case ThrowPotsPreset::Instant:
            // The old flat behaviour, kept reachable on purpose: fastest possible
            // burst, zero humanization.
            settings.switchDelay = 0;
            settings.throwDelay = 40;
            settings.reactionMs = 0;
            settings.jitterPercent = 0;
            settings.fatigue = false;
            settings.pitchJitter = 0;
            settings.aimSmoothMs = 0;
            break;
        case ThrowPotsPreset::Fast:
            settings.switchDelay = 30;
            settings.throwDelay = 60;
            settings.reactionMs = 55;
            settings.jitterPercent = 18;
            settings.fatigue = false;
            settings.pitchJitter = 2;
            settings.aimSmoothMs = 50;
            break;
        case ThrowPotsPreset::Human:
            settings.switchDelay = 55;
            settings.throwDelay = 90;
            settings.reactionMs = 120;
            settings.jitterPercent = 30;
            settings.fatigue = true;
            settings.pitchJitter = 4;
            settings.aimSmoothMs = 90;
            break;
        case ThrowPotsPreset::Careful:
            settings.switchDelay = 95;
            settings.throwDelay = 150;
            settings.reactionMs = 220;
            settings.jitterPercent = 50;
            settings.fatigue = true;
            settings.pitchJitter = 7;
            settings.aimSmoothMs = 150;
            break;
        case ThrowPotsPreset::Custom:
        default:
            break;
    }
}

int64_t ThrowPots::jitter(int baseMs) const {
    return jitter(baseMs, settings.jitterPercent);
}

// Two uniform draws averaged: a soft bell, so most delays land near the base and
// the tails are rare instead of a flat spread that is itself a signature.
int64_t ThrowPots::jitter(int baseMs, int spreadPercent) const {
    if (baseMs <= 0)
        return 0;
    const int spread = (std::clamp)(spreadPercent, 0, 100);
    if (spread == 0)
        return baseMs;
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const float noise = (dist(rng_) + dist(rng_)) * 0.5f;
    const float scaled = static_cast<float>(baseMs) * (1.0f + noise * (static_cast<float>(spread) / 100.0f));
    return static_cast<int64_t>(std::lround((std::max)(0.0f, scaled)));
}

// A real burst slows down as it runs; the fourth pot never comes as fast as the
// first. Capped so a nine-pot Dynamic burst does not crawl.
float ThrowPots::fatigueScale() const {
    if (!settings.fatigue || successfulThrows_ <= 0)
        return 1.0f;
    return 1.0f + 0.07f * static_cast<float>((std::min)(successfulThrows_, 5));
}

float ThrowPots::pickAimPitch() const {
    const float base = static_cast<float>((std::clamp)(settings.aimPitch, 0, 90));
    if (settings.pitchJitter <= 0)
        return base;
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const float noise = (dist(rng_) + dist(rng_)) * 0.5f;
    return (std::clamp)(base + noise * static_cast<float>(settings.pitchJitter), -90.0f, 90.0f);
}

void ThrowPots::enterState(State next, int64_t delayMs) {
    state_ = next;
    stateStart_ = std::chrono::steady_clock::now();
    stateDelay_ = delayMs > 0 ? delayMs : 0;
}

void ThrowPots::resetTransientState() {
    state_ = State::Idle;
    stateDelay_ = 0;
    plan_.clear();
    planIndex_ = 0;
    target_ = {};
    successfulThrows_ = 0;
    retryCount_ = 0;
    pitchModified_ = false;
    aimSweepMs_ = 0;
    restoreSweepMs_ = 0;
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    aimFinal_.store(false, std::memory_order_release);
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);
}

void ThrowPots::onEnable() {
    resetTransientState();
    if (Menu::isOpen || mc.isInGui() || !player.isValid() || player.getHealth() <= 0.0f) {
        enabled = false;
        return;
    }

    originalSlot_ = player.getCurrentSlot();
    originalPitch_ = player.getPitch();

    // Fail fast when there is nothing to throw, so the keybind does not light the
    // module up for a reaction delay and then quietly give up. The plan is rebuilt
    // once the reaction elapses, because health can change in the meantime.
    if (!buildPlan()) {
        finish(true);
        return;
    }
    enterState(State::Reacting, jitter(settings.reactionMs));
}

void ThrowPots::onDisable() {
    finish(true);
}

void ThrowPots::onWorldChange() {
    finish(false);
}

bool ThrowPots::buildPlan() {
    std::vector<PlannedPotion> available;
    for (const auto& slot : mc.getInventorySlots()) {
        if (slot.slotId < 0 || slot.slotId > 8 || slot.itemId != kPotionItemId || !slot.isSplashPotion ||
            !slot.isHealthPotion || slot.stackSize <= 0) {
            continue;
        }
        const bool strong = isStrongHealingPotion(slot.damage);
        if (!settings.allowHealthI && !strong)
            continue;
        available.push_back({slot.slotId, slot.damage, slot.stackSize, strong ? 8 : 4});
    }
    if (available.empty())
        return false;

    if (settings.randomSlots) {
        std::shuffle(available.begin(), available.end(), rng_);
    } else {
        std::stable_sort(available.begin(), available.end(), [](const PlannedPotion& left, const PlannedPotion& right) {
            return left.healAmount > right.healAmount;
        });
    }

    const float maxHealth = player.getMaxHealth();
    healTarget_ = maxHealth * (static_cast<float>(settings.targetHealthPercent) / 100.0f);
    const float missingHealth = healTarget_ - player.getHealth();
    if (missingHealth <= 0.0f)
        return false;

    size_t desiredCount = 1;
    if (settings.mode == ThrowPotsMode::Double) {
        desiredCount = static_cast<size_t>((std::min)(2, static_cast<int>(std::ceil(missingHealth / 8.0f))));
    } else if (settings.mode == ThrowPotsMode::Dynamic) {
        float remaining = missingHealth;
        desiredCount = 0;
        for (const auto& potion : available) {
            if (desiredCount >= static_cast<size_t>(settings.maxThrows) || remaining <= 0.0f)
                break;
            remaining -= static_cast<float>(potion.healAmount);
            ++desiredCount;
        }
    }

    desiredCount = (std::min)(desiredCount, available.size());
    plan_.assign(available.begin(), available.begin() + desiredCount);
    return !plan_.empty();
}

bool ThrowPots::selectNextPotion() {
    if (!player.isValid() || planIndex_ >= plan_.size())
        return false;
    if (settings.mode == ThrowPotsMode::Dynamic && player.getHealth() >= healTarget_)
        return false;

    target_ = plan_[planIndex_];
    bool valid = false;
    for (const auto& slot : mc.getInventorySlots()) {
        if (slot.slotId == target_.slot && slot.itemId == kPotionItemId && slot.isSplashPotion &&
            slot.isHealthPotion && slot.damage == target_.damage && slot.stackSize > 0) {
            target_.stackSize = slot.stackSize;
            valid = true;
            break;
        }
    }
    if (!valid)
        return false;

    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    aimFinal_.store(false, std::memory_order_release);
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);
    player.setCurrentSlot(target_.slot);
    enterState(State::SwitchDelay,
               static_cast<int64_t>(std::lround(static_cast<float>(jitter(settings.switchDelay)) * fatigueScale())));
    return true;
}

bool ThrowPots::targetPotionPresent(bool requireOriginalStackSize) const {
    for (const auto& slot : mc.getInventorySlots()) {
        if (slot.slotId != target_.slot)
            continue;
        if (slot.itemId != kPotionItemId || !slot.isSplashPotion || !slot.isHealthPotion ||
            slot.damage != target_.damage) {
            return false;
        }
        return !requireOriginalStackSize || slot.stackSize >= target_.stackSize;
    }
    return false;
}

void ThrowPots::requestDownRotation() {
    aimYaw_.store(player.getYaw(), std::memory_order_release);
    aimStartPitch_ = player.getPitch();
    aimEndPitch_ = pickAimPitch();
    aimSweepMs_ = settings.aimSmoothMs > 0 ? jitter(settings.aimSmoothMs, 25) : 0;
    // A sweep shorter than a couple of ticks reaches the server as a single jump
    // anyway, so collapse it rather than pay for the extra states.
    if (aimSweepMs_ < 20)
        aimSweepMs_ = 0;

    const float initialPitch = aimSweepMs_ > 0 ? aimStartPitch_ : aimEndPitch_;
    aimPitch_.store(initialPitch, std::memory_order_release);
    aimFinal_.store(aimSweepMs_ <= 0, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    rotationRequested_.store(true, std::memory_order_release);
    enterState(State::AwaitRotation, aimSweepMs_);

    if (settings.aim == ThrowPotsAim::Down) {
        player.setPitch(initialPitch);
        player.setPrevPitch(initialPitch);
        pitchModified_ = true;
        // While sweeping, the raytrace is refreshed once at the end instead of on
        // every step: updateMouseOver off the client thread is the expensive and
        // risky part, and a splash potion does not need a block under the cursor.
        if (aimSweepMs_ <= 0)
            mc.updateMouseOver();
    }
}

void ThrowPots::queueUse() {
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);
    pendingUse_.store(true, std::memory_order_release);
    enterState(State::AwaitUseFlush, 0);
}

void ThrowPots::retryOrRestore() {
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    aimFinal_.store(false, std::memory_order_release);

    if (settings.retryOnFail && retryCount_ < kMaxRetries && targetPotionPresent(true)) {
        ++retryCount_;
        player.setCurrentSlot(target_.slot);
        // A person who notices a throw did not land pauses a beat longer than
        // normal before trying again, so a retry never reads as a machine repeat.
        enterState(State::SwitchDelay, jitter(settings.switchDelay + 45, 40));
        return;
    }
    beginRestore();
}

void ThrowPots::beginRestore() {
    pendingUse_.store(false, std::memory_order_release);
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    aimFinal_.store(false, std::memory_order_release);
    if (!settings.switchBack) {
        finish(true);
        return;
    }

    int64_t wait = jitter(settings.switchDelay);
    restoreSweepMs_ = 0;
    if (pitchModified_ && settings.aimSmoothMs > 0) {
        // Come back up the same way we went down. rotationRequested_ is already
        // false here, so the gate is out of the way and the real player pitch is
        // what reaches the server.
        restoreSweepMs_ = jitter(settings.aimSmoothMs, 25);
        if (wait < restoreSweepMs_)
            wait = restoreSweepMs_;
    }
    enterState(State::RestoreDelay, wait);
}

void ThrowPots::finish(bool restorePlayerState) {
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    aimFinal_.store(false, std::memory_order_release);

    if (restorePlayerState && player.isValid()) {
        if (pitchModified_) {
            player.setPitch(originalPitch_);
            player.setPrevPitch(originalPitch_);
            mc.updateMouseOver();
        }
        if (settings.switchBack && originalSlot_ >= 0 && originalSlot_ <= 8)
            player.setCurrentSlot(originalSlot_);
    }

    state_ = State::Idle;
    stateDelay_ = 0;
    plan_.clear();
    target_ = {};
    originalSlot_ = -1;
    pitchModified_ = false;
    aimSweepMs_ = 0;
    restoreSweepMs_ = 0;
    enabled = false;
}

void ThrowPots::onUpdate() {
    if (state_ == State::Idle)
        return;
    if (Menu::isOpen || mc.isInGui() || !player.isValid() || player.getHealth() <= 0.0f) {
        finish(true);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStart_).count();

    switch (state_) {
        case State::Idle:
            break;

        case State::Reacting:
            if (elapsed < stateDelay_)
                return;
            // Rebuilt here rather than reused from onEnable: health, and the
            // hotbar, can both move during the reaction window.
            if (!buildPlan() || !selectNextPotion())
                finish(true);
            break;

        case State::SwitchDelay:
            if (elapsed < stateDelay_)
                return;
            if (player.getCurrentSlot() != target_.slot || !targetPotionPresent(true)) {
                retryOrRestore();
                return;
            }
            if (settings.aim == ThrowPotsAim::Current)
                queueUse();
            else
                requestDownRotation();
            break;

        case State::AwaitRotation: {
            aimYaw_.store(player.getYaw(), std::memory_order_release);

            bool arrived = true;
            if (aimSweepMs_ > 0) {
                const float progress =
                    (std::clamp)(static_cast<float>(elapsed) / static_cast<float>(aimSweepMs_), 0.0f, 1.0f);
                const float pitch = aimStartPitch_ + (aimEndPitch_ - aimStartPitch_) * smoothstep(progress);
                aimPitch_.store(pitch, std::memory_order_release);
                if (settings.aim == ThrowPotsAim::Down) {
                    player.setPitch(pitch);
                    player.setPrevPitch(pitch);
                }
                arrived = progress >= 1.0f;
                if (arrived) {
                    aimFinal_.store(true, std::memory_order_release);
                    if (settings.aim == ThrowPotsAim::Down)
                        mc.updateMouseOver();
                    aimSweepMs_ = 0;  // one refresh only, however long we wait for the gate
                }
            }

            if (arrived && rotationSent_.load(std::memory_order_acquire))
                queueUse();
            else if (elapsed >= kPacketTimeoutMs)
                retryOrRestore();
            break;
        }

        case State::AwaitUseFlush:
            if (useDispatched_.load(std::memory_order_acquire)) {
                pendingUse_.store(false, std::memory_order_release);
                rotationRequested_.store(false, std::memory_order_release);
                rotationSent_.store(false, std::memory_order_release);
                aimFinal_.store(false, std::memory_order_release);
                if (!useSucceeded_.load(std::memory_order_acquire)) {
                    retryOrRestore();
                    return;
                }
                enterState(State::Verify, static_cast<int64_t>(std::lround(
                                              static_cast<float>(jitter(settings.throwDelay)) * fatigueScale())));
            } else if (elapsed >= kPacketTimeoutMs) {
                retryOrRestore();
            }
            break;

        case State::Verify:
            if (elapsed < stateDelay_)
                return;
            if (targetPotionPresent(true)) {
                retryOrRestore();
                return;
            }

            ++successfulThrows_;
            ++planIndex_;
            retryCount_ = 0;
            if (!selectNextPotion())
                beginRestore();
            break;

        case State::RestoreDelay:
            if (pitchModified_ && restoreSweepMs_ > 0) {
                const float progress =
                    (std::clamp)(static_cast<float>(elapsed) / static_cast<float>(restoreSweepMs_), 0.0f, 1.0f);
                const float pitch = aimEndPitch_ + (originalPitch_ - aimEndPitch_) * smoothstep(progress);
                player.setPitch(pitch);
                player.setPrevPitch(pitch);
            }
            if (elapsed < stateDelay_)
                return;
            finish(true);
            break;
    }
}

bool ThrowPots::needsOutgoingPacket() const {
    return rotationRequested_.load(std::memory_order_acquire) || pendingUse_.load(std::memory_order_acquire);
}

bool ThrowPots::isRewritingRotation() const {
    return rotationRequested_.load(std::memory_order_acquire);
}

void ThrowPots::onOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!enabled || !env || !packet || !needsOutgoingPacket())
        return;

    resolveC03(env);
    if (!tpC03Class || env->IsInstanceOf(packet, tpC03Class) != JNI_TRUE)
        return;

    const bool rewriteRotation = rotationRequested_.load(std::memory_order_acquire);
    if (rewriteRotation) {
        if (!tpYawField || !tpPitchField || !tpRotatingField)
            return;
        env->SetFloatField(packet, tpYawField, aimYaw_.load(std::memory_order_acquire));
        env->SetFloatField(packet, tpPitchField, aimPitch_.load(std::memory_order_acquire));
        env->SetBooleanField(packet, tpRotatingField, JNI_TRUE);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        // Mid-sweep packets carry an intermediate pitch and must NOT release the
        // use: the module only counts the rotation as delivered once the value it
        // published is the final one.
        if (!aimFinal_.load(std::memory_order_acquire))
            return;
        rotationSent_.store(true, std::memory_order_release);
    }

    if (pendingUse_.exchange(false, std::memory_order_acq_rel)) {
        const bool used = mc.sendUseHeldItem();
        useSucceeded_.store(used, std::memory_order_release);
        useDispatched_.store(true, std::memory_order_release);
    }
}

void ThrowPots::onRenderMenu() {
    ui::two_column(
        "Action",
        [this]() {
            ui::Binder("Keybind", &keybind);
            ImGui::Spacing();

            static const char* modes[] = {"Dynamic", "Single", "Double"};
            int mode = static_cast<int>(settings.mode);
            if (ui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes)))
                settings.mode = static_cast<ThrowPotsMode>(mode);

            ui::SliderInt("Target Health (%)", &settings.targetHealthPercent, 1, 100, "%d");
            if (settings.mode == ThrowPotsMode::Dynamic)
                ui::SliderInt("Max Throws", &settings.maxThrows, 1, 9, "%d");
            ui::Checkbox("Random Slots", &settings.randomSlots);
            ui::Checkbox("Allow Heal I", &settings.allowHealthI);
        },
        "Aim",
        [this]() {
            static const char* aims[] = {"Current", "Down", "Silent Down"};
            int aim = static_cast<int>(settings.aim);
            if (ui::Combo("Aim", &aim, aims, IM_ARRAYSIZE(aims)))
                settings.aim = static_cast<ThrowPotsAim>(aim);

            if (settings.aim != ThrowPotsAim::Current) {
                bool touched = false;
                ui::SliderInt("Aim Pitch", &settings.aimPitch, 45, 90, "%d");
                touched |= ui::SliderInt("Pitch Jitter", &settings.pitchJitter, 0, 15, "%d");
                touched |= ui::SliderInt("Aim Smoothing (ms)", &settings.aimSmoothMs, 0, 250, "%d");
                if (touched)
                    settings.preset = static_cast<int>(ThrowPotsPreset::Custom);
            }

            ui::Checkbox("Switch Back", &settings.switchBack);
            ui::Checkbox("Retry on Fail", &settings.retryOnFail);
        });

    ui::two_column(
        "Timing",
        [this]() {
            static const char* presets[] = {"Instant", "Fast", "Human", "Careful", "Custom"};
            int preset = (std::clamp)(settings.preset, 0, 4);
            if (ui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets))) {
                if (preset == static_cast<int>(ThrowPotsPreset::Custom))
                    settings.preset = preset;
                else
                    applyPreset(static_cast<ThrowPotsPreset>(preset));
            }

            bool touched = false;
            touched |= ui::SliderInt("Reaction (ms)", &settings.reactionMs, 0, 500, "%d");
            touched |= ui::SliderInt("Switch Delay (ms)", &settings.switchDelay, 0, 250, "%d");
            touched |= ui::SliderInt("Throw Delay (ms)", &settings.throwDelay, 0, 500, "%d");
            if (touched)
                settings.preset = static_cast<int>(ThrowPotsPreset::Custom);
        },
        "Humanization",
        [this]() {
            bool touched = false;
            touched |= ui::SliderInt("Jitter (%)", &settings.jitterPercent, 0, 100, "%d");
            touched |= ui::Checkbox("Fatigue", &settings.fatigue);
            if (touched)
                settings.preset = static_cast<int>(ThrowPotsPreset::Custom);

            ImGui::Spacing();
            ImGui::TextDisabled("Jitter spreads every delay; Fatigue widens the");
            ImGui::TextDisabled("gaps as the burst runs, like a tiring hand.");
        });

    ImGui::Spacing();
    switch (state_) {
        case State::Idle:
            ImGui::TextDisabled("Idle - press the keybind to throw once");
            break;
        case State::Reacting:
            ImGui::TextDisabled("Reacting...");
            break;
        default:
            ImGui::TextDisabled("Throwing - %d thrown", successfulThrows_);
            break;
    }
    if (settings.preset == static_cast<int>(ThrowPotsPreset::Instant))
        ImGui::TextDisabled("Instant: flat timing and an exact 90 pitch snap - the old, obvious behaviour.");
}

std::string ThrowPots::getDisplayInfo() {
    switch (settings.mode) {
        case ThrowPotsMode::Dynamic:
            return "Dynamic " + std::to_string(settings.targetHealthPercent) + "%";
        case ThrowPotsMode::Single:
            return "Single";
        case ThrowPotsMode::Double:
            return "Double";
    }
    return "";
}

void ThrowPots::saveSettings(json::Value& node) const {
    node["randomSlots"] = settings.randomSlots;
    node["mode"] = static_cast<int>(settings.mode);
    node["aim"] = static_cast<int>(settings.aim);
    node["targetHealthPercent"] = settings.targetHealthPercent;
    node["maxThrows"] = settings.maxThrows;
    node["throwDelay"] = settings.throwDelay;
    node["switchDelay"] = settings.switchDelay;
    node["allowHealthI"] = settings.allowHealthI;
    node["switchBack"] = settings.switchBack;
    node["retryOnFail"] = settings.retryOnFail;
    node["preset"] = settings.preset;
    node["reactionMs"] = settings.reactionMs;
    node["jitterPercent"] = settings.jitterPercent;
    node["fatigue"] = settings.fatigue;
    node["aimPitch"] = settings.aimPitch;
    node["pitchJitter"] = settings.pitchJitter;
    node["aimSmoothMs"] = settings.aimSmoothMs;
}

void ThrowPots::loadSettings(const json::Value& node) {
    settings.randomSlots = node["randomSlots"].as_bool(settings.randomSlots);
    const int storedMode = (std::max)(0, (std::min)(2, node["mode"].as_int(static_cast<int>(settings.mode))));
    if (!node.has("targetHealthPercent") && node.has("healthThreshold")) {
        // Migrate the first Cobble implementation: Single=0, Double=1,
        // HealToHealth=2 -> raid0source ordering Dynamic=0, Single=1, Double=2.
        settings.mode = storedMode == 0 ? ThrowPotsMode::Single
                                        : storedMode == 1 ? ThrowPotsMode::Double : ThrowPotsMode::Dynamic;
    } else {
        settings.mode = static_cast<ThrowPotsMode>(storedMode);
    }
    settings.aim = static_cast<ThrowPotsAim>(
        (std::max)(0, (std::min)(2, node["aim"].as_int(static_cast<int>(settings.aim)))));
    if (node.has("targetHealthPercent")) {
        settings.targetHealthPercent = node["targetHealthPercent"].as_int(settings.targetHealthPercent);
    } else if (node.has("healthThreshold")) {
        settings.targetHealthPercent =
            static_cast<int>(node["healthThreshold"].as_float(20.0f) * 5.0f);
    }
    settings.targetHealthPercent = (std::max)(1, (std::min)(100, settings.targetHealthPercent));
    settings.maxThrows = (std::max)(1, (std::min)(9, node["maxThrows"].as_int(settings.maxThrows)));
    settings.throwDelay = (std::max)(0, (std::min)(500, node["throwDelay"].as_int(settings.throwDelay)));
    settings.switchDelay = (std::max)(0, (std::min)(250, node["switchDelay"].as_int(settings.switchDelay)));
    settings.allowHealthI = node["allowHealthI"].as_bool(settings.allowHealthI);
    settings.switchBack = node["switchBack"].as_bool(settings.switchBack);
    settings.retryOnFail = node["retryOnFail"].as_bool(settings.retryOnFail);

    // Configs written before the humanization pass inherit the Human defaults
    // rather than being pinned to Instant: keeping the old flat timing silently
    // would defeat the point of the change, and Instant is still one click away.
    settings.preset = (std::max)(0, (std::min)(4, node["preset"].as_int(settings.preset)));
    settings.reactionMs = (std::max)(0, (std::min)(500, node["reactionMs"].as_int(settings.reactionMs)));
    settings.jitterPercent = (std::max)(0, (std::min)(100, node["jitterPercent"].as_int(settings.jitterPercent)));
    settings.fatigue = node["fatigue"].as_bool(settings.fatigue);
    settings.aimPitch = (std::max)(45, (std::min)(90, node["aimPitch"].as_int(settings.aimPitch)));
    settings.pitchJitter = (std::max)(0, (std::min)(15, node["pitchJitter"].as_int(settings.pitchJitter)));
    settings.aimSmoothMs = (std::max)(0, (std::min)(250, node["aimSmoothMs"].as_int(settings.aimSmoothMs)));
}
