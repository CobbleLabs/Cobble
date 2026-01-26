#include "armorswitch.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

ArmorSwitch* armorSwitch = nullptr;

// Material names for UI dropdown
static const char* materialNames[] = {"Diamond", "Gold", "Iron", "Leather", "Chain"};
static constexpr int MATERIAL_COUNT = 5;

// Send keyboard input
static void sendKey(WORD vk) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

ArmorSwitch::ArmorSwitch() : Module("ArmorSwitch") {
    keybind = 0;
}

bool ArmorSwitch::canToggle() {
    // Prevent spam: don't allow toggle while processing
    return state == State::Idle;
}

int ArmorSwitch::inventoryToContainerSlot(int invSlot) {
    // mainInventory: 0-8 = hotbar, 9-35 = main inventory
    // Container slots: 5-8 = armor, 9-35 = main inventory, 36-44 = hotbar
    if (invSlot >= 0 && invSlot <= 8) {
        return 36 + invSlot;  // Hotbar: inv 0-8 -> container 36-44
    } else if (invSlot >= 9 && invSlot <= 35) {
        return invSlot;  // Main inventory: same slot numbers
    }
    return -1;
}

int ArmorSwitch::getArmorId(ArmorMaterial material, int slotType) {
    // slotType: 0=helmet, 1=chest, 2=legs, 3=boots
    switch (material) {
        case ArmorMaterial::Diamond:
            switch (slotType) {
                case 0:
                    return ArmorItems::DIAMOND_HELMET;
                case 1:
                    return ArmorItems::DIAMOND_CHESTPLATE;
                case 2:
                    return ArmorItems::DIAMOND_LEGGINGS;
                case 3:
                    return ArmorItems::DIAMOND_BOOTS;
                default:
                    return 0;
            }
        case ArmorMaterial::Gold:
            switch (slotType) {
                case 0:
                    return ArmorItems::GOLD_HELMET;
                case 1:
                    return ArmorItems::GOLD_CHESTPLATE;
                case 2:
                    return ArmorItems::GOLD_LEGGINGS;
                case 3:
                    return ArmorItems::GOLD_BOOTS;
                default:
                    return 0;
            }
        case ArmorMaterial::Iron:
            switch (slotType) {
                case 0:
                    return ArmorItems::IRON_HELMET;
                case 1:
                    return ArmorItems::IRON_CHESTPLATE;
                case 2:
                    return ArmorItems::IRON_LEGGINGS;
                case 3:
                    return ArmorItems::IRON_BOOTS;
                default:
                    return 0;
            }
        case ArmorMaterial::Leather:
            switch (slotType) {
                case 0:
                    return ArmorItems::LEATHER_HELMET;
                case 1:
                    return ArmorItems::LEATHER_CHESTPLATE;
                case 2:
                    return ArmorItems::LEATHER_LEGGINGS;
                case 3:
                    return ArmorItems::LEATHER_BOOTS;
                default:
                    return 0;
            }
        case ArmorMaterial::Chain:
            switch (slotType) {
                case 0:
                    return ArmorItems::CHAINMAIL_HELMET;
                case 1:
                    return ArmorItems::CHAINMAIL_CHESTPLATE;
                case 2:
                    return ArmorItems::CHAINMAIL_LEGGINGS;
                case 3:
                    return ArmorItems::CHAINMAIL_BOOTS;
                default:
                    return 0;
            }
        default:
            return 0;
    }
}

int ArmorSwitch::detectCurrentSet() {
    ArmorMaterial set1Mat = static_cast<ArmorMaterial>(settings.set1Material);
    ArmorMaterial set2Mat = static_cast<ArmorMaterial>(settings.set2Material);

    auto armorSlots = mc.getArmorSlots();
    
    int set1Matches = 0;
    int set2Matches = 0;

    for (int slotType = 0; slotType < 4; slotType++) {
        int equipped = (slotType < (int)armorSlots.size()) ? armorSlots[slotType].itemId : 0;
        if (equipped == 0)
            continue;

        if (equipped == getArmorId(set1Mat, slotType))
            set1Matches++;
        if (equipped == getArmorId(set2Mat, slotType))
            set2Matches++;
    }

    if (set1Matches > set2Matches)
        return 0;
    if (set2Matches > set1Matches)
        return 1;
    return -1;
}

void ArmorSwitch::onEnable() {
    if (mc.isInGui()) {
        enabled = false;
        return;
    }

    // Detect current set and switch to the OTHER one
    int currentSet = detectCurrentSet();
    if (currentSet == 0) {
        targetSet = 1;
    } else {
        targetSet = 0;
    }

    scanForSwaps();

    if (pendingSwaps.empty()) {
        enabled = false;
        return;
    }

    sendKey('E');
    state = State::OpeningInventory;
    stateStartTime = std::chrono::steady_clock::now();
}

