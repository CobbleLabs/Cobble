#include "slowdown_gate.hpp"

#include <atomic>

#include "../../module/movement/no_slow_down.h"
#include "../../sdk/player/player.h"
#include "../../util/logger.hpp"
#include "../java.h"
#include "../../util/inject_trace.hpp"

// ---------------------------------------------------------------------------
// SlowDownHook Java class bytecode
//
// Equivalent source (loaded dynamically into the JVM):
//
//   package com.cobble.agent;
//   public class SlowDownHook {
//       public static native void onSlowDown(Object player);
//   }
//
// We build the minimal .class bytecode manually so there is no plain-text
// class name in the DLL's .rdata section.
// ---------------------------------------------------------------------------
namespace {

// Minimal class bytecode for com/cobble/agent/SlowDownHook
// Hand-crafted to avoid any external build step.
//
// Constant pool:
//   #1  Utf8   "com/cobble/agent/SlowDownHook"
//   #2  Class  #1
//   #3  Utf8   "java/lang/Object"
//   #4  Class  #3
//   #5  Utf8   "<init>"
//   #6  Utf8   "()V"
//   #7  Utf8   "Code"
//   #8  Utf8   "onSlowDown"
//   #9  Utf8   "(Ljava/lang/Object;)V"
//
// Methods:
//   <init> ()V  (default ctor, calls Object.<init>)
//   onSlowDown  (Ljava/lang/Object;)V  ACC_PUBLIC | ACC_STATIC | ACC_NATIVE
// ---------------------------------------------------------------------------
static const uint8_t kSlowDownHookBytecode[] = {
    // Magic + version (Java 8 = 52.0)
    0xCA, 0xFE, 0xBA, 0xBE,
    0x00, 0x00, 0x00, 0x34,
    // Constant pool count = 12 (11 entries + 1)
    0x00, 0x0C,
    // #1 Utf8 "com/cobble/agent/SlowDownHook" (29 chars)
    0x01, 0x00, 0x1D,
    'c','o','m','/','c','o','b','b','l','e','/','a','g','e','n','t','/','S','l','o','w','D','o','w','n','H','o','o','k',
    // #2 Class -> #1
    0x07, 0x00, 0x01,
    // #3 Utf8 "java/lang/Object" (16 chars)
    0x01, 0x00, 0x10,
    'j','a','v','a','/','l','a','n','g','/','O','b','j','e','c','t',
    // #4 Class -> #3
    0x07, 0x00, 0x03,
    // #5 Utf8 "<init>" (6 chars)
    0x01, 0x00, 0x06,
    '<','i','n','i','t','>',
    // #6 Utf8 "()V" (3 chars)
    0x01, 0x00, 0x03,
    '(',')',  'V',
    // #7 Utf8 "Code" (4 chars)
    0x01, 0x00, 0x04,
    'C','o','d','e',
    // #8 Utf8 "onSlowDown" (10 chars)
    0x01, 0x00, 0x0A,
    'o','n','S','l','o','w','D','o','w','n',
    // #9 Utf8 "(Ljava/lang/Object;)V" (21 chars)
    0x01, 0x00, 0x15,
    '(','L','j','a','v','a','/','l','a','n','g','/','O','b','j','e','c','t',';',')',  'V',
    // #10 NameAndType -> #5, #6
    0x0C, 0x00, 0x05, 0x00, 0x06,
    // #11 Methodref -> #4, #10
    0x0A, 0x00, 0x04, 0x00, 0x0A,

    // access_flags = ACC_PUBLIC (0x0001)
    0x00, 0x01,
    // this_class = #2
    0x00, 0x02,
    // super_class = #4
    0x00, 0x04,
    // interfaces_count = 0
    0x00, 0x00,
    // fields_count = 0
    0x00, 0x00,
    // methods_count = 2
    0x00, 0x02,

    // --- Method 0: <init> ()V ---
    // ACC_PUBLIC (0x0001)
    0x00, 0x01,
    // name_index = #5 "<init>"
    0x00, 0x05,
    // descriptor_index = #6 "()V"
    0x00, 0x06,
    // attributes_count = 1
    0x00, 0x01,
    //   Attribute: Code (#7)
    0x00, 0x07,
    //   attribute_length = 17
    0x00, 0x00, 0x00, 0x11,
    //   max_stack = 1, max_locals = 1
    0x00, 0x01, 0x00, 0x01,
    //   code_length = 5
    0x00, 0x00, 0x00, 0x05,
    //   aload_0, invokespecial #11 (Object.<init>), return
    0x2A,        // aload_0
    0xB7, 0x00, 0x0B, // invokespecial #11
    0xB1,        // return
    //   exception_table_length = 0
    0x00, 0x00,
    //   attributes_count = 0
    0x00, 0x00,

    // --- Method 1: onSlowDown (Ljava/lang/Object;)V ---
    // ACC_PUBLIC | ACC_STATIC | ACC_NATIVE = 0x0001 | 0x0008 | 0x0100 = 0x0109
    0x01, 0x09,
    // name_index = #8 "onSlowDown"
    0x00, 0x08,
    // descriptor_index = #9 "(Ljava/lang/Object;)V"
    0x00, 0x09,
    // attributes_count = 0 (native methods have no Code attribute)
    0x00, 0x00,

    // class_attributes_count = 0
    0x00, 0x00,
};

static std::atomic<bool> g_initialized{false};
static jclass g_cls_SlowDownHook = nullptr;

}  // namespace

