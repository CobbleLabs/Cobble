#pragma once

#include "../java.h"

// ============================================================================
// OutgoingPacketGate - Unified gate for all outgoing packet filters
// Combines NoItemRelease, Blink, and future outgoing packet modules
// ============================================================================

namespace OutgoingPacketGate {

// llamado desde bytecode patch de NetworkManager.sendPacket
// retorna JNI_TRUE si el paquete debe enviarse, JNI_FALSE si debe bloquearse
jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet);

}  // namespace OutgoingPacketGate
