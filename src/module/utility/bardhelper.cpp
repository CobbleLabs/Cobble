#include "bardhelper.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#include <algorithm>

BardHelper* bardHelper = nullptr;

BardHelper::BardHelper() : Module("BardHelper") {
    std::random_device rd;
    rng_.seed(rd());
}

int BardHelper::getEffectForItemId(int itemId) const {
    switch (itemId) {
        case 265:  // Iron Ingot (Resistance)
            return settings.resistance ? 0 : -1;
        case 377:  // Blaze Powder (Strength)
            return settings.strength ? 1 : -1;
        case 370:  // Ghast Tear (Regeneration)
            return settings.regeneration ? 2 : -1;
        case 353:  // Sugar (Speed)
            return settings.speed ? 3 : -1;
        case 288:  // Feather (Jump Boost)
            return settings.jump ? 4 : -1;
        case 378:  // Magma Cream (Fire Resistance)
            return settings.fireResistance ? 5 : -1;
        case 375:  // Spider Eye (Debuff)
            return settings.debuff ? 6 : -1;
        default:
            return -1;
    }
}

std::chrono::milliseconds BardHelper::getDelayWithJitter(int baseMs) {
    if (settings.randomization <= 0)
        return std::chrono::milliseconds((std::max)(10, baseMs));

    std::uniform_int_distribution<int> dist(-settings.randomization, settings.randomization);
    int jitter = dist(rng_);
    int finalMs = (std::max)(10, baseMs + jitter);
    return std::chrono::milliseconds(finalMs);
}

bool BardHelper::buildPlan() {
    plan_.clear();
    if (!player.isValid())
        return false;

    unsigned usedEffects = 0;
    const auto slots = mc.getInventorySlots();

    // Check hotbar slots (0 to 8) from getInventorySlots()
    for (const auto& slot : slots) {
        if (slot.slotId < 0 || slot.slotId > 8 || slot.stackSize <= 0)
            continue;

        int effect = getEffectForItemId(slot.itemId);
        if (effect < 0)
            continue;

        if ((usedEffects & (1u << effect)) != 0)
            continue;

        usedEffects |= (1u << effect);
        plan_.push_back({slot.slotId, effect, slot.itemId});
    }

    // Fallback if getInventorySlots returned empty
    if (plan_.empty()) {
        for (int i = 0; i < 9; ++i) {
            int itemId = player.getItemInSlot(i);
            if (itemId <= 0)
                continue;

            int effect = getEffectForItemId(itemId);
            if (effect < 0 || (usedEffects & (1u << effect)) != 0)
                continue;

            usedEffects |= (1u << effect);
            plan_.push_back({i, effect, itemId});
        }
    }

    return !plan_.empty();
}

void BardHelper::applyTarget(size_t index) {
    if (index >= plan_.size() || !player.isValid())
        return;

    int slot = plan_[index].slot;
    if (slot < 0 || slot > 8)
        return;

    player.setCurrentSlot(slot);

    if (settings.clickBuff) {
        mc.rightClickMouse();
    }
}

void BardHelper::finish(bool restoreSlot) {
    if (restoreSlot && settings.switchBack && originalSlot_ >= 0 && originalSlot_ <= 8 && player.isValid()) {
        player.setCurrentSlot(originalSlot_);
    }

    state_ = State::Idle;
    plan_.clear();
    currentTargetIndex_ = 0;
    originalSlot_ = -1;
    active_ = false;
    enabled = false;
}

void BardHelper::onEnable() {
    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        finish(false);
        return;
    }

    originalSlot_ = player.getCurrentSlot();
    active_ = true;
    currentTargetIndex_ = 0;

    if (!buildPlan()) {
        if (settings.mode == static_cast<int>(BardMode::Single)) {
            finish(false);
            return;
        }
        state_ = State::BetweenCycles;
        nextAction_ = std::chrono::steady_clock::now() + getDelayWithJitter(settings.loopDelay);
        return;
    }

    state_ = State::Holding;
    applyTarget(0);
    nextAction_ = std::chrono::steady_clock::now() + getDelayWithJitter(settings.switchDelay);
}

void BardHelper::onDisable() {
    finish(true);
}

void BardHelper::onWorldChange() {
    finish(false);
}

