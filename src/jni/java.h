#pragma once

#include <string>

#include "../../infra/ext/jni/jni.h"
#include "../../infra/ext/jni/jvmti.h"
#include "../sdk/version.h"
#include "mappings/runtime_mappings.hpp"

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
    // MCP descriptor with Minecraft classes renamed for the running client.
    static std::string RuntimeDescriptor(const std::string& descriptor);
    static void GetMinecraftVersion();

    // Same contract as the JNI functions (on failure the exception stays pending).
    // Take MCP or SRG names with MCP descriptors and translate them when the
    // running client renames members.
    static jfieldID GetFieldID(JNIEnv* env, jclass cls, const char* name, const char* sig);
    static jfieldID GetStaticFieldID(JNIEnv* env, jclass cls, const char* name, const char* sig);
    static jmethodID GetMethodID(JNIEnv* env, jclass cls, const char* name, const char* sig);
    static jmethodID GetStaticMethodID(JNIEnv* env, jclass cls, const char* name, const char* sig);
    // True when (name, desc) read from class bytes is the renamed form of an MCP
    // member of mcpOwner (any overload). Always false outside renaming clients.
    static bool IsRuntimeMember(const char* mcpOwner, const char* mcpName, const std::string& name,
                                const std::string& desc);
    // True when the internal class name seen at runtime is the MCP class, under its
    // MCP name or under the name a renaming client gives it.
    static bool IsRuntimeClass(const char* internalName, const char* mcpSlashName);

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
    // Set when the client renames every Minecraft class and member (CheatBreaker).
    static inline const mappings::RuntimeMappings* Mappings = nullptr;
    static inline MinecraftVersion Version = MinecraftVersion::UNDEFINED;
};
