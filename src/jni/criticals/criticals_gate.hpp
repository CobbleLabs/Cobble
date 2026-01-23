#pragma once

#include "../java.h"

namespace CriticalsGate {

// Check if the packet should be delayed by Criticals module
// Returns true if packet should be sent, false if should be blocked/delayed
bool ShouldSendPacket(JNIEnv* env, jobject packet);

// JNI callback (same signature as other gates)
jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet);

}  // namespace CriticalsGate
