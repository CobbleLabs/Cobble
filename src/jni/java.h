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
    static std::string NotchDescriptor(const std::string& descriptor);
    static void GetMinecraftVersion();

    // Global state (safe to access from any thread)
    static inline JavaVM* vm = nullptr;
    static inline jvmtiEnv* tiEnv = nullptr;
    static inline bool Initialized = false;
    static inline jobject ClassLoader = nullptr;
    // loadClass delegates to the parent loader.  findClass only searches the
    // selected loader itself and misses Minecraft classes in Forge/Lunar setups.
    static inline jmethodID MidLoadClass = nullptr;
    static inline jmethodID MidFindClass = nullptr;
    // True when the selected client exposes 1.8.9 Notch names (e.g. ave)
    // instead of MCP/SRG names.
    static inline bool RuntimeObfuscated = false;
    static inline MinecraftVersion Version = MinecraftVersion::UNDEFINED;
};