void BardHelper::onUpdate() {
    if (!enabled)
        return;

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        if (settings.mode == static_cast<int>(BardMode::Single) && active_) {
            finish(true);
        }
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < nextAction_)
        return;

    if (state_ == State::Holding) {
        currentTargetIndex_++;
        if (currentTargetIndex_ < plan_.size()) {
            applyTarget(currentTargetIndex_);
            nextAction_ = now + getDelayWithJitter(settings.switchDelay);
        } else {
            // Finished current cycle
            if (settings.switchBack && originalSlot_ >= 0 && originalSlot_ <= 8 && player.isValid()) {
                player.setCurrentSlot(originalSlot_);
            }

            if (settings.mode == static_cast<int>(BardMode::Single)) {
                finish(false);
            } else {
                state_ = State::BetweenCycles;
                nextAction_ = now + getDelayWithJitter(settings.loopDelay);
            }
        }
    } else if (state_ == State::BetweenCycles) {
        if (settings.switchBack && player.isValid()) {
            originalSlot_ = player.getCurrentSlot();
        }

        if (!buildPlan()) {
            nextAction_ = now + getDelayWithJitter(settings.loopDelay);
            return;
        }

        currentTargetIndex_ = 0;
        state_ = State::Holding;
        applyTarget(0);
        nextAction_ = now + getDelayWithJitter(settings.switchDelay);
    }
}

void BardHelper::onRenderMenu() {
    bool enabledValue = enabled;
    static const char* const modes[] = {"Single Cycle", "Continuous Loop"};

    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled) {
                setEnabled(enabledValue);
            }
            ui::Combo("Mode", &settings.mode, modes, IM_ARRAYSIZE(modes));
            ui::Checkbox("Switch Back", &settings.switchBack);
            ui::Checkbox("Click Buff (Use)", &settings.clickBuff);
            ui::SliderInt("Switch Delay", &settings.switchDelay, 20, 1000, "%d ms");
            if (settings.mode == static_cast<int>(BardMode::Continuous)) {
                ui::SliderInt("Loop Delay", &settings.loopDelay, 50, 2000, "%d ms");
            }
            ui::SliderInt("Randomization", &settings.randomization, 0, 50, "%d ms");
        },
        "Buff Items",
        [this]() {
            ui::Checkbox("Resistance (Iron)", &settings.resistance);
            ui::Checkbox("Strength (Blaze)", &settings.strength);
            ui::Checkbox("Regeneration (Tear)", &settings.regeneration);
            ui::Checkbox("Speed (Sugar)", &settings.speed);
            ui::Checkbox("Jump Boost (Feather)", &settings.jump);
            ui::Checkbox("Fire Res (Magma)", &settings.fireResistance);
            ui::Checkbox("Debuff (Spider Eye)", &settings.debuff);
        });
}

std::string BardHelper::getDisplayInfo() {
    if (settings.mode == static_cast<int>(BardMode::Single))
        return "Single";
    return "Loop";
}

void BardHelper::saveSettings(json::Value& node) const {
    node["mode"] = settings.mode;
    node["switchBack"] = settings.switchBack;
    node["clickBuff"] = settings.clickBuff;
    node["switchDelay"] = settings.switchDelay;
    node["loopDelay"] = settings.loopDelay;
    node["randomization"] = settings.randomization;
    node["resistance"] = settings.resistance;
    node["strength"] = settings.strength;
    node["regeneration"] = settings.regeneration;
    node["speed"] = settings.speed;
    node["jump"] = settings.jump;
    node["fireResistance"] = settings.fireResistance;
    node["debuff"] = settings.debuff;
}

void BardHelper::loadSettings(const json::Value& node) {
    settings.mode = (std::clamp)(node["mode"].as_int(settings.mode), 0, 1);
    settings.switchBack = node["switchBack"].as_bool(settings.switchBack);
    settings.clickBuff = node["clickBuff"].as_bool(settings.clickBuff);
    settings.switchDelay = (std::clamp)(node["switchDelay"].as_int(settings.switchDelay), 20, 1000);
    settings.loopDelay = (std::clamp)(node["loopDelay"].as_int(settings.loopDelay), 50, 2000);
    settings.randomization = (std::clamp)(node["randomization"].as_int(settings.randomization), 0, 50);
    settings.resistance = node["resistance"].as_bool(settings.resistance);
    settings.strength = node["strength"].as_bool(settings.strength);
    settings.regeneration = node["regeneration"].as_bool(settings.regeneration);
    settings.speed = node["speed"].as_bool(settings.speed);
    settings.jump = node["jump"].as_bool(settings.jump);
    settings.fireResistance = node["fireResistance"].as_bool(settings.fireResistance);
    settings.debuff = node["debuff"].as_bool(settings.debuff);
}
