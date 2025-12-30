#include "player.h"
#include "minecraft.h"
#include "../util/logger.hpp"

LocalPlayer player;

void LocalPlayer::initFields()
{
	if (fieldsInitialized || !instance) return;

	// Get Entity class (base class with position, motion, rotation)
	// Must use AssignClass because Minecraft uses custom ClassLoader
	if (!Java::AssignClass("net.minecraft.entity.Entity", entityCls)) {
		log_err("LocalPlayer: failed to find Entity class");
		return;
	}

	// Get EntityLivingBase class (has health, hurtTime, jump)
	Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls);

	// Position fields (in Entity)
	fid_posX = Java::Env->GetFieldID(entityCls, "posX", "D");
	fid_posY = Java::Env->GetFieldID(entityCls, "posY", "D");
	fid_posZ = Java::Env->GetFieldID(entityCls, "posZ", "D");

	// Motion fields (in Entity)
	fid_motionX = Java::Env->GetFieldID(entityCls, "motionX", "D");
	fid_motionY = Java::Env->GetFieldID(entityCls, "motionY", "D");
	fid_motionZ = Java::Env->GetFieldID(entityCls, "motionZ", "D");

	// Rotation fields (in Entity)
	fid_rotationYaw = Java::Env->GetFieldID(entityCls, "rotationYaw", "F");
	fid_rotationPitch = Java::Env->GetFieldID(entityCls, "rotationPitch", "F");

	// On ground (in Entity)
	fid_onGround = Java::Env->GetFieldID(entityCls, "onGround", "Z");

	// Sprint/sneak methods (in Entity)
	mid_isSprinting = Java::Env->GetMethodID(entityCls, "isSprinting", "()Z");
	mid_setSprinting = Java::Env->GetMethodID(entityCls, "setSprinting", "(Z)V");
	mid_isSneaking = Java::Env->GetMethodID(entityCls, "isSneaking", "()Z");

	if (livingCls) {
		// Health (in EntityLivingBase)
		mid_getHealth = Java::Env->GetMethodID(livingCls, "getHealth", "()F");
		
		// HurtTime (in EntityLivingBase)
		fid_hurtTime = Java::Env->GetFieldID(livingCls, "hurtTime", "I");

		// Jump (in EntityLivingBase)
		mid_jump = Java::Env->GetMethodID(livingCls, "jump", "()V");
		
		// JumpTicks (in EntityLivingBase) - controls jump delay
		fid_jumpTicks = Java::Env->GetFieldID(livingCls, "jumpTicks", "I");
	}

	fieldsInitialized = true;
}

void LocalPlayer::refresh()
{
	// Get fresh player instance from Minecraft
	jobject newInstance = mc.getPlayer();
	
	// Delete old local ref if exists
	if (instance) {
		Java::Env->DeleteLocalRef(instance);
		instance = nullptr;
	}

	instance = newInstance;

	if (instance && !fieldsInitialized) {
		initFields();
	}
}

// Position getters
double LocalPlayer::getPosX()
{
	if (!isValid() || !fid_posX) return 0.0;
	return Java::Env->GetDoubleField(instance, fid_posX);
}

double LocalPlayer::getPosY()
{
	if (!isValid() || !fid_posY) return 0.0;
	return Java::Env->GetDoubleField(instance, fid_posY);
}

double LocalPlayer::getPosZ()
{
	if (!isValid() || !fid_posZ) return 0.0;
	return Java::Env->GetDoubleField(instance, fid_posZ);
}

// Motion getters/setters
double LocalPlayer::getMotionX()
{
	if (!isValid() || !fid_motionX) return 0.0;
	return Java::Env->GetDoubleField(instance, fid_motionX);
}

double LocalPlayer::getMotionY()
{
	if (!isValid() || !fid_motionY) return 0.0;
	return Java::Env->GetDoubleField(instance, fid_motionY);
}

double LocalPlayer::getMotionZ()
{
	if (!isValid() || !fid_motionZ) return 0.0;
	return Java::Env->GetDoubleField(instance, fid_motionZ);
}

void LocalPlayer::setMotionX(double value)
{
	if (!isValid() || !fid_motionX) return;
	Java::Env->SetDoubleField(instance, fid_motionX, value);
}

void LocalPlayer::setMotionY(double value)
{
	if (!isValid() || !fid_motionY) return;
	Java::Env->SetDoubleField(instance, fid_motionY, value);
}

void LocalPlayer::setMotionZ(double value)
{
	if (!isValid() || !fid_motionZ) return;
	Java::Env->SetDoubleField(instance, fid_motionZ, value);
}

// Rotation getters/setters
float LocalPlayer::getYaw()
{
	if (!isValid() || !fid_rotationYaw) return 0.0f;
	return Java::Env->GetFloatField(instance, fid_rotationYaw);
}

float LocalPlayer::getPitch()
{
	if (!isValid() || !fid_rotationPitch) return 0.0f;
	return Java::Env->GetFloatField(instance, fid_rotationPitch);
}

