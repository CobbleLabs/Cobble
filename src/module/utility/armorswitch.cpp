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

    int set1Matches = 0;
    int set2Matches = 0;

    for (int slotType = 0; slotType < 4; slotType++) {
        int equipped = getEquippedArmorId(slotType);
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

int ArmorSwitch::getEquippedArmorId(int slotType) {
    if (!player.isValid())
        return 0;

    JNIEnv* env = Java::Env();
    if (!env)
        return 0;

    jobject localPlayer = mc.getPlayer();
    if (!localPlayer)
        return 0;

    jclass playerCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jfieldID fid_inventory = env->GetFieldID(playerCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
    if (!fid_inventory) {
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jobject inventory = env->GetObjectField(localPlayer, fid_inventory);
    if (!inventory) {
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jclass inventoryCls = env->GetObjectClass(inventory);
    jfieldID fid_armorInventory = env->GetFieldID(inventoryCls, "armorInventory", "[Lnet/minecraft/item/ItemStack;");
    if (!fid_armorInventory) {
        env->ExceptionClear();
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jobjectArray armorInventory = (jobjectArray)env->GetObjectField(inventory, fid_armorInventory);
    if (!armorInventory) {
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    // armorInventory indices: 0=boots, 1=legs, 2=chest, 3=helmet
    // Our slotType: 0=helmet, 1=chest, 2=legs, 3=boots
    int armorIdx = 3 - slotType;

    int itemId = 0;
    jobject itemStack = env->GetObjectArrayElement(armorInventory, armorIdx);
    if (itemStack) {
        jclass itemCls = nullptr;
        if (Java::AssignClass("net.minecraft.item.Item", itemCls)) {
            jclass itemStackCls = env->GetObjectClass(itemStack);
            jmethodID mid_getItem = env->GetMethodID(itemStackCls, "getItem", "()Lnet/minecraft/item/Item;");

            if (mid_getItem) {
                jobject item = env->CallObjectMethod(itemStack, mid_getItem);
                if (item) {
                    jmethodID mid_getIdFromItem =
                        env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");
                    if (mid_getIdFromItem) {
                        itemId = env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
                    }
                    env->DeleteLocalRef(item);
                }
            }
            env->DeleteLocalRef(itemStackCls);
            env->DeleteLocalRef(itemCls);
        }
        env->DeleteLocalRef(itemStack);
    }

    env->DeleteLocalRef(armorInventory);
    env->DeleteLocalRef(inventoryCls);
    env->DeleteLocalRef(inventory);
    env->DeleteLocalRef(playerCls);
    env->DeleteLocalRef(localPlayer);

    return itemId;
}

int ArmorSwitch::getEquippedArmorDamage(int slotType) {
    if (!player.isValid())
        return 0;

    JNIEnv* env = Java::Env();
    if (!env)
        return 0;

    jobject localPlayer = mc.getPlayer();
    if (!localPlayer)
        return 0;

    jclass playerCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jfieldID fid_inventory = env->GetFieldID(playerCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
    if (!fid_inventory) {
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jobject inventory = env->GetObjectField(localPlayer, fid_inventory);
    if (!inventory) {
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jclass inventoryCls = env->GetObjectClass(inventory);
    jfieldID fid_armorInventory = env->GetFieldID(inventoryCls, "armorInventory", "[Lnet/minecraft/item/ItemStack;");
    if (!fid_armorInventory) {
        env->ExceptionClear();
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    jobjectArray armorInventory = (jobjectArray)env->GetObjectField(inventory, fid_armorInventory);
    if (!armorInventory) {
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(playerCls);
        env->DeleteLocalRef(localPlayer);
        return 0;
    }

    // armorInventory indices: 0=boots, 1=legs, 2=chest, 3=helmet
    // Our slotType: 0=helmet, 1=chest, 2=legs, 3=boots
    int armorIdx = 3 - slotType;

    int damage = 0;
    jobject itemStack = env->GetObjectArrayElement(armorInventory, armorIdx);
    if (itemStack) {
        if (JniCache::mid_getItemDamage) {
            damage = env->CallIntMethod(itemStack, JniCache::mid_getItemDamage);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                if (JniCache::fid_itemDamage) {
                    damage = env->GetIntField(itemStack, JniCache::fid_itemDamage);
                } else {
                    damage = 0;
                }
            }
        } else if (JniCache::fid_itemDamage) {
            damage = env->GetIntField(itemStack, JniCache::fid_itemDamage);
        } else {
            damage = 0;
        }
        env->DeleteLocalRef(itemStack);
    }

    env->DeleteLocalRef(armorInventory);
    env->DeleteLocalRef(inventoryCls);
    env->DeleteLocalRef(inventory);
    env->DeleteLocalRef(playerCls);
    env->DeleteLocalRef(localPlayer);

    return damage;
}

int ArmorSwitch::findArmorInInventory(int slotType, ArmorMaterial material, int* outDamage) {
    auto slots = mc.getInventorySlots();
    int targetId = getArmorId(material, slotType);

    bool found = false;
    int bestSlot = -1;
    int bestDamage = 0;

    // Search inventory and hotbar (slots 0-35)
    for (const auto& slot : slots) {
        if (slot.slotId >= 0 && slot.slotId <= 35 && slot.itemId == targetId) {
            if (!found || slot.damage < bestDamage) {
                found = true;
                bestSlot = slot.slotId;
                bestDamage = slot.damage;
            }
        }
    }

    if (found) {
        if (outDamage)
            *outDamage = bestDamage;
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
        int currentId = getEquippedArmorId(slotType);

        // TODO: make durability selection work in 1.7.10.
        int sourceDamage = 0;
        int sourceSlot = -1;
        if (currentId == targetId && currentId != 0) {
            sourceSlot = findArmorInInventory(slotType, targetMat, &sourceDamage);
            if (sourceSlot == -1) {
                continue;
            }

            int currentDamage = getEquippedArmorDamage(slotType);
            if (currentDamage <= sourceDamage) {
                continue;
            }
        } else {
            sourceSlot = findArmorInInventory(slotType, targetMat, &sourceDamage);
            if (sourceSlot == -1) {
                continue;
            }
        }

        int hotbarIndex = -1;
        if (sourceSlot >= 0 && sourceSlot <= 8) {
            hotbarIndex = sourceSlot;
            hotbarReserved[hotbarIndex] = true;
        } else {
            for (int i = 0; i < 9; i++) {
                if (!hotbarOccupied[i] && !hotbarReserved[i]) {
                    hotbarIndex = i;
                    hotbarReserved[i] = true;
                    break;
                }
            }

            if (hotbarIndex == -1) {
                // Hotbar full: use slots 2-5 (indices 2-5) as swap targets.
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

            SwapTask moveTask;
            moveTask.containerSlot = sourceSlot;  // main inventory slot 9-35
            moveTask.hotbarIndex = hotbarIndex;
            pendingSwaps.push_back(moveTask);

            SwapTask equipTask;
            equipTask.containerSlot = 5 + slotType;  // 5=helmet, 6=chest, 7=legs, 8=boots
            equipTask.hotbarIndex = hotbarIndex;
            pendingSwaps.push_back(equipTask);

            SwapTask restoreTask;
            restoreTask.containerSlot = sourceSlot;  // move old armor back into the original source slot
            restoreTask.hotbarIndex = hotbarIndex;
            pendingSwaps.push_back(restoreTask);
            continue;
        }

        SwapTask equipTask;
        equipTask.containerSlot = 5 + slotType;  // 5=helmet, 6=chest, 7=legs, 8=boots
        equipTask.hotbarIndex = hotbarIndex;
        pendingSwaps.push_back(equipTask);
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
