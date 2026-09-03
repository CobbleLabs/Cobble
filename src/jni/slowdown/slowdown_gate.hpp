#pragma once

#include "../../jni/java.h"

namespace SlowDownGate {

// Called from EntityPlayerSP.onLivingUpdate as a low-latency supplement to
// the module update fallback.
void JNICALL NativeOnSlowDown(JNIEnv* env, jclass, jobject playerInstance);

// Initialize: register the SlowDownHook native class and method
bool Init(JNIEnv* env);

// Returns false if callbacks or native teardown are not quiescent; callers must
// keep the DLL loaded so retained native entrypoints and refs remain valid.
bool Shutdown(JNIEnv* env, bool safeToUnregister = true);

}  // namespace SlowDownGate
