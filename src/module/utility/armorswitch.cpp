#include "armorswitch.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/core/overlay.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"

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

// LWJGL key code to Windows scan code mapping (common keys)
// LWJGL uses scancodes directly for most keys
static void sendLwjglKey(int lwjglKeyCode) {
    // LWJGL key codes are already scan codes for most keys
    // We need to send as scan code, not virtual key
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = (WORD)lwjglKeyCode;
    inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = (WORD)lwjglKeyCode;
    inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

static bool containerSlotScreenPosition(int containerSlot, POINT& position) {
    if (!mc.isPlayerInventoryOpen() || !Overlay::gameWindow || GetForegroundWindow() != Overlay::gameWindow)
        return false;

    JNIEnv* env = Java::Env();
    if (!env || !JniCache::fid_currentScreen)
        return false;
    jobject screen = env->GetObjectField(mc.instance, JniCache::fid_currentScreen);
    if (!screen)
        return false;

    jclass guiScreen = nullptr;
    if (!Java::AssignClass("net.minecraft.client.gui.GuiScreen", guiScreen)) {
        env->DeleteLocalRef(screen);
        return false;
    }
    jfieldID width = env->GetFieldID(guiScreen, "width", "I");
    if (!width) {
        env->ExceptionClear();
        width = env->GetFieldID(guiScreen, "field_146294_l", "I");
    }
    jfieldID height = env->GetFieldID(guiScreen, "height", "I");
    if (!height) {
        env->ExceptionClear();
        height = env->GetFieldID(guiScreen, "field_146295_m", "I");
    }
    const int guiWidth = width ? env->GetIntField(screen, width) : 0;
    const int guiHeight = height ? env->GetIntField(screen, height) : 0;
    env->DeleteLocalRef(guiScreen);
    env->DeleteLocalRef(screen);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (guiWidth <= 0 || guiHeight <= 0)
        return false;

    int slotX = 0;
    int slotY = 0;
    if (containerSlot >= 5 && containerSlot <= 8) {
        slotX = 16;
        slotY = 16 + (containerSlot - 5) * 18;
    } else if (containerSlot >= 9 && containerSlot <= 35) {
        const int index = containerSlot - 9;
        slotX = 16 + (index % 9) * 18;
        slotY = 92 + (index / 9) * 18;
    } else if (containerSlot >= 36 && containerSlot <= 44) {
        slotX = 16 + (containerSlot - 36) * 18;
        slotY = 150;
    } else {
        return false;
    }

    RECT client{};
    if (!GetClientRect(Overlay::gameWindow, &client))
        return false;
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0)
        return false;

    const int guiLeft = (guiWidth - 176) / 2;
    const int guiTop = (guiHeight - 166) / 2;
    position.x = (guiLeft + slotX) * clientWidth / guiWidth;
    position.y = (guiTop + slotY) * clientHeight / guiHeight;
    return ClientToScreen(Overlay::gameWindow, &position) != FALSE;
}

ArmorSwitch::ArmorSwitch() : Module("ArmorSwitch") {
    keybind = 0;
}

bool ArmorSwitch::canToggle() {
    // Block toggle when in passive mode (onInventoryOpen)
    return !settings.onInventoryOpen && state == State::Idle;
}

bool ArmorSwitch::startVisualMove(int containerSlot) {
    if (!containerSlotScreenPosition(containerSlot, mouseTarget_))
        return false;
    return GetCursorPos(&mouseStart_) != FALSE;
}

bool ArmorSwitch::updateVisualMove(long long elapsed) {
    const int duration = (std::max)(1, activeVisualMoveTime_);
    const float progress = (std::min)(1.0f, static_cast<float>(elapsed) / duration);
    // Smooth acceleration/deceleration prevents robotic constant-speed jumps.
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    const int x = mouseStart_.x + static_cast<int>((mouseTarget_.x - mouseStart_.x) * eased);
    const int y = mouseStart_.y + static_cast<int>((mouseTarget_.y - mouseStart_.y) * eased);
    SetCursorPos(x, y);
    return progress >= 1.0f;
}

