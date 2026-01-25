#pragma once

#include "../java.h"

namespace ApolloPacketObserver {

void JNICALL NativeOnReceive(JNIEnv* env, jclass, jobject packet);

// Boost debug logging budget for Apollo payload types (useful when forcing requests)
void BoostLogBudget(int count);
// Force a limited number of nametag logs even if colors didn't change
void ForceLogNametags(int count);

}  // namespace ApolloPacketObserver
