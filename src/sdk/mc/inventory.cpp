// Inventory-related functions for Minecraft SDK
// Handles inventory slots, window click, and inventory screens

#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../../util/logger.hpp"

// =============================================================================
// getInventorySlots - Get all 36 inventory slots info
// =============================================================================

std::vector<SlotInfo> Minecraft::getInventorySlots()
{
    std::vector<SlotInfo> result;
    if (!isValid()) return result;

    JNIEnv* env = Java::GetEnv();
    if (!env) return result;

    jobject localPlayer = getPlayer();
    if (!localPlayer) return result;

    // Get inventory from player
    if (!JniCache::fid_inventory) {
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    jobject inventory = env->GetObjectField(localPlayer, JniCache::fid_inventory);
    if (!inventory) {
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    // Get InventoryPlayer class for mainInventory field
    jclass inventoryCls = env->GetObjectClass(inventory);
    if (!inventoryCls) {
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    // Get mainInventory array field
    jfieldID fid_mainInventory = env->GetFieldID(inventoryCls, "mainInventory", "[Lnet/minecraft/item/ItemStack;");
    if (!fid_mainInventory) {
        env->ExceptionClear();
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    jobjectArray mainInventory = (jobjectArray)env->GetObjectField(inventory, fid_mainInventory);
    if (!mainInventory) {
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    // Get Item class for getIdFromItem
    jclass itemCls = nullptr;
    Java::AssignClass("net.minecraft.item.Item", itemCls);
    
    jmethodID mid_getIdFromItem = nullptr;
    if (itemCls) {
        mid_getIdFromItem = env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");
    }

    // Get ItemPotion class for splash detection
    jclass potionCls = nullptr;
    Java::AssignClass("net.minecraft.item.ItemPotion", potionCls);
    jmethodID mid_isSplash = nullptr;
    if (potionCls) {
        mid_isSplash = env->GetMethodID(potionCls, "isSplash", "(I)Z");
    }

    int arrayLen = env->GetArrayLength(mainInventory);

    // Iterate all 36 slots (0-8 = hotbar, 9-35 = main inventory)
    for (int i = 0; i < arrayLen && i < 36; i++) {
        SlotInfo slot;
        slot.slotId = i;

        jobject itemStack = env->GetObjectArrayElement(mainInventory, i);
        if (!itemStack) {
            result.push_back(slot);
            continue;
        }

        // Get item from stack
        jobject item = nullptr;
        if (JniCache::mid_getItem) {
            item = env->CallObjectMethod(itemStack, JniCache::mid_getItem);
        }
        
        if (!item) {
            env->DeleteLocalRef(itemStack);
            result.push_back(slot);
            continue;
        }

        // Get item ID
        if (mid_getIdFromItem) {
            slot.itemId = env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
        }
        
        if (JniCache::fid_stackSize) {
            slot.stackSize = env->GetIntField(itemStack, JniCache::fid_stackSize);
        }
        
        if (JniCache::mid_getItemDamage) {
            slot.damage = env->CallIntMethod(itemStack, JniCache::mid_getItemDamage);
        }

        // Check if it's a potion (ID 373)
        if (slot.itemId == 373) {
            if (mid_isSplash && potionCls && env->IsInstanceOf(item, potionCls)) {
                slot.isSplashPotion = env->CallBooleanMethod(item, mid_isSplash, slot.damage);
            }
            
            // Check if health potion by damage value
            if (slot.damage == 8197 || slot.damage == 8229 || 
                slot.damage == 16389 || slot.damage == 16421) {
                slot.isHealthPotion = true;
            }
        }

        env->DeleteLocalRef(item);
        env->DeleteLocalRef(itemStack);
        result.push_back(slot);
    }

    // Cleanup
    env->DeleteLocalRef(mainInventory);
    if (potionCls) env->DeleteLocalRef(potionCls);
    if (itemCls) env->DeleteLocalRef(itemCls);
    env->DeleteLocalRef(inventoryCls);
    env->DeleteLocalRef(inventory);
    env->DeleteLocalRef(localPlayer);

    return result;
}

// =============================================================================
// windowClick - Click an inventory slot (shift-click)
// =============================================================================

void Minecraft::windowClick(int slotId, int button)
{
    if (!isValid()) return;

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    jobject localPlayer = getPlayer();
    if (!localPlayer) return;

    // Get openContainer from player
    if (!JniCache::fid_openContainer) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    jobject openContainer = env->GetObjectField(localPlayer, JniCache::fid_openContainer);
    if (!openContainer) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    // Get windowId from container
    int windowId = 0;
    if (JniCache::fid_windowId) {
        windowId = env->GetIntField(openContainer, JniCache::fid_windowId);
    }

    env->DeleteLocalRef(openContainer);

    // Get playerController from Minecraft
    if (!JniCache::fid_playerController) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    jobject playerController = env->GetObjectField(instance, JniCache::fid_playerController);
    if (!playerController) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    jclass controllerCls = env->GetObjectClass(playerController);
    
    // windowClick(int windowId, int slotId, int mouseButton, int mode, EntityPlayer player)
    jmethodID mid_windowClick = env->GetMethodID(controllerCls, "windowClick", 
        "(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;");

    if (!mid_windowClick) {
        env->DeleteLocalRef(controllerCls);
        env->DeleteLocalRef(playerController);
        env->DeleteLocalRef(localPlayer);
        return;
    }

    // Container slot mapping: hotbar (0-8) -> container slots 36-44
    int containerSlot = slotId;
    if (slotId >= 0 && slotId <= 8) {
        containerSlot = slotId + 36;
    }

    // Call windowClick (mode 1 = shift-click)
    jobject result = env->CallObjectMethod(playerController, mid_windowClick, 
        windowId, containerSlot, button, 1, localPlayer);
    
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (result) env->DeleteLocalRef(result);
    env->DeleteLocalRef(controllerCls);
    env->DeleteLocalRef(playerController);
    env->DeleteLocalRef(localPlayer);
}

// =============================================================================
// openInventory - Open the player inventory GUI
// =============================================================================

void Minecraft::openInventory()
{
    if (!isValid()) return;

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    jobject localPlayer = getPlayer();
    if (!localPlayer) return;

    // Create GuiInventory
    if (!JniCache::cls_GuiInventory) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    jmethodID mid_init = env->GetMethodID(JniCache::cls_GuiInventory, "<init>", 
        "(Lnet/minecraft/entity/player/EntityPlayer;)V");
    if (!mid_init) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    jobject guiInventory = env->NewObject(JniCache::cls_GuiInventory, mid_init, localPlayer);
    if (!guiInventory) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    // Display the GUI
    jmethodID mid_displayGuiScreen = env->GetMethodID(cls, "displayGuiScreen", 
        "(Lnet/minecraft/client/gui/GuiScreen;)V");
    if (!mid_displayGuiScreen) {
        env->ExceptionClear();
        mid_displayGuiScreen = env->GetMethodID(cls, "func_147108_a", 
            "(Lnet/minecraft/client/gui/GuiScreen;)V");
    }

    if (mid_displayGuiScreen) {
        env->CallVoidMethod(instance, mid_displayGuiScreen, guiInventory);
    }

    env->DeleteLocalRef(guiInventory);
    env->DeleteLocalRef(localPlayer);
}

// =============================================================================
// closeInventory - Close the current GUI
// =============================================================================

void Minecraft::closeInventory()
{
    if (!isValid()) return;

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    jobject localPlayer = getPlayer();
    if (!localPlayer) return;

    // Call player.closeScreen()
    if (!JniCache::cls_EntityPlayer) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    jmethodID mid_closeScreen = env->GetMethodID(JniCache::cls_EntityPlayer, "closeScreen", "()V");
    if (!mid_closeScreen) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    env->CallVoidMethod(localPlayer, mid_closeScreen);
    env->DeleteLocalRef(localPlayer);
}

// =============================================================================
// isInventoryOpen - Check if an inventory GUI is open
// =============================================================================

bool Minecraft::isInventoryOpen()
{
    if (!isValid()) return false;

    JNIEnv* env = Java::GetEnv();
    if (!env) return false;

    if (!JniCache::fid_currentScreen) return false;
    
    jobject screen = env->GetObjectField(instance, JniCache::fid_currentScreen);
    if (!screen) return false;

    // Check if it's a GuiContainer (inventory-type GUI)
    jclass guiContainerCls = nullptr;
    bool result = false;
    
    if (Java::AssignClass("net.minecraft.client.gui.inventory.GuiContainer", guiContainerCls)) {
        result = env->IsInstanceOf(screen, guiContainerCls);
        env->DeleteLocalRef(guiContainerCls);
    }

    env->DeleteLocalRef(screen);
    return result;
}
