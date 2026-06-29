#include "java.h"
#include "../sdk/jni/jstring_util.h"
#include "../util/logger.hpp"
#include "../util/aes_string.hpp"
#include "../util/inject_trace.hpp"

void setupClassLoader() {
    COBBLE_TRACE("setupClassLoader: enter");
    JNIEnv* env = Java::Env();
    if (!env) {
        COBBLE_TRACE("setupClassLoader: Java::Env() returned nullptr");
        return;
    }

    COBBLE_TRACE("setupClassLoader: env ok, finding Thread");
    jclass c_Thread = env->FindClass(OBFUSCATE_STRING("java/lang/Thread").c_str());
    if (!c_Thread) {
        COBBLE_TRACE("setupClassLoader: c_Thread is null");
        return;
    }

    COBBLE_TRACE("setupClassLoader: finding Map");
    jclass c_Map = env->FindClass(OBFUSCATE_STRING("java/util/Map").c_str());
    if (!c_Map) {
        COBBLE_TRACE("setupClassLoader: c_Map is null");
        env->DeleteLocalRef(c_Thread);
        return;
    }

    COBBLE_TRACE("setupClassLoader: finding Set");
    jclass c_Set = env->FindClass(OBFUSCATE_STRING("java/util/Set").c_str());
    if (!c_Set) {
        COBBLE_TRACE("setupClassLoader: c_Set is null");
        env->DeleteLocalRef(c_Thread);
        env->DeleteLocalRef(c_Map);
        return;
    }

    COBBLE_TRACE("setupClassLoader: finding ClassLoader");
    jclass c_ClassLoader = env->FindClass(OBFUSCATE_STRING("java/lang/ClassLoader").c_str());
    if (!c_ClassLoader) {
        COBBLE_TRACE("setupClassLoader: c_ClassLoader is null");
        env->DeleteLocalRef(c_Thread);
        env->DeleteLocalRef(c_Map);
        env->DeleteLocalRef(c_Set);
        return;
    }

    COBBLE_TRACE("setupClassLoader: getting method IDs");
    jmethodID mid_getAllStackTraces = env->GetStaticMethodID(c_Thread, OBFUSCATE_STRING("getAllStackTraces").c_str(), OBFUSCATE_STRING("()Ljava/util/Map;").c_str());
    jmethodID mid_keySet = env->GetMethodID(c_Map, OBFUSCATE_STRING("keySet").c_str(), OBFUSCATE_STRING("()Ljava/util/Set;").c_str());
    jmethodID mid_toArray = env->GetMethodID(c_Set, OBFUSCATE_STRING("toArray").c_str(), OBFUSCATE_STRING("()[Ljava/lang/Object;").c_str());
    jmethodID mid_getContextClassLoader = env->GetMethodID(c_Thread, OBFUSCATE_STRING("getContextClassLoader").c_str(), OBFUSCATE_STRING("()Ljava/lang/ClassLoader;").c_str());
    Java::MidFindClass = env->GetMethodID(c_ClassLoader, OBFUSCATE_STRING("findClass").c_str(), OBFUSCATE_STRING("(Ljava/lang/String;)Ljava/lang/Class;").c_str());
    jmethodID mid_currentThread = env->GetStaticMethodID(c_Thread, OBFUSCATE_STRING("currentThread").c_str(), OBFUSCATE_STRING("()Ljava/lang/Thread;").c_str());

    if (!mid_getAllStackTraces || !mid_keySet || !mid_toArray || !mid_getContextClassLoader || !Java::MidFindClass || !mid_currentThread) {
        COBBLE_TRACE("setupClassLoader: failed to get one or more critical method IDs");
        env->DeleteLocalRef(c_Thread);
        env->DeleteLocalRef(c_Map);
        env->DeleteLocalRef(c_Set);
        env->DeleteLocalRef(c_ClassLoader);
        return;
    }

    COBBLE_TRACE("setupClassLoader: calling getAllStackTraces");
    jobject obj_stackTracesMap = env->CallStaticObjectMethod(c_Thread, mid_getAllStackTraces);
    if (!obj_stackTracesMap) {
        COBBLE_TRACE("setupClassLoader: obj_stackTracesMap is null");
        env->DeleteLocalRef(c_Thread);
        env->DeleteLocalRef(c_Map);
        env->DeleteLocalRef(c_Set);
        env->DeleteLocalRef(c_ClassLoader);
        return;
    }

    COBBLE_TRACE("setupClassLoader: calling keySet");
    jobject obj_threadsSet = env->CallObjectMethod(obj_stackTracesMap, mid_keySet);
    if (!obj_threadsSet) {
        COBBLE_TRACE("setupClassLoader: obj_threadsSet is null");
        env->DeleteLocalRef(obj_stackTracesMap);
        env->DeleteLocalRef(c_Thread);
        env->DeleteLocalRef(c_Map);
        env->DeleteLocalRef(c_Set);
        env->DeleteLocalRef(c_ClassLoader);
        return;
    }

    COBBLE_TRACE("setupClassLoader: calling toArray");
    jobjectArray threads = (jobjectArray)env->CallObjectMethod(obj_threadsSet, mid_toArray);
    if (!threads) {
        COBBLE_TRACE("setupClassLoader: threads array is null");
        env->DeleteLocalRef(obj_threadsSet);
        env->DeleteLocalRef(obj_stackTracesMap);
        env->DeleteLocalRef(c_Thread);
        env->DeleteLocalRef(c_Map);
        env->DeleteLocalRef(c_Set);
        env->DeleteLocalRef(c_ClassLoader);
        return;
    }

    jint szThreads = env->GetArrayLength(threads);
    char traceBuf[128];
    wsprintfA(traceBuf, "setupClassLoader: starting loop over %d threads", szThreads);
    COBBLE_TRACE(traceBuf);

    for (int i = 0; i < szThreads; i++) {
        jobject thread = env->GetObjectArrayElement(threads, i);
        if (!thread) continue;

        jobject classLoaderObj = env->CallObjectMethod(thread, mid_getContextClassLoader);
        if (classLoaderObj) {
            jstring className = env->NewStringUTF(OBFUSCATE_STRING("net.minecraft.client.Minecraft").c_str());
            jobject minecraftClass = env->CallObjectMethod(classLoaderObj, Java::MidFindClass, className);

            // CRITICAL: Clear ClassNotFoundException so JNI state remains unpoisoned for next calls
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }

            if (minecraftClass) {
                COBBLE_TRACE("setupClassLoader: net.minecraft.client.Minecraft found!");
                Java::ClassLoader = env->NewGlobalRef(classLoaderObj);

                env->DeleteLocalRef(minecraftClass);
                env->DeleteLocalRef(className);
                env->DeleteLocalRef(classLoaderObj);
                env->DeleteLocalRef(thread);
                break;
            }

            env->DeleteLocalRef(className);
            env->DeleteLocalRef(classLoaderObj);
        }

        env->DeleteLocalRef(thread);
    }

    COBBLE_TRACE("setupClassLoader: performing final cleanup");
    env->DeleteLocalRef(threads);
    env->DeleteLocalRef(obj_threadsSet);
    env->DeleteLocalRef(obj_stackTracesMap);
    env->DeleteLocalRef(c_Thread);
    env->DeleteLocalRef(c_Map);
    env->DeleteLocalRef(c_Set);
    env->DeleteLocalRef(c_ClassLoader);

    Java::Initialized = (Java::ClassLoader != nullptr);
    COBBLE_TRACE(Java::Initialized ? "setupClassLoader: success" : "setupClassLoader: failed (no ClassLoader loaded)");
}

