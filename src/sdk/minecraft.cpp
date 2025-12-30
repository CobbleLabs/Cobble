#include "minecraft.h"
#include "../util/logger.hpp"

Minecraft mc;

void SDK_Init()
{
	if (!Java::Initialized) {
		log_err("SDK_Init: java not initialized");
		return;
	}

	// Get Minecraft class
	if (!Java::AssignClass("net.minecraft.client.Minecraft", mc.cls)) {
		log_err("SDK_Init: failed to find Minecraft class");
		return;
	}

	// getMinecraft() -> Minecraft instance
	mc.mid_getMinecraft = Java::Env->GetStaticMethodID(mc.cls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
	if (!mc.mid_getMinecraft) {
		log_err("SDK_Init: failed to find getMinecraft");
		return;
	}

	mc.instance = Java::Env->CallStaticObjectMethod(mc.cls, mc.mid_getMinecraft);
	if (!mc.instance) {
		log_err("SDK_Init: getMinecraft returned null");
		return;
	}

	// thePlayer field - signature depends on version
	const char* playerSig = (Java::Version == MinecraftVersion::MC_1_7_10)
		? "Lnet/minecraft/client/entity/EntityClientPlayerMP;"
		: "Lnet/minecraft/client/entity/EntityPlayerSP;";
	mc.fid_thePlayer = Java::Env->GetFieldID(mc.cls, "thePlayer", playerSig);

	// theWorld field
	mc.fid_theWorld = Java::Env->GetFieldID(mc.cls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");

	// currentScreen field (to detect if GUI is open)
	mc.fid_currentScreen = Java::Env->GetFieldID(mc.cls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");

	// clickMouse method
	mc.mid_clickMouse = Java::Env->GetMethodID(mc.cls, "clickMouse", "()V");

	// Click delay fields
	mc.fid_rightClickDelayTimer = Java::Env->GetFieldID(mc.cls, "rightClickDelayTimer", "I");
	mc.fid_leftClickCounter = Java::Env->GetFieldID(mc.cls, "leftClickCounter", "I");

	// Initialize player (will lazy-load fields on first refresh)
	player.refresh();

	log_ok("SDK initialized");
}

jobject Minecraft::getPlayer()
{
	if (!isValid() || !fid_thePlayer) return nullptr;
	return Java::Env->GetObjectField(instance, fid_thePlayer);
}

jobject Minecraft::getWorld()
{
	if (!isValid() || !fid_theWorld) return nullptr;
	return Java::Env->GetObjectField(instance, fid_theWorld);
}

bool Minecraft::isInGui()
{
	if (!isValid() || !fid_currentScreen) return false;
	jobject screen = Java::Env->GetObjectField(instance, fid_currentScreen);
	bool result = (screen != nullptr);
	if (screen) Java::Env->DeleteLocalRef(screen);
	return result;
}

void Minecraft::clickMouse()
{
	if (!isValid() || !mid_clickMouse) return;
	Java::Env->CallVoidMethod(instance, mid_clickMouse);
}

int Minecraft::getRightClickDelay()
{
	if (!isValid() || !fid_rightClickDelayTimer) return 0;
	return Java::Env->GetIntField(instance, fid_rightClickDelayTimer);
}

void Minecraft::setRightClickDelay(int value)
{
	if (!isValid() || !fid_rightClickDelayTimer) return;
	Java::Env->SetIntField(instance, fid_rightClickDelayTimer, value);
}

void Minecraft::setLeftClickCounter(int value)
{
	if (!isValid() || !fid_leftClickCounter) return;
	Java::Env->SetIntField(instance, fid_leftClickCounter, value);
}

float Minecraft::getMouseSensitivity()
{
	if (!isValid()) return 0.5f; // Default

	// Get gameSettings field
	jfieldID fid_gameSettings = Java::Env->GetFieldID(cls, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;");
	if (!fid_gameSettings) return 0.5f;

	jobject gameSettings = Java::Env->GetObjectField(instance, fid_gameSettings);
	if (!gameSettings) return 0.5f;

	// Get GameSettings class and mouseSensitivity field
	jclass settingsClass = Java::Env->GetObjectClass(gameSettings);
	if (!settingsClass) {
		Java::Env->DeleteLocalRef(gameSettings);
		return 0.5f;
	}

	jfieldID fid_sensitivity = Java::Env->GetFieldID(settingsClass, "mouseSensitivity", "F");
	if (!fid_sensitivity) {
		Java::Env->DeleteLocalRef(settingsClass);
		Java::Env->DeleteLocalRef(gameSettings);
		return 0.5f;
	}

	float sensitivity = Java::Env->GetFloatField(gameSettings, fid_sensitivity);

	Java::Env->DeleteLocalRef(settingsClass);
	Java::Env->DeleteLocalRef(gameSettings);

	return sensitivity;
}

bool Minecraft::isTargetingBlock()
{
	if (!isValid()) return false;

	// Get objectMouseOver field
	jfieldID fid_objectMouseOver = Java::Env->GetFieldID(cls, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
	if (!fid_objectMouseOver) return false;

	jobject mouseOver = Java::Env->GetObjectField(instance, fid_objectMouseOver);
	if (!mouseOver) return false;

	// Get MovingObjectPosition class and typeOfHit field
	jclass mopClass = nullptr;
	if (!Java::AssignClass("net.minecraft.util.MovingObjectPosition", mopClass)) {
		Java::Env->DeleteLocalRef(mouseOver);
		return false;
	}

	jfieldID fid_typeOfHit = Java::Env->GetFieldID(mopClass, "typeOfHit", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_typeOfHit) {
		Java::Env->DeleteLocalRef(mouseOver);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject typeOfHit = Java::Env->GetObjectField(mouseOver, fid_typeOfHit);
	Java::Env->DeleteLocalRef(mouseOver);

	if (!typeOfHit) {
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	// Get the BLOCK enum constant
	jclass typeClass = Java::Env->GetObjectClass(typeOfHit);
	if (!typeClass) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jfieldID fid_block = Java::Env->GetStaticFieldID(typeClass, "BLOCK", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_block) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(typeClass);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject blockEnum = Java::Env->GetStaticObjectField(typeClass, fid_block);
	bool result = Java::Env->IsSameObject(typeOfHit, blockEnum);

	Java::Env->DeleteLocalRef(blockEnum);
	Java::Env->DeleteLocalRef(typeOfHit);
	Java::Env->DeleteLocalRef(typeClass);
	Java::Env->DeleteLocalRef(mopClass);

	return result;
}

std::vector<PlayerInfo> Minecraft::getPlayerList()
{
	std::vector<PlayerInfo> result;
	if (!isValid()) return result;

	jobject world = getWorld();
	if (!world) return result;

	jclass worldCls = nullptr;
	if (!Java::AssignClass("net.minecraft.client.multiplayer.WorldClient", worldCls)) {
		Java::Env->DeleteLocalRef(world);
		return result;
	}

	// Get playerEntities field (List<EntityPlayer>)
	jfieldID fid_playerEntities = Java::Env->GetFieldID(worldCls, "playerEntities", "Ljava/util/List;");
	if (!fid_playerEntities) {
		Java::Env->DeleteLocalRef(world);
		Java::Env->DeleteLocalRef(worldCls);
		return result;
	}

	jobject playerList = Java::Env->GetObjectField(world, fid_playerEntities);
	Java::Env->DeleteLocalRef(world);
	Java::Env->DeleteLocalRef(worldCls);

	if (!playerList) return result;

	// Get List methods
	jclass listCls = Java::Env->GetObjectClass(playerList);
	jmethodID mid_size = Java::Env->GetMethodID(listCls, "size", "()I");
	jmethodID mid_get = Java::Env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");

	if (!mid_size || !mid_get) {
		Java::Env->DeleteLocalRef(playerList);
		Java::Env->DeleteLocalRef(listCls);
		return result;
	}

	int size = Java::Env->CallIntMethod(playerList, mid_size);

	// Get Entity class for position fields
	jclass entityCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.Entity", entityCls)) {
		Java::Env->DeleteLocalRef(playerList);
		Java::Env->DeleteLocalRef(listCls);
		return result;
	}

	jfieldID fid_posX = Java::Env->GetFieldID(entityCls, "posX", "D");
	jfieldID fid_posY = Java::Env->GetFieldID(entityCls, "posY", "D");
	jfieldID fid_posZ = Java::Env->GetFieldID(entityCls, "posZ", "D");
	jfieldID fid_lastPosX = Java::Env->GetFieldID(entityCls, "lastTickPosX", "D");
	jfieldID fid_lastPosY = Java::Env->GetFieldID(entityCls, "lastTickPosY", "D");
	jfieldID fid_lastPosZ = Java::Env->GetFieldID(entityCls, "lastTickPosZ", "D");
	jmethodID mid_isInvisible = Java::Env->GetMethodID(entityCls, "isInvisible", "()Z");

	jobject localPlayer = getPlayer();

	for (int i = 0; i < size; i++) {
		jobject entity = Java::Env->CallObjectMethod(playerList, mid_get, i);
		if (!entity) continue;

		// Skip local player
		if (localPlayer && Java::Env->IsSameObject(entity, localPlayer)) {
			Java::Env->DeleteLocalRef(entity);
			continue;
		}

		PlayerInfo info;
		info.instance = entity;
		info.posX = Java::Env->GetDoubleField(entity, fid_posX);
		info.posY = Java::Env->GetDoubleField(entity, fid_posY);
		info.posZ = Java::Env->GetDoubleField(entity, fid_posZ);
		info.lastPosX = fid_lastPosX ? Java::Env->GetDoubleField(entity, fid_lastPosX) : info.posX;
		info.lastPosY = fid_lastPosY ? Java::Env->GetDoubleField(entity, fid_lastPosY) : info.posY;
		info.lastPosZ = fid_lastPosZ ? Java::Env->GetDoubleField(entity, fid_lastPosZ) : info.posZ;
		info.isInvisible = mid_isInvisible ? Java::Env->CallBooleanMethod(entity, mid_isInvisible) : false;
		
		// Get player name
		jmethodID mid_getName = Java::Env->GetMethodID(entityCls, "getName", "()Ljava/lang/String;");
		if (mid_getName) {
			jstring jname = (jstring)Java::Env->CallObjectMethod(entity, mid_getName);
			if (jname) {
				info.name = jstringToString(jname);
				Java::Env->DeleteLocalRef(jname);
			}
		}
		if (info.name.empty()) info.name = "Player";
		
		// Get health and hurtTime (EntityLivingBase)
		jclass livingCls = nullptr;
		if (Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls)) {
			jmethodID mid_getHealth = Java::Env->GetMethodID(livingCls, "getHealth", "()F");
			jmethodID mid_getMaxHealth = Java::Env->GetMethodID(livingCls, "getMaxHealth", "()F");
			jfieldID fid_hurtTime = Java::Env->GetFieldID(livingCls, "hurtTime", "I");
			
			if (mid_getHealth) {
				info.health = Java::Env->CallFloatMethod(entity, mid_getHealth);
			}
			if (mid_getMaxHealth) {
				info.maxHealth = Java::Env->CallFloatMethod(entity, mid_getMaxHealth);
			}
			if (fid_hurtTime) {
				info.hurtTime = Java::Env->GetIntField(entity, fid_hurtTime);
			}
			Java::Env->DeleteLocalRef(livingCls);
		}

		result.push_back(info);
	}

	if (localPlayer) Java::Env->DeleteLocalRef(localPlayer);
	Java::Env->DeleteLocalRef(entityCls);
	Java::Env->DeleteLocalRef(playerList);
	Java::Env->DeleteLocalRef(listCls);

	return result;
}

bool Minecraft::canEntityBeSeen(jobject target)
{
	if (!isValid() || !target) return false;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return false;

	// Get EntityLivingBase class for canEntityBeSeen method
	jclass livingCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return false;
	}

	jmethodID mid_canSee = Java::Env->GetMethodID(livingCls, "canEntityBeSeen", "(Lnet/minecraft/entity/Entity;)Z");
	if (!mid_canSee) {
		Java::Env->DeleteLocalRef(localPlayer);
		Java::Env->DeleteLocalRef(livingCls);
		return false;
	}

	bool result = Java::Env->CallBooleanMethod(localPlayer, mid_canSee, target);

	Java::Env->DeleteLocalRef(localPlayer);
	Java::Env->DeleteLocalRef(livingCls);

	return result;
}

RenderPos Minecraft::getRenderPos()
{
	RenderPos result;
	if (!isValid()) return result;
	
	// Get RenderManager from Minecraft
	jfieldID fid_renderManager = Java::Env->GetFieldID(cls, "renderManager", "Lnet/minecraft/client/renderer/entity/RenderManager;");
	if (!fid_renderManager) {
		Java::Env->ExceptionClear();
		return result;
	}
	
	jobject renderManager = Java::Env->GetObjectField(instance, fid_renderManager);
	if (!renderManager) return result;
	
	jclass rmCls = Java::Env->GetObjectClass(renderManager);
	
	jfieldID fid_renderPosX = Java::Env->GetFieldID(rmCls, "renderPosX", "D");
	jfieldID fid_renderPosY = Java::Env->GetFieldID(rmCls, "renderPosY", "D");
	jfieldID fid_renderPosZ = Java::Env->GetFieldID(rmCls, "renderPosZ", "D");
	
	if (fid_renderPosX && fid_renderPosY && fid_renderPosZ) {
		result.x = Java::Env->GetDoubleField(renderManager, fid_renderPosX);
		result.y = Java::Env->GetDoubleField(renderManager, fid_renderPosY);
		result.z = Java::Env->GetDoubleField(renderManager, fid_renderPosZ);
	}
	
	Java::Env->DeleteLocalRef(rmCls);
	Java::Env->DeleteLocalRef(renderManager);
	
	return result;
}

float Minecraft::getPartialTicks()
{
	if (!isValid()) return 0.0f;
	
	// Get Timer from Minecraft
	jfieldID fid_timer = Java::Env->GetFieldID(cls, "timer", "Lnet/minecraft/util/Timer;");
	if (!fid_timer) {
		Java::Env->ExceptionClear();
		return 0.0f;
	}
	
	jobject timer = Java::Env->GetObjectField(instance, fid_timer);
	if (!timer) return 0.0f;
	
	jclass timerCls = Java::Env->GetObjectClass(timer);
	
	// Try 1.8 field name first, then 1.7
	jfieldID fid_renderPartialTicks = Java::Env->GetFieldID(timerCls, "renderPartialTicks", "F");
	if (!fid_renderPartialTicks) {
		Java::Env->ExceptionClear();
		fid_renderPartialTicks = Java::Env->GetFieldID(timerCls, "field_74281_c", "F"); // SRG name
	}
	
	float result = 0.0f;
	if (fid_renderPartialTicks) {
		result = Java::Env->GetFloatField(timer, fid_renderPartialTicks);
	}
	
	Java::Env->DeleteLocalRef(timerCls);
	Java::Env->DeleteLocalRef(timer);
	
	return result;
}

std::vector<SlotInfo> Minecraft::getInventorySlots()
{
	std::vector<SlotInfo> result;
	if (!isValid()) return result;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return result;

	// Get EntityPlayer class for inventory field
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get InventoryPlayer class
	jclass inventoryCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get ItemStack class
	jclass itemStackCls = nullptr;
	if (!Java::AssignClass("net.minecraft.item.ItemStack", itemStackCls)) {
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get Item class
	jclass itemCls = nullptr;
	if (!Java::AssignClass("net.minecraft.item.Item", itemCls)) {
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// Get ItemPotion class for splash detection
	jclass potionCls = nullptr;
	Java::AssignClass("net.minecraft.item.ItemPotion", potionCls);

	// player.inventory field
	jfieldID fid_inventory = Java::Env->GetFieldID(playerCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
	if (!fid_inventory) {
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	jobject inventory = Java::Env->GetObjectField(localPlayer, fid_inventory);
	if (!inventory) {
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// inventory.mainInventory field (ItemStack[36])
	jfieldID fid_mainInventory = Java::Env->GetFieldID(inventoryCls, "mainInventory", "[Lnet/minecraft/item/ItemStack;");
	if (!fid_mainInventory) {
		Java::Env->DeleteLocalRef(inventory);
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	jobjectArray mainInventory = (jobjectArray)Java::Env->GetObjectField(inventory, fid_mainInventory);
	if (!mainInventory) {
		Java::Env->DeleteLocalRef(inventory);
		if (potionCls) Java::Env->DeleteLocalRef(potionCls);
		Java::Env->DeleteLocalRef(itemCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return result;
	}

	// ItemStack methods
	jmethodID mid_getItem = Java::Env->GetMethodID(itemStackCls, "getItem", "()Lnet/minecraft/item/Item;");
	jfieldID fid_stackSize = Java::Env->GetFieldID(itemStackCls, "stackSize", "I");
	jmethodID mid_getMetadata = Java::Env->GetMethodID(itemStackCls, "getMetadata", "()I");

	// Item.getIdFromItem static method
	jmethodID mid_getIdFromItem = Java::Env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");

	// ItemPotion.isSplash (if available)
	jmethodID mid_isSplash = nullptr;
	if (potionCls) {
		mid_isSplash = Java::Env->GetMethodID(potionCls, "isSplash", "(I)Z");
	}

	int arrayLen = Java::Env->GetArrayLength(mainInventory);
	
	// Iterate all 36 slots (0-8 = hotbar, 9-35 = main inventory)
	for (int i = 0; i < arrayLen && i < 36; i++) {
		SlotInfo slot;
		slot.slotId = i;

		jobject itemStack = Java::Env->GetObjectArrayElement(mainInventory, i);
		if (!itemStack) {
			// Empty slot
			result.push_back(slot);
			continue;
		}

		// Get item
		jobject item = Java::Env->CallObjectMethod(itemStack, mid_getItem);
		if (!item) {
			Java::Env->DeleteLocalRef(itemStack);
			result.push_back(slot);
			continue;
		}

		// Get item ID
		slot.itemId = Java::Env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
		slot.stackSize = Java::Env->GetIntField(itemStack, fid_stackSize);
		slot.damage = Java::Env->CallIntMethod(itemStack, mid_getMetadata);

		// Check if it's a potion (ID 373)
		if (slot.itemId == 373) {
			// Check splash via ItemPotion.isSplash(damage)
			if (mid_isSplash && potionCls) {
				// Need to check if item is instance of ItemPotion
				if (Java::Env->IsInstanceOf(item, potionCls)) {
					slot.isSplashPotion = Java::Env->CallBooleanMethod(item, mid_isSplash, slot.damage);
				}
			}
			
			// Check if health potion by damage value
			// 1.8.9 potion damage values:
			// 8197 = Instant Health I, 8229 = Instant Health II
			// 16389 = Splash Instant Health I, 16421 = Splash Instant Health II
			int baseDamage = slot.damage & 0x3F;  // Lower 6 bits contain effect ID
			// Effect ID 5 = Instant Health, ID 6 = Instant Health II
			// Actually in 1.8.9: damage 8197 & 0x3F = 5, damage 8229 & 0x3F = 5
			// Better approach: check if it contains Instant Health effect
			// Instant Health potions: 8197, 8229, 16389, 16421
			if (slot.damage == 8197 || slot.damage == 8229 || 
				slot.damage == 16389 || slot.damage == 16421) {
				slot.isHealthPotion = true;
			}
		}

		Java::Env->DeleteLocalRef(item);
		Java::Env->DeleteLocalRef(itemStack);
		result.push_back(slot);
	}

	// Cleanup
	Java::Env->DeleteLocalRef(mainInventory);
	Java::Env->DeleteLocalRef(inventory);
	if (potionCls) Java::Env->DeleteLocalRef(potionCls);
	Java::Env->DeleteLocalRef(itemCls);
	Java::Env->DeleteLocalRef(itemStackCls);
	Java::Env->DeleteLocalRef(inventoryCls);
	Java::Env->DeleteLocalRef(playerCls);
	Java::Env->DeleteLocalRef(localPlayer);

	return result;
}

void Minecraft::windowClick(int slotId, int button)
{
	if (!isValid()) return;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return;

	// Get player's openContainer to get the windowId
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// player.openContainer field
	jfieldID fid_openContainer = Java::Env->GetFieldID(playerCls, "openContainer", "Lnet/minecraft/inventory/Container;");
	if (!fid_openContainer) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jobject openContainer = Java::Env->GetObjectField(localPlayer, fid_openContainer);
	if (!openContainer) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Get Container class for windowId field
	jclass containerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.inventory.Container", containerCls)) {
		Java::Env->DeleteLocalRef(openContainer);
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// container.windowId field
	jfieldID fid_windowId = Java::Env->GetFieldID(containerCls, "windowId", "I");
	int windowId = 0;
	if (fid_windowId) {
		windowId = Java::Env->GetIntField(openContainer, fid_windowId);
	}

	Java::Env->DeleteLocalRef(containerCls);
	Java::Env->DeleteLocalRef(openContainer);
	Java::Env->DeleteLocalRef(playerCls);

	// Get playerController from Minecraft
	jfieldID fid_playerController = Java::Env->GetFieldID(cls, "playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;");
	if (!fid_playerController) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jobject playerController = Java::Env->GetObjectField(instance, fid_playerController);
	if (!playerController) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jclass controllerCls = Java::Env->GetObjectClass(playerController);

	// windowClick(int windowId, int slotId, int mouseButton, int mode, EntityPlayer player)
	// mode 1 = shift-click (quick move)
	jmethodID mid_windowClick = Java::Env->GetMethodID(controllerCls, "windowClick", 
		"(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;");

	if (!mid_windowClick) {
		Java::Env->DeleteLocalRef(controllerCls);
		Java::Env->DeleteLocalRef(playerController);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Container slot mapping:
	// For player inventory GUI (windowId 0):
	// - Crafting output: slot 0
	// - Crafting grid: slots 1-4
	// - Armor: slots 5-8
	// - Main inventory: slots 9-35
	// - Hotbar: slots 36-44
	
	int containerSlot = slotId;
	if (slotId >= 0 && slotId <= 8) {
		// Hotbar: mainInventory[0-8] -> container slots 36-44
		containerSlot = slotId + 36;
	}
	// else slotId 9-35 maps directly to container slots 9-35

	// Call windowClick with exception handling
	jobject result = Java::Env->CallObjectMethod(playerController, mid_windowClick, 
		windowId, containerSlot, button, 1, localPlayer);
	
	// Check for exception
	if (Java::Env->ExceptionCheck()) {
		Java::Env->ExceptionClear();
	}

	if (result) Java::Env->DeleteLocalRef(result);
	Java::Env->DeleteLocalRef(controllerCls);
	Java::Env->DeleteLocalRef(playerController);
	Java::Env->DeleteLocalRef(localPlayer);
}

void Minecraft::openInventory()
{
	if (!isValid()) return;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return;

	// Create GuiInventory and display it
	// new GuiInventory(player) then minecraft.displayGuiScreen(guiInventory)
	
	jclass guiInventoryCls = nullptr;
	if (!Java::AssignClass("net.minecraft.client.gui.inventory.GuiInventory", guiInventoryCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Constructor: GuiInventory(EntityPlayer player)
	jmethodID mid_init = Java::Env->GetMethodID(guiInventoryCls, "<init>", "(Lnet/minecraft/entity/player/EntityPlayer;)V");
	if (!mid_init) {
		Java::Env->DeleteLocalRef(guiInventoryCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jobject guiInventory = Java::Env->NewObject(guiInventoryCls, mid_init, localPlayer);
	if (!guiInventory) {
		Java::Env->DeleteLocalRef(guiInventoryCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	// Minecraft.displayGuiScreen(GuiScreen)
	jmethodID mid_displayGuiScreen = Java::Env->GetMethodID(cls, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V");
	if (!mid_displayGuiScreen) {
		Java::Env->DeleteLocalRef(guiInventory);
		Java::Env->DeleteLocalRef(guiInventoryCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	Java::Env->CallVoidMethod(instance, mid_displayGuiScreen, guiInventory);

	Java::Env->DeleteLocalRef(guiInventory);
	Java::Env->DeleteLocalRef(guiInventoryCls);
	Java::Env->DeleteLocalRef(localPlayer);
}

void Minecraft::closeInventory()
{
	if (!isValid()) return;

	jobject localPlayer = getPlayer();
	if (!localPlayer) return;

	// Call player.closeScreen()
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) {
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	jmethodID mid_closeScreen = Java::Env->GetMethodID(playerCls, "closeScreen", "()V");
	if (!mid_closeScreen) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(localPlayer);
		return;
	}

	Java::Env->CallVoidMethod(localPlayer, mid_closeScreen);

	Java::Env->DeleteLocalRef(playerCls);
	Java::Env->DeleteLocalRef(localPlayer);
}

bool Minecraft::isInventoryOpen()
{
	if (!isValid() || !fid_currentScreen) return false;
	
	jobject screen = Java::Env->GetObjectField(instance, fid_currentScreen);
	if (!screen) return false;

	// Check if it's a GuiInventory or GuiContainer
	jclass guiContainerCls = nullptr;
	bool result = false;
	
	if (Java::AssignClass("net.minecraft.client.gui.inventory.GuiContainer", guiContainerCls)) {
		result = Java::Env->IsInstanceOf(screen, guiContainerCls);
		Java::Env->DeleteLocalRef(guiContainerCls);
	}

	Java::Env->DeleteLocalRef(screen);
	return result;
}

bool Minecraft::isTargetingEntity()
{
	if (!isValid()) return false;

	// Get objectMouseOver field
	jfieldID fid_objectMouseOver = Java::Env->GetFieldID(cls, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
	if (!fid_objectMouseOver) return false;

	jobject mouseOver = Java::Env->GetObjectField(instance, fid_objectMouseOver);
	if (!mouseOver) return false;

	// Get MovingObjectPosition class and typeOfHit field
	jclass mopClass = nullptr;
	if (!Java::AssignClass("net.minecraft.util.MovingObjectPosition", mopClass)) {
		Java::Env->DeleteLocalRef(mouseOver);
		return false;
	}

	jfieldID fid_typeOfHit = Java::Env->GetFieldID(mopClass, "typeOfHit", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_typeOfHit) {
		Java::Env->DeleteLocalRef(mouseOver);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject typeOfHit = Java::Env->GetObjectField(mouseOver, fid_typeOfHit);
	Java::Env->DeleteLocalRef(mouseOver);

	if (!typeOfHit) {
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	// Get the ENTITY enum constant
	jclass typeClass = Java::Env->GetObjectClass(typeOfHit);
	if (!typeClass) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jfieldID fid_entity = Java::Env->GetStaticFieldID(typeClass, "ENTITY", "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
	if (!fid_entity) {
		Java::Env->DeleteLocalRef(typeOfHit);
		Java::Env->DeleteLocalRef(typeClass);
		Java::Env->DeleteLocalRef(mopClass);
		return false;
	}

	jobject entityEnum = Java::Env->GetStaticObjectField(typeClass, fid_entity);
	bool result = Java::Env->IsSameObject(typeOfHit, entityEnum);

	Java::Env->DeleteLocalRef(entityEnum);
	Java::Env->DeleteLocalRef(typeOfHit);
	Java::Env->DeleteLocalRef(typeClass);
	Java::Env->DeleteLocalRef(mopClass);

	return result;
}

int Minecraft::getTargetedEntityHurtTime()
{
	if (!isValid()) return 0;

	// Get objectMouseOver field
	jfieldID fid_objectMouseOver = Java::Env->GetFieldID(cls, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
	if (!fid_objectMouseOver) return 0;

	jobject mouseOver = Java::Env->GetObjectField(instance, fid_objectMouseOver);
	if (!mouseOver) return 0;

	// Get MovingObjectPosition class
	jclass mopClass = nullptr;
	if (!Java::AssignClass("net.minecraft.util.MovingObjectPosition", mopClass)) {
		Java::Env->DeleteLocalRef(mouseOver);
		return 0;
	}

	// Get entityHit field (the entity being looked at)
	jfieldID fid_entityHit = Java::Env->GetFieldID(mopClass, "entityHit", "Lnet/minecraft/entity/Entity;");
	if (!fid_entityHit) {
		Java::Env->DeleteLocalRef(mouseOver);
		Java::Env->DeleteLocalRef(mopClass);
		return 0;
	}

	jobject entityHit = Java::Env->GetObjectField(mouseOver, fid_entityHit);
	Java::Env->DeleteLocalRef(mouseOver);
	Java::Env->DeleteLocalRef(mopClass);

	if (!entityHit) return 0;

	// Get EntityLivingBase class for hurtTime
	jclass livingCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls)) {
		Java::Env->DeleteLocalRef(entityHit);
		return 0;
	}

	// Check if entity is an EntityLivingBase
	if (!Java::Env->IsInstanceOf(entityHit, livingCls)) {
		Java::Env->DeleteLocalRef(entityHit);
		Java::Env->DeleteLocalRef(livingCls);
		return 0;
	}

	jfieldID fid_hurtTime = Java::Env->GetFieldID(livingCls, "hurtTime", "I");
	if (!fid_hurtTime) {
		Java::Env->DeleteLocalRef(entityHit);
		Java::Env->DeleteLocalRef(livingCls);
		return 0;
	}

	int hurtTime = Java::Env->GetIntField(entityHit, fid_hurtTime);

	Java::Env->DeleteLocalRef(entityHit);
	Java::Env->DeleteLocalRef(livingCls);

	return hurtTime;
}
