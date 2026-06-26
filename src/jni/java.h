#pragma once

#include <string>

#include "../../infra/ext/jni/jni.h"
#include "../../infra/ext/jni/jvmti.h"
#include "../sdk/version.h"

// ============================================================================
// JNI Thread-Safe Access
// JNIEnv* is thread-local by JNI spec. Each thread must have its own env.
// Use Java::Env() to get the correct env for the current thread.
// ============================================================================

struct Java {
    static void Init();
    static void Kill();

    // ALWAYS use this to get JNIEnv - returns thread-local env
    // Automatically attaches thread to JVM if needed
    static JNIEnv* Env();

    static bool AssignClass(std::string name, jclass& out);
    static void GetMinecraftVersion();

    // Global state (safe to access from any thread)
    static inline JavaVM* vm = nullptr;
    static inline jvmtiEnv* tiEnv = nullptr;
    static inline bool Initialized = false;
    static inline jobject ClassLoader = nullptr;
    static inline jmethodID MidFindClass = nullptr;
    static inline MinecraftVersion Version = MinecraftVersion::UNDEFINED;
};
