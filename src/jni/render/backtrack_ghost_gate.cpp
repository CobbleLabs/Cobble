#include "backtrack_ghost_gate.hpp"

#include <atomic>

#include "../../module/combat/backtrack.h"
#include "../../sdk/core/minecraft.h"
#include "../../util/logger.hpp"
#include "../java.h"
#include "../../util/inject_trace.hpp"

// ---------------------------------------------------------------------------
// BacktrackGhostHook Java class bytecode
//
// Equivalent source (loaded dynamically into the JVM):
//
//   package com.cobble.agent;
//   public class BacktrackGhostHook {
//       public static native void onRenderWorld(float partialTicks);
//   }
//
// Minimal hand-crafted .class so there is no plain-text class name in .rdata
// and no external build step (same approach as SlowDownHook).
// ---------------------------------------------------------------------------
namespace {

static const uint8_t kBacktrackGhostHookBytecode[] = {
    // Magic + version (Java 8 = 52.0)
    0xCA,
    0xFE,
    0xBA,
    0xBE,
    0x00,
    0x00,
    0x00,
    0x34,
    // Constant pool count = 12 (11 entries + 1)
    0x00,
    0x0C,
    // #1 Utf8 "com/cobble/agent/BacktrackGhostHook" (35 chars)
    0x01,
    0x00,
    0x23,
    'c',
    'o',
    'm',
    '/',
    'c',
    'o',
    'b',
    'b',
    'l',
    'e',
    '/',
    'a',
    'g',
    'e',
    'n',
    't',
    '/',
    'B',
    'a',
    'c',
    'k',
    't',
    'r',
    'a',
    'c',
    'k',
    'G',
    'h',
    'o',
    's',
    't',
    'H',
    'o',
    'o',
    'k',
    // #2 Class -> #1
    0x07,
    0x00,
    0x01,
    // #3 Utf8 "java/lang/Object" (16 chars)
    0x01,
    0x00,
    0x10,
    'j',
    'a',
    'v',
    'a',
    '/',
    'l',
    'a',
    'n',
    'g',
    '/',
    'O',
    'b',
    'j',
    'e',
    'c',
    't',
    // #4 Class -> #3
    0x07,
    0x00,
    0x03,
    // #5 Utf8 "<init>" (6 chars)
    0x01,
    0x00,
    0x06,
    '<',
    'i',
    'n',
    'i',
    't',
    '>',
    // #6 Utf8 "()V" (3 chars)
    0x01,
    0x00,
    0x03,
    '(',
    ')',
    'V',
    // #7 Utf8 "Code" (4 chars)
    0x01,
    0x00,
    0x04,
    'C',
    'o',
    'd',
    'e',
    // #8 Utf8 "onRenderWorld" (13 chars)
    0x01,
    0x00,
    0x0D,
    'o',
    'n',
    'R',
    'e',
    'n',
    'd',
    'e',
    'r',
    'W',
    'o',
    'r',
    'l',
    'd',
    // #9 Utf8 "(Ljava/lang/Object;)V" (21 chars)
    0x01,
    0x00,
    0x15,
    '(',
    'L',
    'j',
    'a',
    'v',
    'a',
    '/',
    'l',
    'a',
    'n',
    'g',
    '/',
    'O',
    'b',
    'j',
    'e',
    'c',
    't',
    ';',
    ')',
    'V',
    // #10 NameAndType -> #5, #6
    0x0C,
    0x00,
    0x05,
    0x00,
    0x06,
    // #11 Methodref -> #4, #10
    0x0A,
    0x00,
    0x04,
    0x00,
    0x0A,

    // access_flags = ACC_PUBLIC (0x0001)
    0x00,
    0x01,
    // this_class = #2
    0x00,
    0x02,
    // super_class = #4
    0x00,
    0x04,
    // interfaces_count = 0
    0x00,
    0x00,
    // fields_count = 0
    0x00,
    0x00,
    // methods_count = 2
    0x00,
    0x02,

    // --- Method 0: <init> ()V ---
    0x00,
    0x01,  // ACC_PUBLIC
    0x00,
    0x05,  // name = #5 "<init>"
    0x00,
    0x06,  // descriptor = #6 "()V"
    0x00,
    0x01,  // attributes_count = 1
    0x00,
    0x07,  //   Code (#7)
    0x00,
    0x00,
    0x00,
    0x11,  // attribute_length = 17
    0x00,
    0x01,
    0x00,
    0x01,  // max_stack = 1, max_locals = 1
    0x00,
    0x00,
    0x00,
    0x05,  // code_length = 5
    0x2A,  // aload_0
    0xB7,
    0x00,
    0x0B,  // invokespecial #11 (Object.<init>)
    0xB1,  // return
    0x00,
    0x00,  // exception_table_length = 0
    0x00,
    0x00,  // attributes_count = 0

    // --- Method 1: onRenderWorld (F)V ---
    // ACC_PUBLIC | ACC_STATIC | ACC_NATIVE = 0x0109
    0x01,
    0x09,
    0x00,
    0x08,  // name = #8 "onRenderWorld"
    0x00,
    0x09,  // descriptor = #9 "(F)V"
    0x00,
    0x00,  // attributes_count = 0 (native has no Code)

    // class_attributes_count = 0
    0x00,
    0x00,
};

static std::atomic<bool> g_initialized{false};
static jclass g_cls_BacktrackGhostHook = nullptr;

}  // namespace

