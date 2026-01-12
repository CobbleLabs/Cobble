#pragma once

#include <string>

#include "../../infra/ext/jni/jni.h"
#include "../../infra/ext/jni/jvmti.h"
#include "../sdk/version.h"

struct Java {
    static void Init();
    static void Kill();

    static bool AssignClass(std::string name, jclass& out);
    static void GetMinecraftVersion();

    static inline JNIEnv* Env;
    static inline jvmtiEnv* tiEnv;
    static inline bool Initialized;
    static inline jobject ClassLoader;
    static inline jmethodID MidFindClass;
    static inline MinecraftVersion Version;

    static JNIEnv* GetEnv();
};
