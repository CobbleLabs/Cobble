// Inventory-related functions for Minecraft SDK
// Handles inventory slots, window click, and inventory screens

#include "../../util/logger.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"

// =============================================================================
// Helper: Get item ID from ItemStack (uses cached JNI IDs)
// =============================================================================

int getItemIdFromStack(JNIEnv* env, jobject itemStack) {
    if (!itemStack || !JniCache::mid_getItem || !JniCache::cls_Item || !JniCache::mid_getIdFromItem)
        return -1;

    jobject item = env->CallObjectMethod(itemStack, JniCache::mid_getItem);
    if (!item)
        return -1;

    int id = env->CallStaticIntMethod(JniCache::cls_Item, JniCache::mid_getIdFromItem, item);
    env->DeleteLocalRef(item);
    return id;
}

// =============================================================================
// getInventorySlots - Get all 36 inventory slots info
// =============================================================================

std::vector<SlotInfo> Minecraft::getInventorySlots() {
    std::vector<SlotInfo> result;
    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    jobject localPlayer = getPlayer();
    if (!localPlayer)
        return result;

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

    // Get NBT classes for enchantment reading
    jclass nbtCompoundCls = nullptr;
    Java::AssignClass("net.minecraft.nbt.NBTTagCompound", nbtCompoundCls);
    jmethodID mid_getShort = nullptr;
    if (nbtCompoundCls) {
        mid_getShort = env->GetMethodID(nbtCompoundCls, "getShort", "(Ljava/lang/String;)S");
    }

    // Cache strings for enchantment lookup
    jstring strId = env->NewStringUTF("id");
    jstring strLvl = env->NewStringUTF("lvl");

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

        // Get item ID using helper
        slot.itemId = getItemIdFromStack(env, itemStack);
        if (slot.itemId < 0) {
            env->DeleteLocalRef(itemStack);
            result.push_back(slot);
            continue;
        }

        if (JniCache::fid_stackSize) {
            slot.stackSize = env->GetIntField(itemStack, JniCache::fid_stackSize);
        }

        if (JniCache::mid_getItemDamage) {
            slot.damage = env->CallIntMethod(itemStack, JniCache::mid_getItemDamage);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                // Try direct field as fallback
                if (JniCache::fid_itemDamage) {
                    slot.damage = env->GetIntField(itemStack, JniCache::fid_itemDamage);
                } else {
                    slot.damage = 0;
                }
            }
        } else if (JniCache::fid_itemDamage) {
            slot.damage = env->GetIntField(itemStack, JniCache::fid_itemDamage);
        }

        // Check if it's a potion (ID 373)
        if (slot.itemId == 373) {
            // Splash detection via metadata bit 14 (0x4000 = 16384)
            slot.isSplashPotion = (slot.damage & 0x4000) != 0;

            // Check if health potion by damage value
            if (slot.damage == 8197 || slot.damage == 8229 || slot.damage == 16389 || slot.damage == 16421) {
                slot.isHealthPotion = true;
            }
        }

        // Get Protection enchantment level for armor items (IDs 298-317)
        if (slot.itemId >= 298 && slot.itemId <= 317 && mid_getShort && nbtCompoundCls) {
            jclass stackCls = env->GetObjectClass(itemStack);
            jmethodID mid_getEnchantTagList = env->GetMethodID(stackCls, "getEnchantmentTagList", "()Lnet/minecraft/nbt/NBTTagList;");
            env->DeleteLocalRef(stackCls);
            
            if (mid_getEnchantTagList) {
                jobject tagList = env->CallObjectMethod(itemStack, mid_getEnchantTagList);
                if (tagList && !env->ExceptionCheck()) {
                    jclass tagListCls = env->GetObjectClass(tagList);
                    jmethodID mid_tagCount = env->GetMethodID(tagListCls, "tagCount", "()I");
                    jmethodID mid_getCompoundAt = env->GetMethodID(tagListCls, "getCompoundTagAt", "(I)Lnet/minecraft/nbt/NBTTagCompound;");
                    env->DeleteLocalRef(tagListCls);
                    
                    if (mid_tagCount && mid_getCompoundAt) {
                        int count = env->CallIntMethod(tagList, mid_tagCount);
                        for (int e = 0; e < count; e++) {
                            jobject compound = env->CallObjectMethod(tagList, mid_getCompoundAt, e);
                            if (compound) {
                                short enchantId = env->CallShortMethod(compound, mid_getShort, strId);
                                if (enchantId == 0) {  // Protection enchant ID = 0
                                    slot.protectionLevel = env->CallShortMethod(compound, mid_getShort, strLvl);
                                    env->DeleteLocalRef(compound);
                                    break;
                                }
                                env->DeleteLocalRef(compound);
                            }
                        }
                    }
                    env->DeleteLocalRef(tagList);
                } else {
                    env->ExceptionClear();
                }
            }
        }

        env->DeleteLocalRef(itemStack);
        result.push_back(slot);
    }

    // Cleanup
    env->DeleteLocalRef(strId);
    env->DeleteLocalRef(strLvl);
    env->DeleteLocalRef(mainInventory);
    if (nbtCompoundCls)
        env->DeleteLocalRef(nbtCompoundCls);
    env->DeleteLocalRef(inventoryCls);
    env->DeleteLocalRef(inventory);
    env->DeleteLocalRef(localPlayer);

    return result;
}