void ArmorSwitch::performSwap() {
    // The visible cursor move belongs to Legit Mode. Use the inventory
    // controller for the actual number-key swap: injected number keys are
    // occasionally dropped by LWJGL, leaving a partial armor set.
    mc.windowClickSwap(lastSwap.containerSlot, lastSwap.hotbarIndex);
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
    // Safety check - return -1 if not in game
    if (!mc.isValid() || !player.isValid())
        return -1;

    ArmorMaterial set1Mat = static_cast<ArmorMaterial>(settings.set1Material);
    ArmorMaterial set2Mat = static_cast<ArmorMaterial>(settings.set2Material);

    auto armorSlots = mc.getArmorSlots();
    if (armorSlots.empty())
        return -1;

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
    // Safety check - don't start if not in game
    if (!mc.isValid() || !player.isValid()) {
        enabled = false;
        return;
    }

    // Safety check - don't start if already processing
    if (state != State::Idle) {
        enabled = false;
        return;
    }

    // Passive mode: just stay enabled and monitor in onUpdate
    if (settings.onInventoryOpen) {
        wasInventoryOpen = mc.isInventoryOpen();
        return;  // Stay enabled, onUpdate will handle the rest
    }

    // Manual mode: don't start if already in a GUI
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

    // Open inventory first
    if (settings.silentMode) {
        log_debug("[ArmorSwitch] Silent Mode: Sending Open Packet + Starting swaps.");
        mc.sendInventoryOpenPacket();

        state = State::Switching;
        stateStartTime = std::chrono::steady_clock::now();
        lastClickTime = stateStartTime - std::chrono::milliseconds(effectiveDelay());
    } else {
        sendLwjglKey(mc.getInventoryKey());
        state = State::OpeningInventory;
        stateStartTime = std::chrono::steady_clock::now();
    }
}

void ArmorSwitch::onDisable() {
    state = State::Idle;
    pendingSwaps.clear();
}

void ArmorSwitch::finishSwap(bool fromOnInventoryOpen) {
    // Close via JNI, not SendInput(VK_ESCAPE): with Ctrl held, Ctrl+Esc opens
    // the Windows Start menu.
    if (!fromOnInventoryOpen && !settings.silentMode && mc.isInventoryOpen()) {
        mc.closeInventory();
    }

    state = State::Idle;
    pendingSwaps.clear();

    // Don't disable in passive mode - stays active to detect next inventory open
    if (!settings.onInventoryOpen) {
        enabled = false;
    }
}