void ArmorSwitch::onDisable() {
    state = State::Idle;
    pendingSwaps.clear();
}

void ArmorSwitch::finishSwap() {
    if (mc.isInventoryOpen()) {
        sendKey(VK_ESCAPE);
    }

    state = State::Idle;
    pendingSwaps.clear();
    enabled = false;
}

void ArmorSwitch::onUpdate() {
    if (!player.isValid())
        return;
    if (state == State::Idle)
        return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStartTime).count();

    switch (state) {
        case State::Idle:
            break;

        case State::OpeningInventory: {
            if (mc.isInventoryOpen()) {
                state = State::Switching;
                stateStartTime = now;
                lastClickTime = now - std::chrono::milliseconds(settings.switchDelay);
            } else if (elapsed > 1000) {
                finishSwap();
            }
            break;
        }

        case State::Switching: {
            if (elapsed < settings.switchDelay)
                return;

            if (!mc.isInventoryOpen()) {
                finishSwap();
                return;
            }

            if (pendingSwaps.empty()) {
                state = State::Closing;
                stateStartTime = now;
                return;
            }

            auto clickElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count();
            if (clickElapsed < settings.switchDelay)
                return;

            SwapTask task = pendingSwaps.front();
            pendingSwaps.erase(pendingSwaps.begin());

            // Swap container slot with hotbar slot
            mc.windowClickSwap(task.containerSlot, task.hotbarIndex);

            lastClickTime = now;
            break;
        }

        case State::Closing: {
            if (elapsed >= settings.switchDelay) {
                finishSwap();
            }
            break;
        }
    }
}

int ArmorSwitch::findArmorInInventory(int slotType, ArmorMaterial material, int* outDamage, int* outProtLevel) {
    auto slots = mc.getInventorySlots();
    int targetId = getArmorId(material, slotType);

    bool found = false;
    int bestSlot = -1;
    int bestDamage = 0;
    int bestProtLevel = 0;

    // Search inventory and hotbar (slots 0-35)
    // SlotInfo already has protectionLevel from SDK
    for (const auto& slot : slots) {
        if (slot.slotId >= 0 && slot.slotId <= 35 && slot.itemId == targetId) {
            // Compare: Protection first (if enabled), then durability (lower damage = better)
            bool isBetter = false;
            if (!found) {
                isBetter = true;
            } else if (settings.preferProtection) {
                if (slot.protectionLevel > bestProtLevel) {
                    isBetter = true;
                } else if (slot.protectionLevel == bestProtLevel && slot.damage < bestDamage) {
                    isBetter = true;
                }
            } else {
                if (slot.damage < bestDamage) {
                    isBetter = true;
                }
            }
            
            if (isBetter) {
                found = true;
                bestSlot = slot.slotId;
                bestDamage = slot.damage;
                bestProtLevel = slot.protectionLevel;
            }
        }
    }

    if (found) {
        if (outDamage)
            *outDamage = bestDamage;
        if (outProtLevel)
            *outProtLevel = bestProtLevel;
        return bestSlot;
    }
    return -1;
}

