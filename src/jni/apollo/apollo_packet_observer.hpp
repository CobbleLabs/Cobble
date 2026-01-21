#pragma once

#include "../java.h"

namespace ApolloPacketObserver {

void JNICALL NativeOnReceive(JNIEnv* env, jclass, jobject packet);

}  // namespace ApolloPacketObserver
