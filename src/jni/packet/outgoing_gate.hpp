#pragma once

#include "../java.h"

// ============================================================================
// OutgoingPacketGate - Unified gate for all outgoing packet filters
// Combines NoItemRelease, Blink, and future outgoing packet modules
// ============================================================================

namespace OutgoingPacketGate {

// Called from bytecode patch of NetworkManager.sendPacket
// Returns JNI_TRUE if packet should be sent, JNI_FALSE if it should be blocked
jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet);

}  // namespace OutgoingPacketGate