// =============================================================================
// getArmorSlots - Get equipped armor slots info (0=helmet, 1=chest, 2=legs, 3=boots)
// =============================================================================

std::vector<SlotInfo> Minecraft::getArmorSlots() {
    std::vector<SlotInfo> result;
    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    jobject localPlayer = getPlayer();
    if (!localPlayer)
        return result;

    if (!JniCache::fid_inventory) {
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    jobject inventory = env->GetObjectField(localPlayer, JniCache::fid_inventory);
    if (!inventory) {
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    jclass inventoryCls = env->GetObjectClass(inventory);
    jfieldID fid_armorInventory = env->GetFieldID(inventoryCls, "armorInventory", "[Lnet/minecraft/item/ItemStack;");
    if (!fid_armorInventory) {
        env->ExceptionClear();
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    jobjectArray armorInventory = (jobjectArray)env->GetObjectField(inventory, fid_armorInventory);
    if (!armorInventory) {
        env->DeleteLocalRef(inventoryCls);
        env->DeleteLocalRef(inventory);
        env->DeleteLocalRef(localPlayer);
        return result;
    }

    // NBT for enchantment reading
    jclass nbtCompoundCls = nullptr;
    Java::AssignClass("net.minecraft.nbt.NBTTagCompound", nbtCompoundCls);
    jmethodID mid_getShort = nullptr;
    if (nbtCompoundCls) {
        mid_getShort = env->GetMethodID(nbtCompoundCls, "getShort", "(Ljava/lang/String;)S");
    }
    jstring strId = env->NewStringUTF("id");
    jstring strLvl = env->NewStringUTF("lvl");

    // armorInventory indices: 0=boots, 1=legs, 2=chest, 3=helmet
    // We return: 0=helmet, 1=chest, 2=legs, 3=boots
    for (int slotType = 0; slotType < 4; slotType++) {
        SlotInfo slot;
        slot.slotId = slotType;

        int armorIdx = 3 - slotType;  // Convert our index to MC index
        jobject itemStack = env->GetObjectArrayElement(armorInventory, armorIdx);
        if (!itemStack) {
            result.push_back(slot);
            continue;
        }

        // Get item ID using helper
        slot.itemId = getItemIdFromStack(env, itemStack);
        if (slot.itemId < 0) {
            env->DeleteLocalRef(itemStack);
            result.push_back(slot);
            continue;
        }

        if (JniCache::mid_getItemDamage) {
            slot.damage = env->CallIntMethod(itemStack, JniCache::mid_getItemDamage);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                slot.damage = 0;
            }
        }

        // Get Protection enchantment
        if (mid_getShort && nbtCompoundCls) {
            jclass stackCls = env->GetObjectClass(itemStack);
            jmethodID mid_getEnchantTagList = env->GetMethodID(stackCls, "getEnchantmentTagList", "()Lnet/minecraft/nbt/NBTTagList;");
            env->DeleteLocalRef(stackCls);

            if (mid_getEnchantTagList) {
                jobject tagList = env->CallObjectMethod(itemStack, mid_getEnchantTagList);
                if (tagList && !env->ExceptionCheck()) {
                    jclass tagListCls = env->GetObjectClass(tagList);
                    jmethodID mid_tagCount = env->GetMethodID(tagListCls, "tagCount", "()I");
                    jmethodID mid_getCompoundAt = env->GetMethodID(tagListCls, "getCompoundTagAt", "(I)Lnet/minecraft/nbt/NBTTagCompound;");
                    env->DeleteLocalRef(tagListCls);

                    if (mid_tagCount && mid_getCompoundAt) {
                        int count = env->CallIntMethod(tagList, mid_tagCount);
                        for (int e = 0; e < count; e++) {
                            jobject compound = env->CallObjectMethod(tagList, mid_getCompoundAt, e);
                            if (compound) {
                                short enchantId = env->CallShortMethod(compound, mid_getShort, strId);
                                if (enchantId == 0) {
                                    slot.protectionLevel = env->CallShortMethod(compound, mid_getShort, strLvl);
                                    env->DeleteLocalRef(compound);
                                    break;
                                }
                                env->DeleteLocalRef(compound);
                            }
                        }
                    }
                    env->DeleteLocalRef(tagList);
                } else {
                    env->ExceptionClear();
                }
            }
        }

        env->DeleteLocalRef(itemStack);
        result.push_back(slot);
    }

    env->DeleteLocalRef(strId);
    env->DeleteLocalRef(strLvl);
    env->DeleteLocalRef(armorInventory);
    if (nbtCompoundCls)
        env->DeleteLocalRef(nbtCompoundCls);
    env->DeleteLocalRef(inventoryCls);
    env->DeleteLocalRef(inventory);
    env->DeleteLocalRef(localPlayer);

    return result;
}

// =============================================================================
// windowClick - Click an inventory slot (shift-click)
// =============================================================================

void Minecraft::windowClick(int slotId, int button) {
    if (!isValid())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    jobject localPlayer = getPlayer();
    if (!localPlayer)
        return;

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
    jmethodID mid_windowClick = env->GetMethodID(
        controllerCls, "windowClick", "(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;");

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

    log_debug("[windowClick] slotId=%d -> containerSlot=%d, windowId=%d, button=%d, mode=1 (shift)", slotId,
              containerSlot, windowId, button);

    // Call windowClick (mode 1 = shift-click)
    jobject result =
        env->CallObjectMethod(playerController, mid_windowClick, windowId, containerSlot, button, 1, localPlayer);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (result)
        env->DeleteLocalRef(result);
    env->DeleteLocalRef(controllerCls);
    env->DeleteLocalRef(playerController);
    env->DeleteLocalRef(localPlayer);
}

// =============================================================================
// windowClickSwap - Swap a container slot with a hotbar slot (mode 2)
// This is like pressing a number key while hovering over a slot
// =============================================================================

void Minecraft::windowClickSwap(int containerSlot, int hotbarIndex) {
    if (!isValid())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    jobject localPlayer = getPlayer();
    if (!localPlayer)
        return;

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
    jmethodID mid_windowClick = env->GetMethodID(
        controllerCls, "windowClick", "(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;");

    if (!mid_windowClick) {
        env->DeleteLocalRef(controllerCls);
        env->DeleteLocalRef(playerController);
        env->DeleteLocalRef(localPlayer);
        return;
    }

    // Call windowClick with mode 2 (hotbar swap), button = hotbar index
    jobject result =
        env->CallObjectMethod(playerController, mid_windowClick, windowId, containerSlot, hotbarIndex, 2, localPlayer);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (result)
        env->DeleteLocalRef(result);
    env->DeleteLocalRef(controllerCls);
    env->DeleteLocalRef(playerController);
    env->DeleteLocalRef(localPlayer);
}

// =============================================================================
// openInventory - Open the player inventory GUI
// =============================================================================

void Minecraft::openInventory() {
    if (!isValid())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    jobject localPlayer = getPlayer();
    if (!localPlayer)
        return;

    // Create GuiInventory
    if (!JniCache::cls_GuiInventory) {
        env->DeleteLocalRef(localPlayer);
        return;
    }

    jmethodID mid_init =
        env->GetMethodID(JniCache::cls_GuiInventory, "<init>", "(Lnet/minecraft/entity/player/EntityPlayer;)V");
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
    jmethodID mid_displayGuiScreen =
        env->GetMethodID(cls, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
    if (!mid_displayGuiScreen) {
        env->ExceptionClear();
        mid_displayGuiScreen = env->GetMethodID(cls, "func_147108_a", "(Lnet/minecraft/client/gui/GuiScreen;)V");
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

void Minecraft::closeInventory() {
    if (!isValid())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    jobject localPlayer = getPlayer();
    if (!localPlayer)
        return;

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

bool Minecraft::isInventoryOpen() {
    if (!isValid())
        return false;

    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    if (!JniCache::fid_currentScreen)
        return false;

    jobject screen = env->GetObjectField(instance, JniCache::fid_currentScreen);
    if (!screen)
        return false;

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
