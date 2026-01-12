// Minecraft SDK - Core module
// Contains SDK_Init and basic Minecraft instance methods

#include "minecraft.h"
#include "../jni/jni_cache.h"
#include "../../util/logger.hpp"
#include "../../util/inventory/inventory_hooks.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

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

	// KeyBinding cache for releaseMovementKeys (legacy, kept for compatibility)
	jclass kbCls = nullptr;
	if (Java::AssignClass("net.minecraft.client.settings.KeyBinding", kbCls)) {
		mc.cls_keyBinding = (jclass)Java::Env->NewGlobalRef(kbCls);
		
		mc.mid_unPressAllKeys = Java::Env->GetStaticMethodID(mc.cls_keyBinding, "unPressAllKeys", "()V");
		if (!mc.mid_unPressAllKeys) {
			Java::Env->ExceptionClear();
			mc.mid_unPressAllKeys = Java::Env->GetStaticMethodID(mc.cls_keyBinding, "func_74505_d", "()V");
		}
		
		mc.mid_setKeyBindState = Java::Env->GetStaticMethodID(mc.cls_keyBinding, "setKeyBindState", "(IZ)V");
		mc.mid_getKeyCode = Java::Env->GetMethodID(mc.cls_keyBinding, "getKeyCode", "()I");
		mc.fid_pressed = Java::Env->GetFieldID(mc.cls_keyBinding, "pressed", "Z");
	}

	// Initialize JNI cache (new centralized system)
	if (!JniCache::Init()) {
		log_warn("JniCache::Init failed - some features may not work");
	}

	// Initialize player (will lazy-load fields on first refresh)
	player.refresh();
	
	// Initialize inventory hooks for potion detection
	if (!InventoryHooks::install()) {
		log_warn("[SDK] Inventory hooks installation failed - potion detection disabled");
	}

	log_ok("SDK initialized");
}

// =============================================================================
// Core Minecraft instance methods
// =============================================================================

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

void Minecraft::setTimerSpeed(float speed)
{
	if (!isValid()) return;
	if (!JniCache::fid_timer || !JniCache::fid_timerSpeed) return;
	
	jobject timer = Java::Env->GetObjectField(instance, JniCache::fid_timer);
	if (!timer) return;
	
	Java::Env->SetFloatField(timer, JniCache::fid_timerSpeed, speed);
	Java::Env->DeleteLocalRef(timer);
}
