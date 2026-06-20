#pragma once

#include "../../jni/java.h"

namespace SlowDownGate {

// Called from EntityPlayerSP.onLivingUpdate (JNI thread) when the player
// is about to have their movement input multiplied by 0.2 due to item use.
// Receives the EntityPlayerSP instance and overwrites moveForward/moveStrafe.
void JNICALL NativeOnSlowDown(JNIEnv* env, jclass, jobject playerInstance);

// Initialize: register the SlowDownHook native class and method
bool Init(JNIEnv* env);

void Shutdown(JNIEnv* env);

}  // namespace SlowDownGate