void ArmorSwitch::onUpdate() {
    if (!player.isValid())
        return;

    // Passive mode: monitor for inventory open
    if (settings.onInventoryOpen && state == State::Idle) {
        bool inventoryOpen = mc.isInventoryOpen();

        if (inventoryOpen && !wasInventoryOpen) {
            // User just opened inventory - detect and switch armor
            int currentSet = detectCurrentSet();
            if (currentSet == 0) {
                targetSet = 1;
            } else {
                targetSet = 0;
            }

            scanForSwaps();

            if (!pendingSwaps.empty()) {
                state = State::Switching;
                stateStartTime = std::chrono::steady_clock::now();
                lastClickTime = stateStartTime - std::chrono::milliseconds(effectiveDelay());
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

        case State::OpeningInventory: {
            if (mc.isInventoryOpen()) {
                state = State::Switching;
                stateStartTime = now;
                lastClickTime = now - std::chrono::milliseconds(effectiveDelay());
            } else if (elapsed > 1000) {
                finishSwap();
            }
            break;
        }

        case State::Switching: {
            if (!mc.isInventoryOpen() && !settings.silentMode) {
                log_debug("[ArmorSwitch] Aborting: Inventory closed unexpectedly.");
                finishSwap(settings.onInventoryOpen);
                return;
            }

            if (pendingSwaps.empty()) {
                // Done switching - go to closing
                state = State::Closing;
                stateStartTime = now;
                return;
            }

            auto clickElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count();
            int delay = effectiveDelay();
            if (clickElapsed < delay)
                return;

            lastSwap = pendingSwaps.front();
            pendingSwaps.erase(pendingSwaps.begin());

            if (settings.visualMode && !settings.silentMode && startVisualMove(lastSwap.containerSlot)) {
                auto nextVariation = [this](int min, int max) {
                    visualSeed_ = visualSeed_ * 1664525u + 1013904223u;
                    return min + static_cast<int>(visualSeed_ % static_cast<uint32_t>(max - min + 1));
                };
                mouseTarget_.x += nextVariation(-3, 3);
                mouseTarget_.y += nextVariation(-3, 3);
                activeVisualMoveTime_ = (std::max)(80, settings.visualMoveTime + nextVariation(-25, 35));
                state = State::VisualMove;
                stateStartTime = now;
                return;
            }

            // Swap container slot with hotbar slot.
            if (settings.silentMode) {
                log_debug("[ArmorSwitch] Silent Swap: Container %d <-> Hotbar %d", lastSwap.containerSlot,
                          lastSwap.hotbarIndex);
            }
            performSwap();

            lastClickTime = now;

            // If retryOnFail enabled, verify all swaps
            if (settings.retryOnFail || settings.visualMode) {
                state = State::Verifying;
                stateStartTime = now;
            }
            break;
        }

        case State::VisualMove: {
            if (!mc.isPlayerInventoryOpen()) {
                finishSwap(settings.onInventoryOpen);
                return;
            }
            if (!updateVisualMove(elapsed))
                return;

            performSwap();
            lastClickTime = now;
            if (settings.retryOnFail) {
                state = State::Verifying;
                stateStartTime = now;
            } else {
                state = State::Switching;
                stateStartTime = now;
            }
            break;
        }

        case State::Verifying: {
            if (!mc.isInventoryOpen() && !settings.silentMode) {
                finishSwap(settings.onInventoryOpen);
                return;
            }

            int verifyDelay = (settings.retryOnFail || settings.visualMode)
                                  ? (std::max)(VERIFY_DELAY_MS, effectiveDelay())
                                  : VERIFY_DELAY_MS;
            // Wait fixed delay before checking
            if (elapsed < verifyDelay)
                return;

            auto slots = mc.getInventorySlots();

            auto retrySwap = [&]() {
                if (settings.silentMode) {
                    log_debug("[ArmorSwitch] VS Fail: Retrying swap (try %d/%d)", lastSwap.retryCount + 1, MAX_RETRIES);
                }
                if (lastSwap.retryCount < MAX_RETRIES) {
                    lastSwap.retryCount++;
                    pendingSwaps.insert(pendingSwaps.begin(), lastSwap);
                    state = State::Switching;
                    stateStartTime = now;
                    lastClickTime = now - std::chrono::milliseconds(effectiveDelay());
                } else {
                    if (settings.silentMode)
                        log_debug("[ArmorSwitch] Swap Failed: Max retries reached execution aborted for this item.");
                    state = State::Switching;
                    stateStartTime = now;
                }
            };

            if (lastSwap.armorSlotType >= 0) {
                // Check armor slot
                auto armorSlots = mc.getArmorSlots();
                int currentId =
                    (lastSwap.armorSlotType < (int)armorSlots.size()) ? armorSlots[lastSwap.armorSlotType].itemId : 0;

                if (currentId == lastSwap.expectedItemId) {
                    if (settings.silentMode)
                        log_debug("[ArmorSwitch] Verify OK: Armor slot %d has correct item %d", lastSwap.armorSlotType,
                                  currentId);
                    state = State::Switching;
                    stateStartTime = now;
                } else {
                    if (settings.silentMode)
                        log_debug("[ArmorSwitch] Verify Mismatch: Armor slot %d has %d, expected %d",
                                  lastSwap.armorSlotType, currentId, lastSwap.expectedItemId);
                    retrySwap();
                }
            } else if (lastSwap.armorSlotType == -2) {
                // Verify hotbar slot contains the target armor after move
                int hotbarId = 0;
                for (const auto& s : slots) {
                    if (s.slotId == lastSwap.hotbarIndex) {
                        hotbarId = s.itemId;
                        break;
                    }
                }
                if (hotbarId == lastSwap.expectedItemId) {
                    if (settings.silentMode)
                        log_debug("[ArmorSwitch] Verify OK: Hotbar slot %d has correct item %d", lastSwap.hotbarIndex,
                                  hotbarId);
                    state = State::Switching;
                    stateStartTime = now;
                } else {
                    if (settings.silentMode)
                        log_debug("[ArmorSwitch] Verify Mismatch: Hotbar slot %d has %d, expected %d",
                                  lastSwap.hotbarIndex, hotbarId, lastSwap.expectedItemId);
                    retrySwap();
                }
            } else if (lastSwap.armorSlotType == -3) {
                // Verify inventory slot contains old armor after restore
                int invId = 0;
                bool found = false;
                for (const auto& s : slots) {
                    if (s.slotId == lastSwap.containerSlot) {
                        invId = s.itemId;
                        found = true;
                        break;
                    }
                }
                if (found && invId == lastSwap.expectedItemId) {
                    if (settings.silentMode)
                        log_debug("[ArmorSwitch] Verify OK: Inventory slot %d has correct item %d",
                                  lastSwap.containerSlot, invId);
                    state = State::Switching;
                    stateStartTime = now;
                } else {
                    if (settings.silentMode)
                        log_debug("[ArmorSwitch] Verify Mismatch: Inventory slot %d has %d (found=%d), expected %d",
                                  lastSwap.containerSlot, invId, found, lastSwap.expectedItemId);
                    retrySwap();
                }
            } else {
                // Non-verifiable swap
                state = State::Switching;
                stateStartTime = now;
            }
            break;
        }

        case State::Closing: {
            if (settings.silentMode) {
                log_debug("[ArmorSwitch] Silent Mode: Finished (Sending Close Packet).");
                mc.sendInventoryClosePacket();
                finishSwap(settings.onInventoryOpen);
            } else if (elapsed >= settings.switchDelay) {
                finishSwap(settings.onInventoryOpen);
            }
            break;
        }
    }
}

int ArmorSwitch::findArmorInInventory(const std::vector<SlotInfo>& slots, int slotType, ArmorMaterial material,
                                      int* outDamage, int* outProtLevel) {
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
    const bool verifySwaps = settings.retryOnFail || settings.visualMode;

    ArmorMaterial targetMat =
        static_cast<ArmorMaterial>(targetSet == 0 ? settings.set1Material : settings.set2Material);

    auto slots = mc.getInventorySlots();
    if (slots.empty()) {
        return;
    }

    // Get equipped armor info from SDK
    auto armorSlots = mc.getArmorSlots();

    bool hotbarOccupied[9] = {};
    bool hotbarReserved[9] = {};
    for (const auto& slot : slots) {
        if (slot.slotId >= 0 && slot.slotId <= 8 && slot.itemId != 0) {
            hotbarOccupied[slot.slotId] = true;
        }
    }

    struct PlannedSwap {
        int slotType;
        int targetId;
        int sourceSlot;
        int hotbarIndex;
        bool movedFromInventory;
        int oldArmorId;
    };
    std::vector<PlannedSwap> batchSwaps;

    auto findHotbarSlot = [&]() -> int {
        for (int i = 0; i < 9; i++) {
            if (!hotbarOccupied[i] && !hotbarReserved[i]) {
                hotbarReserved[i] = true;
                return i;
            }
        }
        for (int i = 2; i <= 5; i++) {
            if (!hotbarReserved[i]) {
                hotbarReserved[i] = true;
                return i;
            }
        }
        return -1;
    };

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
            sourceSlot = findArmorInInventory(slots, slotType, targetMat, &sourceDamage, &sourceProtLevel);
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
            sourceSlot = findArmorInInventory(slots, slotType, targetMat, &sourceDamage, &sourceProtLevel);
            if (sourceSlot == -1) {
                continue;
            }
        }

        int hotbarIndex = -1;

        // Case 1: Armor is already in hotbar (sourceSlot 0-8)
        if (sourceSlot >= 0 && sourceSlot <= 8) {
            hotbarIndex = sourceSlot;
            hotbarReserved[hotbarIndex] = true;
        } else {
            // Case 2: Armor is in main inventory (sourceSlot 9-35)
            hotbarIndex = findHotbarSlot();
            if (hotbarIndex == -1) {
                continue;
            }
        }

        if (verifySwaps) {
            batchSwaps.push_back({slotType, targetId, sourceSlot, hotbarIndex, sourceSlot >= 9, currentId});
            continue;
        }

        if (sourceSlot >= 0 && sourceSlot <= 8) {
            // Direct swap: hotbar slot <-> armor slot
            // After this: armor slot has new armor, hotbar has old armor (or empty)
            // Old armor stays in hotbar where new armor was - simple exchange
            SwapTask equipTask;
            equipTask.containerSlot = 5 + slotType;  // armor slot
            equipTask.hotbarIndex = hotbarIndex;
            equipTask.expectedItemId = targetId;
            equipTask.armorSlotType = slotType;
            equipTask.retryCount = 0;
            pendingSwaps.push_back(equipTask);
        } else {
            // Move -> Equip -> Restore for inventory pieces
            SwapTask moveTask;
            moveTask.containerSlot = sourceSlot;  // main inventory slot 9-35
            moveTask.hotbarIndex = hotbarIndex;
            moveTask.expectedItemId = 0;  // Not an armor slot swap
            moveTask.armorSlotType = -1;
            moveTask.retryCount = 0;
            pendingSwaps.push_back(moveTask);

            SwapTask equipTask;
            equipTask.containerSlot = 5 + slotType;  // armor slot
            equipTask.hotbarIndex = hotbarIndex;
            equipTask.expectedItemId = targetId;
            equipTask.armorSlotType = slotType;
            equipTask.retryCount = 0;
            pendingSwaps.push_back(equipTask);

            SwapTask restoreTask;
            restoreTask.containerSlot = sourceSlot;
            restoreTask.hotbarIndex = hotbarIndex;
            restoreTask.expectedItemId = 0;  // Not an armor slot swap
            restoreTask.armorSlotType = -1;
            restoreTask.retryCount = 0;
            pendingSwaps.push_back(restoreTask);
        }
    }

    if (verifySwaps) {
        // Stage 1: move every target armor piece into its reserved hotbar slot
        for (const auto& swap : batchSwaps) {
            if (!swap.movedFromInventory)
                continue;
            SwapTask moveTask;
            moveTask.containerSlot = swap.sourceSlot;
            moveTask.hotbarIndex = swap.hotbarIndex;
            moveTask.expectedItemId = swap.targetId;  // Expect target armor to land in hotbar
            moveTask.armorSlotType = -2;              // Verify hotbar slot
            moveTask.retryCount = 0;
            pendingSwaps.push_back(moveTask);
        }

        // Stage 2: equip all armor pieces from the hotbar
        for (const auto& swap : batchSwaps) {
            SwapTask equipTask;
            equipTask.containerSlot = 5 + swap.slotType;
            equipTask.hotbarIndex = swap.hotbarIndex;
            equipTask.expectedItemId = swap.targetId;
            equipTask.armorSlotType = swap.slotType;
            equipTask.retryCount = 0;
            pendingSwaps.push_back(equipTask);
        }

        // Stage 3: return the old armor back to the original inventory slot
        for (const auto& swap : batchSwaps) {
            if (!swap.movedFromInventory || swap.oldArmorId == 0)
                continue;
            SwapTask restoreTask;
            restoreTask.containerSlot = swap.sourceSlot;
            restoreTask.hotbarIndex = swap.hotbarIndex;
            restoreTask.expectedItemId = swap.oldArmorId;  // Expect old armor back in its source slot
            restoreTask.armorSlotType = -3;                // Verify inventory slot
            restoreTask.retryCount = 0;
            pendingSwaps.push_back(restoreTask);
        }
    }
}

