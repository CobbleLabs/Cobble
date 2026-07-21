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

}  // namespace

ThrowPots::ThrowPots() : Module("ThrowPots") {
    keybind = 0;
}

void ThrowPots::resetTransientState() {
    state_ = State::Idle;
    plan_.clear();
    planIndex_ = 0;
    target_ = {};
    successfulThrows_ = 0;
    retryCount_ = 0;
    pitchModified_ = false;
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
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
    if (!buildPlan() || !selectNextPotion())
        finish(true);
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
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::shuffle(available.begin(), available.end(), random);
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
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);
    player.setCurrentSlot(target_.slot);
    state_ = State::SwitchDelay;
    stateStart_ = std::chrono::steady_clock::now();
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
    rotationSent_.store(false, std::memory_order_release);
    rotationRequested_.store(true, std::memory_order_release);
    state_ = State::AwaitRotation;
    stateStart_ = std::chrono::steady_clock::now();

    if (settings.aim == ThrowPotsAim::Down) {
        player.setPitch(90.0f);
        player.setPrevPitch(90.0f);
        mc.updateMouseOver();
        pitchModified_ = true;
    }
}

void ThrowPots::queueUse() {
    useDispatched_.store(false, std::memory_order_release);
    useSucceeded_.store(false, std::memory_order_release);
    pendingUse_.store(true, std::memory_order_release);
    state_ = State::AwaitUseFlush;
    stateStart_ = std::chrono::steady_clock::now();
}

void ThrowPots::retryOrRestore() {
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);

    if (settings.retryOnFail && retryCount_ < kMaxRetries && targetPotionPresent(true)) {
        ++retryCount_;
        player.setCurrentSlot(target_.slot);
        state_ = State::SwitchDelay;
        stateStart_ = std::chrono::steady_clock::now();
        return;
    }
    beginRestore();
}

void ThrowPots::beginRestore() {
    pendingUse_.store(false, std::memory_order_release);
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);
    if (!settings.switchBack) {
        finish(true);
        return;
    }
    state_ = State::RestoreDelay;
    stateStart_ = std::chrono::steady_clock::now();
}

void ThrowPots::finish(bool restorePlayerState) {
    pendingUse_.store(false, std::memory_order_release);
    useDispatched_.store(false, std::memory_order_release);
    rotationRequested_.store(false, std::memory_order_release);
    rotationSent_.store(false, std::memory_order_release);

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
    plan_.clear();
    target_ = {};
    originalSlot_ = -1;
    pitchModified_ = false;
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

        case State::SwitchDelay:
            if (elapsed < settings.switchDelay)
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

        case State::AwaitRotation:
            aimYaw_.store(player.getYaw(), std::memory_order_release);
            if (rotationSent_.load(std::memory_order_acquire))
                queueUse();
            else if (elapsed >= kPacketTimeoutMs)
                retryOrRestore();
            break;

        case State::AwaitUseFlush:
            if (useDispatched_.load(std::memory_order_acquire)) {
                pendingUse_.store(false, std::memory_order_release);
                rotationRequested_.store(false, std::memory_order_release);
                rotationSent_.store(false, std::memory_order_release);
                if (!useSucceeded_.load(std::memory_order_acquire)) {
                    retryOrRestore();
                    return;
                }
                state_ = State::Verify;
                stateStart_ = now;
            } else if (elapsed >= kPacketTimeoutMs) {
                retryOrRestore();
            }
            break;

        case State::Verify:
            if (elapsed < settings.throwDelay)
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
            if (elapsed < settings.switchDelay)
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
        env->SetFloatField(packet, tpPitchField, 90.0f);
        env->SetBooleanField(packet, tpRotatingField, JNI_TRUE);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
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
        "Throwing",
        [this]() {
            static const char* aims[] = {"Current", "Down", "Silent Down"};
            int aim = static_cast<int>(settings.aim);
            if (ui::Combo("Aim", &aim, aims, IM_ARRAYSIZE(aims)))
                settings.aim = static_cast<ThrowPotsAim>(aim);

            ui::SliderInt("Switch Delay (ms)", &settings.switchDelay, 0, 250, "%d");
            ui::SliderInt("Throw Delay (ms)", &settings.throwDelay, 0, 500, "%d");
            ui::Checkbox("Switch Back", &settings.switchBack);
            ui::Checkbox("Retry on Fail", &settings.retryOnFail);
        });
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
}
