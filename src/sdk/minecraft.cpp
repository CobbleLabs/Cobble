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
		info.isInvisible = mid_isInvisible ? Java::Env->CallBooleanMethod(entity, mid_isInvisible) : false;

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