void ArmorSwitch::onRenderMenu() {
    ui::two_column(
        "Settings",
        [this]() {
            ui::Binder("Keybind", &keybind);
            ImGui::Spacing();

            ui::Combo("Set 1", &settings.set1Material, materialNames, MATERIAL_COUNT);
            ui::Combo("Set 2", &settings.set2Material, materialNames, MATERIAL_COUNT);

            ImGui::Spacing();

            int delay = settings.switchDelay;
            if (ui::SliderInt("Delay (ms)", &delay, 0, 200, "%d"))
                settings.switchDelay = delay;
        },
        "Options",
        [this]() {
            bool wasOnInventoryOpen = settings.onInventoryOpen;
            ui::Checkbox("On Inventory Open", &settings.onInventoryOpen);
            if (settings.onInventoryOpen != wasOnInventoryOpen) {
                if (settings.onInventoryOpen) {
                    if (!enabled) {
                        enabled = true;
                        wasInventoryOpen = mc.isInventoryOpen();
                    }
                } else {
                    if (enabled && state == State::Idle) {
                        enabled = false;
                    }
                }
            }
            ui::Checkbox("Prefer Protection", &settings.preferProtection);
            ui::Checkbox("Retry on Fail", &settings.retryOnFail);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Use if armor swaps fail on some servers");
            }
            ui::Checkbox("Silent Mode", &settings.silentMode);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Swaps armor silently without opening the inventory");
            }
            ui::Checkbox("Legit Mode", &settings.visualMode);
            if (settings.visualMode) {
                int moveTime = settings.visualMoveTime;
                if (ui::SliderInt("Mouse Move (ms)", &moveTime, 80, 350, "%d"))
                    settings.visualMoveTime = moveTime;
            }
        });
}

