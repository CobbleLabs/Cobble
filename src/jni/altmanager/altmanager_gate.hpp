#pragma once

#include "../java.h"

namespace AltManagerGate {

void MaybeSpoofLoginPacket(JNIEnv *env, jobject packet);
void Shutdown(JNIEnv* env);

} // namespace AltManagerGate