void LocalPlayer::setYaw(float value)
{
	if (!isValid() || !fid_rotationYaw) return;
	Java::Env->SetFloatField(instance, fid_rotationYaw, value);
}

void LocalPlayer::setPitch(float value)
{
	if (!isValid() || !fid_rotationPitch) return;
	Java::Env->SetFloatField(instance, fid_rotationPitch, value);
}

// State
bool LocalPlayer::isOnGround()
{
	if (!isValid() || !fid_onGround) return false;
	return Java::Env->GetBooleanField(instance, fid_onGround);
}

void LocalPlayer::setOnGround(bool value)
{
	if (!isValid() || !fid_onGround) return;
	Java::Env->SetBooleanField(instance, fid_onGround, value);
}

bool LocalPlayer::isSprinting()
{
	if (!isValid() || !mid_isSprinting) return false;
	return Java::Env->CallBooleanMethod(instance, mid_isSprinting);
}

void LocalPlayer::setSprinting(bool value)
{
	if (!isValid() || !mid_setSprinting) return;
	Java::Env->CallVoidMethod(instance, mid_setSprinting, value);
}

bool LocalPlayer::isSneaking()
{
	if (!isValid() || !mid_isSneaking) return false;
	return Java::Env->CallBooleanMethod(instance, mid_isSneaking);
}

// Combat
float LocalPlayer::getHealth()
{
	if (!isValid() || !mid_getHealth) return 0.0f;
	return Java::Env->CallFloatMethod(instance, mid_getHealth);
}

int LocalPlayer::getHurtTime()
{
	if (!isValid() || !fid_hurtTime) return 0;
	return Java::Env->GetIntField(instance, fid_hurtTime);
}

void LocalPlayer::jump()
{
	if (!isValid() || !mid_jump) return;
	Java::Env->CallVoidMethod(instance, mid_jump);
}

void LocalPlayer::setJumpTicks(int value)
{
	if (!isValid() || !fid_jumpTicks) return;
	Java::Env->SetIntField(instance, fid_jumpTicks, value);
}

int LocalPlayer::getHeldItemId()
{
	if (!isValid()) return -1;

	// Chain: player.inventory.getCurrentItem().getItem() -> Item.getIdFromItem(item)
	// Uses method calls instead of caching to keep implementation simple
	
	// Get EntityPlayer class for inventory field
	jclass playerCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls)) return -1;

	// Get InventoryPlayer class
	jclass inventoryCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
		Java::Env->DeleteLocalRef(playerCls);
		return -1;
	}

	// Get ItemStack class
	jclass itemStackCls = nullptr;
	if (!Java::AssignClass("net.minecraft.item.ItemStack", itemStackCls)) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		return -1;
	}

	// Get Item class
	jclass itemCls = nullptr;
	if (!Java::AssignClass("net.minecraft.item.Item", itemCls)) {
		Java::Env->DeleteLocalRef(playerCls);
		Java::Env->DeleteLocalRef(inventoryCls);
		Java::Env->DeleteLocalRef(itemStackCls);
		return -1;
	}

	int result = -1;

	// player.inventory field
	jfieldID fid_inventory = Java::Env->GetFieldID(playerCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
	if (!fid_inventory) goto cleanup;

	{
		jobject inventory = Java::Env->GetObjectField(instance, fid_inventory);
		if (!inventory) goto cleanup;

		// inventory.getCurrentItem()
		jmethodID mid_getCurrentItem = Java::Env->GetMethodID(inventoryCls, "getCurrentItem", "()Lnet/minecraft/item/ItemStack;");
		if (!mid_getCurrentItem) {
			Java::Env->DeleteLocalRef(inventory);
			goto cleanup;
		}

		jobject itemStack = Java::Env->CallObjectMethod(inventory, mid_getCurrentItem);
		Java::Env->DeleteLocalRef(inventory);

		if (!itemStack) goto cleanup; // Empty hand

		// itemStack.getItem()
		jmethodID mid_getItem = Java::Env->GetMethodID(itemStackCls, "getItem", "()Lnet/minecraft/item/Item;");
		if (!mid_getItem) {
			Java::Env->DeleteLocalRef(itemStack);
			goto cleanup;
		}

		jobject item = Java::Env->CallObjectMethod(itemStack, mid_getItem);
		Java::Env->DeleteLocalRef(itemStack);

		if (!item) goto cleanup;

		// Item.getIdFromItem(item) - static method
		jmethodID mid_getIdFromItem = Java::Env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");
		if (!mid_getIdFromItem) {
			Java::Env->DeleteLocalRef(item);
			goto cleanup;
		}

		result = Java::Env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
		Java::Env->DeleteLocalRef(item);
	}

cleanup:
	Java::Env->DeleteLocalRef(playerCls);
	Java::Env->DeleteLocalRef(inventoryCls);
	Java::Env->DeleteLocalRef(itemStackCls);
	Java::Env->DeleteLocalRef(itemCls);

	return result;
}
