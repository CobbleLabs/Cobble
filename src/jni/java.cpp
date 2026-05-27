#include "java.h"
#include "../sdk/jni/jstring_util.h"
#include "../util/logger.hpp"
#include "../util/aes_string.hpp"

void setupClassLoader() {
    JNIEnv* env = Java::Env();
    if (!env) return;

    jclass c_Thread = env->FindClass(OBFUSCATE_STRING("java/lang/Thread").c_str());
    jclass c_Map = env->FindClass(OBFUSCATE_STRING("java/util/Map").c_str());
    jclass c_Set = env->FindClass(OBFUSCATE_STRING("java/util/Set").c_str());
    jclass c_ClassLoader = env->FindClass(OBFUSCATE_STRING("java/lang/ClassLoader").c_str());

    jmethodID mid_getAllStackTraces = env->GetStaticMethodID(c_Thread, OBFUSCATE_STRING("getAllStackTraces").c_str(), OBFUSCATE_STRING("()Ljava/util/Map;").c_str());
    jmethodID mid_keySet = env->GetMethodID(c_Map, OBFUSCATE_STRING("keySet").c_str(), OBFUSCATE_STRING("()Ljava/util/Set;").c_str());
    jmethodID mid_toArray = env->GetMethodID(c_Set, OBFUSCATE_STRING("toArray").c_str(), OBFUSCATE_STRING("()[Ljava/lang/Object;").c_str());
    jmethodID mid_getContextClassLoader =
        env->GetMethodID(c_Thread, OBFUSCATE_STRING("getContextClassLoader").c_str(), OBFUSCATE_STRING("()Ljava/lang/ClassLoader;").c_str());
    Java::MidFindClass = env->GetMethodID(c_ClassLoader, OBFUSCATE_STRING("findClass").c_str(), OBFUSCATE_STRING("(Ljava/lang/String;)Ljava/lang/Class;").c_str());
    jmethodID mid_currentThread = env->GetStaticMethodID(c_Thread, OBFUSCATE_STRING("currentThread").c_str(), OBFUSCATE_STRING("()Ljava/lang/Thread;").c_str());

    jobject obj_stackTracesMap = env->CallStaticObjectMethod(c_Thread, mid_getAllStackTraces);
    jobject obj_threadsSet = env->CallObjectMethod(obj_stackTracesMap, mid_keySet);

    jobjectArray threads = (jobjectArray)env->CallObjectMethod(obj_threadsSet, mid_toArray);
    jint szThreads = env->GetArrayLength(threads);

    for (int i = 0; i < szThreads; i++) {
        jobject thread = env->GetObjectArrayElement(threads, i);
        jobject classLoaderObj = env->CallObjectMethod(thread, mid_getContextClassLoader);

        if (classLoaderObj) {
            jstring className = env->NewStringUTF(OBFUSCATE_STRING("net.minecraft.client.Minecraft").c_str());
            jobject minecraftClass = env->CallObjectMethod(classLoaderObj, Java::MidFindClass, className);

            if (minecraftClass) {
                Java::ClassLoader = env->NewGlobalRef(classLoaderObj);

                env->DeleteLocalRef(minecraftClass);
                env->DeleteLocalRef(classLoaderObj);
                env->DeleteLocalRef(thread);

                break;
            }
            env->DeleteLocalRef(classLoaderObj);
        }

        env->DeleteLocalRef(thread);
    }

    env->DeleteLocalRef(threads);
    env->DeleteLocalRef(obj_stackTracesMap);
    env->DeleteLocalRef(obj_threadsSet);
    env->DeleteLocalRef(c_Thread);
    env->DeleteLocalRef(c_Map);
    env->DeleteLocalRef(c_Set);
    env->DeleteLocalRef(c_ClassLoader);

    Java::Initialized = true;
}

void Java::Init() {
    Java::Initialized = false;
    log_debug("starting java init...");

    // Check if there is any Java VMs in the injected thread
    jsize count = 0;
    if (JNI_GetCreatedJavaVMs(&vm, 1, &count) != JNI_OK || count == 0) {
        log_err("no java vm found");
        return;
    }
    log_ok("found java vm");

    // Get env for current thread (will attach if needed)
    JNIEnv* env = Env();
    if (!env) {
        log_err("failed to get jni env");
        return;
    }
    log_ok("attached to java");

    jint tiRes = vm->GetEnv((void**)&Java::tiEnv, JVMTI_VERSION_1_2);
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
    setupClassLoader();

    if (!Java::Initialized) {
        log_err("classloader setup failed");
        return;
    }
    log_ok("classloader ready");

    GetMinecraftVersion();
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
    if (!getMinecraft)
        return;

    jobject minecraft = env->CallStaticObjectMethod(minecraftClass, getMinecraft);
    if (!minecraft)
        return;

    jfieldID launchedVersionField = env->GetFieldID(minecraftClass, OBFUSCATE_STRING("launchedVersion").c_str(),
                                                    OBFUSCATE_STRING("Ljava/lang/String;").c_str());
    if (!launchedVersionField)
        return;

    jstring versionStr = (jstring)env->GetObjectField(minecraft, launchedVersionField);
    if (!versionStr)
        return;

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
    // Always get fresh env from VM - safer than caching
    // The overhead is minimal (just a pointer lookup in VM)
    if (!vm) return nullptr;

    JNIEnv* env = nullptr;
    jint status = vm->GetEnv((void**)&env, JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        // Thread not attached, attach it
        if (vm->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    } else if (status != JNI_OK) {
        return nullptr;
    }

    return env;
}

