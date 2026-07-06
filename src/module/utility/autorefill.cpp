#include "autorefill.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

AutoRefill* autoRefill = nullptr;

AutoRefill::AutoRefill() : Module("AutoRefill") {
    keybind = 0;  // No default keybind
}

bool AutoRefill::canToggle() {
    // Block toggle when in passive mode (onInventoryOpen)
    return !settings.onInventoryOpen;
}

void AutoRefill::onEnable() {
    // Manual mode: one-shot refill driven entirely by packets (no GUI, no SendInput).
    if (mc.isInGui()) {
        enabled = false;
        return;
    }

    scanForRefills();

    if (pendingRefills.empty()) {
        enabled = false;
        return;
    }

    mc.sendInventoryOpenPacket();
    state = State::Refilling;
    stateStartTime = std::chrono::steady_clock::now();
    lastMoveTime = stateStartTime - std::chrono::milliseconds(settings.speed);
}

void AutoRefill::onDisable() {
    state = State::Idle;
    pendingRefills.clear();
}

void AutoRefill::finishRefill() {
    state = State::Idle;
    pendingRefills.clear();

    // Passive mode stays enabled to catch the next inventory open.
    if (!settings.onInventoryOpen) {
        enabled = false;
    }
}

void AutoRefill::onUpdate() {
    if (!player.isValid())
        return;

    // Passive mode: refill when the user opens their inventory GUI.
    if (settings.onInventoryOpen && state == State::Idle) {
        bool inventoryOpen = mc.isInventoryOpen();

        if (inventoryOpen && !wasInventoryOpen) {
            scanForRefills();
            if (!pendingRefills.empty()) {
                state = State::Refilling;
                stateStartTime = std::chrono::steady_clock::now();
                lastMoveTime = stateStartTime - std::chrono::milliseconds(settings.speed);
            }
        }

        wasInventoryOpen = inventoryOpen;
    }

    if (state == State::Idle)
        return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStartTime).count();

    switch (state) {
        case State::Idle:
            break;

        case State::Refilling: {
            if (elapsed < settings.speed)
                return;

            if (pendingRefills.empty()) {
                state = State::Closing;
                stateStartTime = now;
                return;
            }

            auto moveElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
            if (moveElapsed < settings.speed)
                return;

            RefillTask task = pendingRefills.front();
            pendingRefills.erase(pendingRefills.begin());

            mc.windowClick(task.sourceSlot, 0);  // sends the real C0E click packet on window 0
            lastMoveTime = now;
            break;
        }

        case State::Closing: {
            if (elapsed < settings.speed)
                return;

            // Validation: re-scan and retry if items are still missing.
            scanForRefills();
            if (!pendingRefills.empty()) {
                state = State::Refilling;
                stateStartTime = now;
                lastMoveTime = now - std::chrono::milliseconds(settings.speed);
                return;
            }

            // Only send a close packet in manual mode; in passive mode the user
            // owns the open inventory, so leave it untouched.
            if (!settings.onInventoryOpen)
                mc.sendInventoryClosePacket();
            finishRefill();
            break;
        }
    }
}

void AutoRefill::scanForRefills() {
    pendingRefills.clear();

    auto slots = mc.getInventorySlots();
    if (slots.empty())
        return;

    // Collect all refillable items from main inventory (slots 9-35)
    std::vector<int> availableSources;
    for (const auto& slot : slots) {
        if (slot.slotId < 9 || slot.slotId > 35)
            continue;
        if (slot.itemId == 0)
            continue;

        bool isRefillable = false;
        if (settings.refillSoup && slot.itemId == RefillItems::MUSHROOM_SOUP) {
            isRefillable = true;
        }
        // Only refill Instant Health 2 splash potions (damage value 16421)
        if (settings.refillPotions && slot.itemId == RefillItems::POTION && slot.damage == 16421) {
            isRefillable = true;
        }

        if (isRefillable) {
            availableSources.push_back(slot.slotId);
        }
    }

    if (availableSources.empty())
        return;

    // Find empty hotbar slots and assign sources
    size_t sourceIdx = 0;
    for (int hotbarSlot = 0; hotbarSlot <= 8 && sourceIdx < availableSources.size(); hotbarSlot++) {
        bool isEmpty = true;
        for (const auto& slot : slots) {
            if (slot.slotId == hotbarSlot && slot.itemId != 0) {
                isEmpty = false;
                break;
            }
        }

        if (isEmpty) {
            RefillTask task;
            task.hotbarSlot = hotbarSlot;
            task.sourceSlot = availableSources[sourceIdx];
            pendingRefills.push_back(task);
            sourceIdx++;
        }
    }
}

void AutoRefill::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::Binder("Keybind", &keybind);
            ImGui::Spacing();
            int speed = settings.speed;
            if (ui::SliderInt("Speed (ms)", &speed, 0, 200, "%d"))
                settings.speed = speed;
            ImGui::Spacing();
            ui::Checkbox("On Inventory Open", &settings.onInventoryOpen);
        },
        "Filters",
        [this]() {
            ui::Checkbox("Refill Soup", &settings.refillSoup);
            ui::Checkbox("Refill Potions", &settings.refillPotions);
        });
}

std::string AutoRefill::getDisplayInfo() {
    return settings.onInventoryOpen ? "Inv Open" : "Manual";
}



void AutoRefill::saveSettings(json::Value& node) const {
    node["refillSoup"] = settings.refillSoup;
    node["refillPotions"] = settings.refillPotions;
    node["speed"] = settings.speed;
    node["onInventoryOpen"] = settings.onInventoryOpen;
}

void AutoRefill::loadSettings(const json::Value& node) {
    settings.refillSoup = node["refillSoup"].as_bool(settings.refillSoup);
    settings.refillPotions = node["refillPotions"].as_bool(settings.refillPotions);
    settings.speed = node["speed"].as_int(settings.speed);
    settings.onInventoryOpen = node["onInventoryOpen"].as_bool(settings.onInventoryOpen);
}