void Java::Init() {
    Java::Initialized = false;
    COBBLE_TRACE("Java::Init: enter");
    log_debug("starting java init...");

    // Check if there is any Java VMs in the injected thread
    jsize count = 0;
    COBBLE_TRACE("Java::Init: calling JNI_GetCreatedJavaVMs");
    jint getVMRes = JNI_GetCreatedJavaVMs(&vm, 1, &count);
    
    char traceBuf[128];
    wsprintfA(traceBuf, "Java::Init: JNI_GetCreatedJavaVMs returned %d, count = %d", getVMRes, count);
    COBBLE_TRACE(traceBuf);

    if (getVMRes != JNI_OK || count == 0) {
        log_err("no java vm found");
        return;
    }
    log_ok("found java vm");

    // Get env for current thread (will attach if needed)
    COBBLE_TRACE("Java::Init: getting env");
    JNIEnv* env = Env();
    if (!env) {
        COBBLE_TRACE("Java::Init: Env() returned nullptr");
        log_err("failed to get jni env");
        return;
    }
    log_ok("attached to java");

    COBBLE_TRACE("Java::Init: getting JVMTI env");
    jint tiRes = vm->GetEnv((void**)&Java::tiEnv, JVMTI_VERSION_1_2);
    wsprintfA(traceBuf, "Java::Init: GetEnv for JVMTI returned %d, tiEnv = %p", tiRes, Java::tiEnv);
    COBBLE_TRACE(traceBuf);

    if (tiRes != JNI_OK || Java::tiEnv == nullptr) {
        log_warn("jvmti not available (not critical)");
    } else {
        // Request JVMTI capabilities - try each individually since all-at-once may fail
        auto tryAddCap = [](jvmtiEnv* ti, const char* name, auto setter) {
            jvmtiCapabilities caps;
            memset(&caps, 0, sizeof(caps));
            setter(caps);
            jvmtiError err = ti->AddCapabilities(&caps);
            if (err == JVMTI_ERROR_NONE) {
                log_debug("JVMTI cap '%s': granted", name);
                return true;
            } else {
                log_debug("JVMTI cap '%s': denied (err=%d)", name, err);
                return false;
            }
        };

        // Try to add each capability
        tryAddCap(Java::tiEnv, "retransform_classes", [](jvmtiCapabilities& c) { c.can_retransform_classes = 1; });
        tryAddCap(Java::tiEnv, "redefine_classes", [](jvmtiCapabilities& c) { c.can_redefine_classes = 1; });
        tryAddCap(Java::tiEnv, "class_hook_events",
                  [](jvmtiCapabilities& c) { c.can_generate_all_class_hook_events = 1; });
        tryAddCap(Java::tiEnv, "method_entry_events",
                  [](jvmtiCapabilities& c) { c.can_generate_method_entry_events = 1; });
        tryAddCap(Java::tiEnv, "method_exit_events",
                  [](jvmtiCapabilities& c) { c.can_generate_method_exit_events = 1; });
        tryAddCap(Java::tiEnv, "force_early_return", [](jvmtiCapabilities& c) { c.can_force_early_return = 1; });
        tryAddCap(Java::tiEnv, "access_local_vars", [](jvmtiCapabilities& c) { c.can_access_local_variables = 1; });

        // Check what we actually got
        jvmtiCapabilities gotCaps;
        memset(&gotCaps, 0, sizeof(gotCaps));
        Java::tiEnv->GetCapabilities(&gotCaps);
        log_ok("JVMTI final caps: retransform=%d redefine=%d hooks=%d entry=%d force=%d locals=%d",
               gotCaps.can_retransform_classes, gotCaps.can_redefine_classes,
               gotCaps.can_generate_all_class_hook_events, gotCaps.can_generate_method_entry_events,
               gotCaps.can_force_early_return, gotCaps.can_access_local_variables);
    }

    log_debug("setting up classloader...");
    COBBLE_TRACE("Java::Init: calling setupClassLoader");
    setupClassLoader();

    if (!Java::Initialized) {
        COBBLE_TRACE("Java::Init: setupClassLoader failed");
        log_err("classloader setup failed");
        return;
    }
    COBBLE_TRACE("Java::Init: classloader ok, getting minecraft version");
    log_ok("classloader ready");

    GetMinecraftVersion();
    COBBLE_TRACE("Java::Init: exit");
}

