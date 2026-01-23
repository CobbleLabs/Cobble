#pragma once

#include "../java.h"

namespace BlinkGate {

// Check if packet should be blocked by Blink
// Returns true if packet should be sent, false if it should be blocked
bool ShouldSendPacket(JNIEnv* env, jobject packet);

// Called from NativeShouldSend in the packet interceptor
jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet);

}  // namespace BlinkGate
