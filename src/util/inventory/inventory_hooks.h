#pragma once

#include "../../jni/java.h"

// JVMTI-based inventory event hooks
// Hooks Minecraft's inventory modification methods to track state changes
namespace InventoryHooks {
	// Install JVMTI method hooks for inventory tracking
	bool install();
	
	// Helper to extract item ID from ItemStack
	int getItemIdFromStack(JNIEnv* env, jobject itemStack);
}