void ArmorSwitch::scanForSwaps() {
    pendingSwaps.clear();

    ArmorMaterial targetMat =
        static_cast<ArmorMaterial>(targetSet == 0 ? settings.set1Material : settings.set2Material);

    auto slots = mc.getInventorySlots();
    if (slots.empty())
        return;

    // Get equipped armor info from SDK
    auto armorSlots = mc.getArmorSlots();

    bool hotbarOccupied[9] = {};
    bool hotbarReserved[9] = {};
    for (const auto& slot : slots) {
        if (slot.slotId >= 0 && slot.slotId <= 8 && slot.itemId != 0) {
            hotbarOccupied[slot.slotId] = true;
        }
    }

    // Container slots for armor: 5=helmet, 6=chest, 7=legs, 8=boots
    // slotType: 0=helmet, 1=chest, 2=legs, 3=boots
    for (int slotType = 0; slotType < 4; slotType++) {
        int targetId = getArmorId(targetMat, slotType);
        
        // Get current equipped armor info from SDK
        int currentId = (slotType < (int)armorSlots.size()) ? armorSlots[slotType].itemId : 0;
        int currentDamage = (slotType < (int)armorSlots.size()) ? armorSlots[slotType].damage : 0;
        int currentProtLevel = (slotType < (int)armorSlots.size()) ? armorSlots[slotType].protectionLevel : 0;

        int sourceDamage = 0;
        int sourceProtLevel = 0;
        int sourceSlot = -1;
        
        if (currentId == targetId && currentId != 0) {
            // Same material equipped - check if inventory has better
            sourceSlot = findArmorInInventory(slotType, targetMat, &sourceDamage, &sourceProtLevel);
            if (sourceSlot == -1) {
                continue;
            }

            // Compare: Protection first (if enabled), then durability
            bool shouldSwitch = false;
            if (settings.preferProtection) {
                if (sourceProtLevel > currentProtLevel) {
                    shouldSwitch = true;
                } else if (sourceProtLevel == currentProtLevel && currentDamage > sourceDamage) {
                    shouldSwitch = true;
                }
            } else {
                if (currentDamage > sourceDamage) {
                    shouldSwitch = true;
                }
            }
            
            if (!shouldSwitch) {
                continue;
            }
        } else {
            // Different material or empty slot - find target material
            sourceSlot = findArmorInInventory(slotType, targetMat, &sourceDamage, &sourceProtLevel);
            if (sourceSlot == -1) {
                continue;
            }
        }

        int hotbarIndex = -1;
        
        // Case 1: Armor is already in hotbar (sourceSlot 0-8)
        if (sourceSlot >= 0 && sourceSlot <= 8) {
            hotbarIndex = sourceSlot;
            hotbarReserved[hotbarIndex] = true;
            
            // Direct swap: hotbar slot <-> armor slot
            // After this: armor slot has new armor, hotbar has old armor (or empty)
            // Old armor stays in hotbar where new armor was - simple exchange
            SwapTask equipTask;
            equipTask.containerSlot = 5 + slotType;  // armor slot
            equipTask.hotbarIndex = hotbarIndex;
            pendingSwaps.push_back(equipTask);
        } else {
            // Case 2: Armor is in main inventory (sourceSlot 9-35)
            // Need to use hotbar as intermediary
            
            // Find a hotbar slot to use
            for (int i = 0; i < 9; i++) {
                if (!hotbarOccupied[i] && !hotbarReserved[i]) {
                    hotbarIndex = i;
                    hotbarReserved[i] = true;
                    break;
                }
            }

            if (hotbarIndex == -1) {
                // Hotbar full: use slots 2-5 as swap targets
                for (int i = 2; i <= 5; i++) {
                    if (!hotbarReserved[i]) {
                        hotbarIndex = i;
                        hotbarReserved[i] = true;
                        break;
                    }
                }
            }

            if (hotbarIndex == -1) {
                continue;
            }

            // Step 1: Move new armor from inventory to hotbar
            // sourceSlot (inv) <-> hotbarIndex
            // After: hotbar has new armor, sourceSlot has whatever was in hotbar
            SwapTask moveTask;
            moveTask.containerSlot = sourceSlot;  // main inventory slot 9-35
            moveTask.hotbarIndex = hotbarIndex;
            pendingSwaps.push_back(moveTask);

            // Step 2: Swap hotbar (new armor) with armor slot (old armor)
            // After: armor slot has new armor, hotbar has old armor
            SwapTask equipTask;
            equipTask.containerSlot = 5 + slotType;  // armor slot
            equipTask.hotbarIndex = hotbarIndex;
            pendingSwaps.push_back(equipTask);

            // Step 3: Move old armor (now in hotbar) back to original source slot
            // After: sourceSlot has old armor, hotbar has original hotbar item
            SwapTask restoreTask;
            restoreTask.containerSlot = sourceSlot;
            restoreTask.hotbarIndex = hotbarIndex;
            pendingSwaps.push_back(restoreTask);
        }
    }
}

void ArmorSwitch::onRenderMenu() {
    ui::section("Settings", [this]() {
        ui::Binder("Keybind", &keybind);
        ImGui::Spacing();

        ui::Combo("Set 1", &settings.set1Material, materialNames, MATERIAL_COUNT);
        ui::Combo("Set 2", &settings.set2Material, materialNames, MATERIAL_COUNT);

        ImGui::Spacing();

        int delay = settings.switchDelay;
        if (ui::SliderInt("Delay (ms)", &delay, 0, 200, "%d"))
            settings.switchDelay = delay;
        
        ImGui::Spacing();
        ui::Checkbox("Prefer Protection", &settings.preferProtection);
    });
}

std::string ArmorSwitch::getDisplayInfo() {
    int currentSet = detectCurrentSet();
    if (currentSet == 0) {
        return std::string("Set 1 - ") + materialNames[settings.set1Material];
    } else if (currentSet == 1) {
        return std::string("Set 2 - ") + materialNames[settings.set2Material];
    }
    return "Mixed";
}
