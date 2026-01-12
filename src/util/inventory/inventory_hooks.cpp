#include "inventory_hooks.h"
#include "../logger.hpp"

int InventoryHooks::getItemIdFromStack(JNIEnv* env, jobject itemStack)
{
	if (!itemStack) return -1;
	
	// Get ItemStack class
	jclass itemStackCls = env->GetObjectClass(itemStack);
	if (!itemStackCls) return -1;
	
	// Get Item class
	jclass itemCls = nullptr;
	if (!Java::AssignClass("net.minecraft.item.Item", itemCls)) {
		env->DeleteLocalRef(itemStackCls);
		return -1;
	}
	
	// Get itemStack.getItem() method
	jmethodID mid_getItem = env->GetMethodID(itemStackCls, "getItem", "()Lnet/minecraft/item/Item;");
	if (!mid_getItem || env->ExceptionCheck()) {
		env->ExceptionClear();
		env->DeleteLocalRef(itemStackCls);
		env->DeleteLocalRef(itemCls);
		return -1;
	}
	
	jobject item = env->CallObjectMethod(itemStack, mid_getItem);
	if (!item || env->ExceptionCheck()) {
		env->ExceptionClear();
		env->DeleteLocalRef(itemStackCls);
		env->DeleteLocalRef(itemCls);
		return -1;
	}
	
	// Get Item.getIdFromItem static method
	jmethodID mid_getIdFromItem = env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");
	if (!mid_getIdFromItem || env->ExceptionCheck()) {
		env->ExceptionClear();
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(itemStackCls);
		env->DeleteLocalRef(itemCls);
		return -1;
	}
	
	int itemId = env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		itemId = -1;
	}
	
	// Cleanup
	env->DeleteLocalRef(item);
	env->DeleteLocalRef(itemStackCls);
	env->DeleteLocalRef(itemCls);
	
	return itemId;
}

bool InventoryHooks::install()
{
	if (!Java::tiEnv) {
		log_err("[InventoryHooks] JVMTI not available");
		return false;
	}
	
	// Setup Method Entry callback
	jvmtiEventCallbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	
	callbacks.MethodEntry = [](jvmtiEnv* jvmti_env, JNIEnv* jni_env,
	                           jthread thread, jmethodID method) {
		char* methodName = nullptr;
		jvmti_env->GetMethodName(method, &methodName, nullptr, nullptr);
		
		if (methodName && strcmp(methodName, "setInventorySlotContents") == 0) {
			// Get declaring class
			jclass declaringClass = nullptr;
			jvmti_env->GetMethodDeclaringClass(method, &declaringClass);
			
			if (declaringClass) {
				char* classSig = nullptr;
				jvmti_env->GetClassSignature(declaringClass, &classSig, nullptr);
				
				// Only hook InventoryPlayer.setInventorySlotContents
				if (classSig && strstr(classSig, "InventoryPlayer")) {
					// Get method parameters: (int slot, ItemStack stack)
					jint slot = -1;
					jvmtiError slotErr = jvmti_env->GetLocalInt(thread, 0, 1, &slot);
					
					jobject itemStack = nullptr;
					jvmtiError stackErr = jvmti_env->GetLocalObject(thread, 0, 2, &itemStack);
					
					if (slotErr == JVMTI_ERROR_NONE && stackErr == JVMTI_ERROR_NONE) {
							// Get item ID from stack
							int itemId = -1;
							if (itemStack) {
								itemId = InventoryHooks::getItemIdFromStack(jni_env, itemStack);
							}
							
							// Cache removed - hooks disabled for now
							// InventoryCache::onSlotChanged(slot, itemId);
							(void)itemId;  // Suppress unused warning
						
						if (itemStack) {
							jni_env->DeleteLocalRef(itemStack);
						}
					}
				}
				
				if (classSig) jvmti_env->Deallocate((unsigned char*)classSig);
				jni_env->DeleteLocalRef(declaringClass);
			}
		}
		
		if (methodName) jvmti_env->Deallocate((unsigned char*)methodName);
	};
	
	// Set callbacks
	jvmtiError err = Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(callbacks));
	if (err != JVMTI_ERROR_NONE) {
		log_err("[InventoryHooks] Failed to set callbacks: %d", err);
		return false;
	}
	
	// Enable Method Entry events
	err = Java::tiEnv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, nullptr);
	if (err != JVMTI_ERROR_NONE) {
		log_err("[InventoryHooks] Failed to enable Method Entry: %d", err);
		return false;
	}
	
	log_ok("[InventoryHooks] Installed successfully");
	return true;
}
