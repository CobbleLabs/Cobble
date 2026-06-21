#pragma once

#include "../../jni/java.h"

namespace SlowDownGate {

// Called from EntityPlayerSP.onLivingUpdate as a low-latency supplement to
// the module update fallback.
void JNICALL NativeOnSlowDown(JNIEnv* env, jclass, jobject playerInstance);

// Initialize: register the SlowDownHook native class and method
bool Init(JNIEnv* env);

void Shutdown(JNIEnv* env);

}  // namespace SlowDownGate
