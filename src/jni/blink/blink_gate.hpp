#pragma once

#include "../java.h"

namespace BlinkGate {

// chequea si el paquete debe ser bloqueado por Blink
// retorna true si el paquete debe enviarse, false si debe bloquearse
bool ShouldSendPacket(JNIEnv* env, jobject packet);

// llamado desde NativeShouldSend en el packet interceptor
jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet);

}  // namespace BlinkGate