std::string ArmorSwitch::getDisplayInfo() {
    // Show "Inv Open" in passive mode
    if (settings.onInventoryOpen) {
        return "Inv Open";
    }

    // Safety check - don't query if not in game
    if (!mc.isValid() || !player.isValid())
        return "---";

    int currentSet = detectCurrentSet();
    if (currentSet == 0) {
        return std::string("Set 1 - ") + materialNames[settings.set1Material];
    } else if (currentSet == 1) {
        return std::string("Set 2 - ") + materialNames[settings.set2Material];
    }
    return "Mixed";
}

void ArmorSwitch::saveSettings(json::Value& node) const {
    node["set1Material"] = settings.set1Material;
    node["set2Material"] = settings.set2Material;
    node["switchDelay"] = settings.switchDelay;
    node["retryMinDelay"] = settings.retryMinDelay;
    node["preferProtection"] = settings.preferProtection;
    node["onInventoryOpen"] = settings.onInventoryOpen;
    node["retryOnFail"] = settings.retryOnFail;
    node["silentMode"] = settings.silentMode;
    node["visualMode"] = settings.visualMode;
    node["visualMoveTime"] = settings.visualMoveTime;
    node["autoBalance"] = settings.autoBalance;
    node["maxDurabilityDiff"] = settings.maxDurabilityDiff;
    node["checkInterval"] = settings.checkInterval;
}

void ArmorSwitch::loadSettings(const json::Value& node) {
    settings.set1Material = node["set1Material"].as_int(settings.set1Material);
    settings.set2Material = node["set2Material"].as_int(settings.set2Material);
    settings.switchDelay = node["switchDelay"].as_int(settings.switchDelay);
    settings.retryMinDelay = node["retryMinDelay"].as_int(settings.retryMinDelay);
    settings.preferProtection = node["preferProtection"].as_bool(settings.preferProtection);
    settings.onInventoryOpen = node["onInventoryOpen"].as_bool(settings.onInventoryOpen);
    settings.retryOnFail = node["retryOnFail"].as_bool(settings.retryOnFail);
    settings.silentMode = node["silentMode"].as_bool(settings.silentMode);
    settings.visualMode = node["visualMode"].as_bool(settings.visualMode);
    settings.visualMoveTime = (std::max)(80, node["visualMoveTime"].as_int(settings.visualMoveTime));
    settings.autoBalance = node["autoBalance"].as_bool(settings.autoBalance);
    settings.maxDurabilityDiff = node["maxDurabilityDiff"].as_int(settings.maxDurabilityDiff);
    settings.checkInterval = node["checkInterval"].as_int(settings.checkInterval);
}