namespace SlowDownGate {

void JNICALL NativeOnSlowDown(JNIEnv* env, jclass, jobject playerInstance) {
    COBBLE_TRACE_ONCE("NativeOnSlowDown: callback triggered!");
    if (!noSlowDown || !noSlowDown->enabled || !playerInstance)
        return;

    std::lock_guard<std::recursive_mutex> lock(player.instanceMutex);
    if (!player.instance)
        return;

    if (env->IsSameObject(playerInstance, player.instance) == JNI_TRUE) {
        COBBLE_TRACE_ONCE("NativeOnSlowDown: player matches, applying movement!");
        noSlowDown->applyMovement(env, playerInstance);
    } else {
        COBBLE_TRACE_ONCE("NativeOnSlowDown: player DOES NOT MATCH cached instance!");
    }
}

bool Init(JNIEnv* env) {
    if (g_initialized.load())
        return true;
    if (!env)
        return false;

    // Try to find existing class first
    jclass hook = nullptr;
    if (!Java::AssignClass("com.cobble.agent.SlowDownHook", hook)) {
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    COBBLE_TRACE(hook ? "SlowDownGate::Init: AssignClass found existing class" : "SlowDownGate::Init: AssignClass did not find class");

    if (!hook) {
        if (Java::ClassLoader) {
            COBBLE_TRACE("SlowDownGate::Init: defining class using Java::ClassLoader");
            hook = env->DefineClass("com/cobble/agent/SlowDownHook",
                                    Java::ClassLoader,
                                    reinterpret_cast<const jbyte*>(kSlowDownHookBytecode),
                                    static_cast<jint>(sizeof(kSlowDownHookBytecode)));
            if (env->ExceptionCheck()) {
                COBBLE_TRACE("SlowDownGate::Init: DefineClass threw exception");
                jthrowable ex = env->ExceptionOccurred();
                if (ex) {
                    env->ExceptionClear();
                    jclass exClass = env->GetObjectClass(ex);
                    jclass classClass = env->FindClass("java/lang/Class");
                    jmethodID mid_getName = classClass ? env->GetMethodID(classClass, "getName", "()Ljava/lang/String;") : nullptr;
                    jstring nameStr = (exClass && mid_getName) ? (jstring)env->CallObjectMethod(exClass, mid_getName) : nullptr;
                    
                    jclass throwableClass = env->FindClass("java/lang/Throwable");
                    jmethodID mid_getMessage = throwableClass ? env->GetMethodID(throwableClass, "getMessage", "()Ljava/lang/String;") : nullptr;
                    jstring msgStr = (ex && mid_getMessage) ? (jstring)env->CallObjectMethod(ex, mid_getMessage) : nullptr;

                    const char* nameChars = nameStr ? env->GetStringUTFChars(nameStr, nullptr) : "UnknownException";
                    const char* msgChars = msgStr ? env->GetStringUTFChars(msgStr, nullptr) : "No message";
                    char traceBuf[512];
                    wsprintfA(traceBuf, "SlowDownGate::Init: Exception: %s - Message: %s", nameChars, msgChars);
                    COBBLE_TRACE(traceBuf);

                    if (nameStr) env->ReleaseStringUTFChars(nameStr, nameChars);
                    if (msgStr) env->ReleaseStringUTFChars(msgStr, msgChars);
                }
            }
        } else {
            COBBLE_TRACE("SlowDownGate::Init: Java::ClassLoader is NULL");
        }
    }

    if (!hook) {
        COBBLE_TRACE("SlowDownGate::Init: Hook class is NULL (DefineClass failed)");
        log_warn("[SlowDownGate] Failed to define SlowDownHook class");
        return false;
    }

    JNINativeMethod methods[1];
    const char* mName = "onSlowDown";
    const char* mDesc = "(Ljava/lang/Object;)V";
    methods[0].name      = const_cast<char*>(mName);
    methods[0].signature = const_cast<char*>(mDesc);
    methods[0].fnPtr     = reinterpret_cast<void*>(&NativeOnSlowDown);

    if (env->RegisterNatives(hook, methods, 1) != 0) {
        COBBLE_TRACE("SlowDownGate::Init: RegisterNatives failed");
        if (env->ExceptionCheck()) env->ExceptionClear();
        log_err("[SlowDownGate] RegisterNatives failed");
        env->DeleteLocalRef(hook);
        return false;
    }

    g_cls_SlowDownHook = (jclass)env->NewGlobalRef(hook);
    env->DeleteLocalRef(hook);
    g_initialized = true;
    COBBLE_TRACE("SlowDownGate::Init: Success!");
    log_ok("[SlowDownGate] SlowDownHook native bridge ready");
    return true;
}

void Shutdown(JNIEnv* env) {
    if (g_cls_SlowDownHook) {
        env->UnregisterNatives(g_cls_SlowDownHook);
        env->DeleteGlobalRef(g_cls_SlowDownHook);
        g_cls_SlowDownHook = nullptr;
    }
    g_initialized = false;
}

}  // namespace SlowDownGate
