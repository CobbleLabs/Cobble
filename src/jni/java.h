#pragma once

#include <string>

#include "../../ext/jni/jni.h"
#include "../../ext/jni/jvmti.h"
#include "../sdk/version.h"

struct Java
{
	static void Init();
	static void Kill();

	static bool AssignClass(std::string name, jclass &out);
	static void GetMinecraftVersion();

	static inline JNIEnv* Env;
	static inline jvmtiEnv* tiEnv;
	static inline bool Initialized;
	static inline MinecraftVersion Version;
	
	static JNIEnv* GetEnv();
};

