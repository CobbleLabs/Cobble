#pragma once

#include "../java.h"

namespace BytecodePatcher {

struct HookTargets {
    bool outgoing = false;
    bool incoming = false;
    bool customPayload = false;
    bool teams = false;
    bool lunarNametags = false;
};

void SetTargets(const HookTargets& targets);

bool RegisterHooks(JNIEnv* env, jboolean(JNICALL* shouldSend)(JNIEnv*, jclass, jobject),
                   void (JNICALL* onReceive)(JNIEnv*, jclass, jobject));

bool InstallBytecodeHook(JNIEnv* env);

void Shutdown(JNIEnv* env);

}  // namespace BytecodePatcher