void Java::Kill() {
    if (vm) {
        vm->DetachCurrentThread();
    }
}

bool Java::AssignClass(std::string name, jclass& out) {
    JNIEnv* env = Env();
    if (!env || !Java::ClassLoader || !Java::MidFindClass) return false;

    jstring className = env->NewStringUTF(name.c_str());
    jobject findClass = env->CallObjectMethod(Java::ClassLoader, Java::MidFindClass, className);
    env->DeleteLocalRef(className);

    // CRITICAL: Clear any pending exceptions (e.g. ClassNotFoundException)
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (findClass) {
        out = (jclass)findClass;
        return true;
    }

    return false;
}

void Java::GetMinecraftVersion() {
    Java::Version = MinecraftVersion::UNDEFINED;

    JNIEnv* env = Env();
    if (!env) return;

    jclass minecraftClass = nullptr;
    if (!AssignClass(OBFUSCATE_STRING("net.minecraft.client.Minecraft"), minecraftClass))
        return;

    jmethodID getMinecraft =
        env->GetStaticMethodID(minecraftClass, OBFUSCATE_STRING("getMinecraft").c_str(),
                               OBFUSCATE_STRING("()Lnet/minecraft/client/Minecraft;").c_str());
    if (!getMinecraft) {
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    jobject minecraft = env->CallStaticObjectMethod(minecraftClass, getMinecraft);
    if (!minecraft) {
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    jfieldID launchedVersionField = env->GetFieldID(minecraftClass, OBFUSCATE_STRING("launchedVersion").c_str(),
                                                    OBFUSCATE_STRING("Ljava/lang/String;").c_str());
    if (!launchedVersionField) {
        env->DeleteLocalRef(minecraft);
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    jstring versionStr = (jstring)env->GetObjectField(minecraft, launchedVersionField);
    if (!versionStr) {
        env->DeleteLocalRef(minecraft);
        env->DeleteLocalRef(minecraftClass);
        return;
    }

    std::string version = jstringToString(versionStr);

    if (version == "1.8.9") {
        Java::Version = MinecraftVersion::MC_1_8_9;
        log_ok("detected minecraft 1.8.9");
    } else if (version == "1.7.10") {
        Java::Version = MinecraftVersion::MC_1_7_10;
        log_ok("detected minecraft 1.7.10");
    } else {
        log_warn("unknown minecraft version: %s", version.c_str());
    }

    env->DeleteLocalRef(versionStr);
    env->DeleteLocalRef(minecraft);
    env->DeleteLocalRef(minecraftClass);
}

JNIEnv* Java::Env() {
    if (!vm) return nullptr;

    JNIEnv* env = nullptr;
    jint status = vm->GetEnv((void**)&env, JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        if (vm->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    } else if (status != JNI_OK) {
        return nullptr;
    }

    return env;
}