#include "../../module/visual/pings.h"

namespace BacktrackGhostGate {

void JNICALL NativeOnRenderWorld(JNIEnv* env, jclass, jobject viewEntity) {
    (void)viewEntity;
    if (!env)
        return;

    // Render focus player pings with actual glow chams aura. (The old backtrack
    // 3D-ghost draw was removed: Backtrack no longer renders a separate silhouette
    // to aim at — you hit the live player and the hold is done via outgoing timing.)
    if (pingsModule && pingsModule->enabled) {
        pingsModule->onRender3D();
    }
}

bool Init(JNIEnv* env) {
    if (g_initialized.load())
        return true;
    if (!env)
        return false;

    jclass hook = nullptr;
    if (!Java::AssignClass("com.cobble.agent.BacktrackGhostHook", hook)) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    if (!hook) {
        if (Java::ClassLoader) {
            hook = env->DefineClass("com/cobble/agent/BacktrackGhostHook", Java::ClassLoader,
                                    reinterpret_cast<const jbyte*>(kBacktrackGhostHookBytecode),
                                    static_cast<jint>(sizeof(kBacktrackGhostHookBytecode)));
            if (env->ExceptionCheck()) {
                COBBLE_TRACE("BacktrackGhostGate::Init: DefineClass threw");
                env->ExceptionClear();
            }
        } else {
            COBBLE_TRACE("BacktrackGhostGate::Init: Java::ClassLoader is NULL");
        }
    }

    if (!hook) {
        log_warn("[BacktrackGhostGate] Failed to define BacktrackGhostHook class");
        return false;
    }

    JNINativeMethod methods[1];
    const char* mName = "onRenderWorld";
    const char* mDesc = "(Ljava/lang/Object;)V";
    methods[0].name = const_cast<char*>(mName);
    methods[0].signature = const_cast<char*>(mDesc);
    methods[0].fnPtr = reinterpret_cast<void*>(&NativeOnRenderWorld);

    if (env->RegisterNatives(hook, methods, 1) != 0) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        log_err("[BacktrackGhostGate] RegisterNatives failed");
        env->DeleteLocalRef(hook);
        return false;
    }

    g_cls_BacktrackGhostHook = (jclass)env->NewGlobalRef(hook);
    env->DeleteLocalRef(hook);
    g_initialized = true;
    log_ok("[BacktrackGhostGate] BacktrackGhostHook native bridge ready");
    return true;
}

void Shutdown(JNIEnv* env, bool safeToUnregister) {
    if (g_cls_BacktrackGhostHook) {
        if (safeToUnregister && env)
            env->UnregisterNatives(g_cls_BacktrackGhostHook);
        if (env)
            env->DeleteGlobalRef(g_cls_BacktrackGhostHook);
        g_cls_BacktrackGhostHook = nullptr;
    }
    g_initialized = false;
}

bool IsReady() {
    return g_initialized.load();
}

}  // namespace BacktrackGhostGate
