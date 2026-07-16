#include "backtrack_gate.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../../module/combat/backtrack.h"
#include "../../sdk/core/minecraft.h"
#include "../../util/logger.hpp"
#include "../java.h"

// ---------------------------------------------------------------------------
// IncomingHook Java class bytecode
//
//   package com.cobble.agent;
//   public class IncomingHook {
//       public static native boolean canProcess(Object packet);
//   }
//
// Same hand-crafted layout as SlowDownHook (same string lengths), only the class
// name, method name and the return type of the descriptor differ.
// ---------------------------------------------------------------------------
namespace {

static const uint8_t kIncomingHookBytecode[] = {
    0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x34,
    0x00, 0x0C,  // constant pool count = 12
    // #1 Utf8 "com/cobble/agent/IncomingHook" (29 chars)
    0x01, 0x00, 0x1D,
    'c','o','m','/','c','o','b','b','l','e','/','a','g','e','n','t','/',
    'I','n','c','o','m','i','n','g','H','o','o','k',
    // #2 Class -> #1
    0x07, 0x00, 0x01,
    // #3 Utf8 "java/lang/Object" (16)
    0x01, 0x00, 0x10,
    'j','a','v','a','/','l','a','n','g','/','O','b','j','e','c','t',
    // #4 Class -> #3
    0x07, 0x00, 0x03,
    // #5 Utf8 "<init>" (6)
    0x01, 0x00, 0x06,
    '<','i','n','i','t','>',
    // #6 Utf8 "()V" (3)
    0x01, 0x00, 0x03,
    '(',')','V',
    // #7 Utf8 "Code" (4)
    0x01, 0x00, 0x04,
    'C','o','d','e',
    // #8 Utf8 "canProcess" (10)
    0x01, 0x00, 0x0A,
    'c','a','n','P','r','o','c','e','s','s',
    // #9 Utf8 "(Ljava/lang/Object;)Z" (21)
    0x01, 0x00, 0x15,
    '(','L','j','a','v','a','/','l','a','n','g','/','O','b','j','e','c','t',';',')','Z',
    // #10 NameAndType -> #5, #6
    0x0C, 0x00, 0x05, 0x00, 0x06,
    // #11 Methodref -> #4, #10
    0x0A, 0x00, 0x04, 0x00, 0x0A,

    0x00, 0x01,  // access = ACC_PUBLIC
    0x00, 0x02,  // this_class = #2
    0x00, 0x04,  // super_class = #4
    0x00, 0x00,  // interfaces_count
    0x00, 0x00,  // fields_count
    0x00, 0x02,  // methods_count = 2

    // Method 0: <init> ()V
    0x00, 0x01, 0x00, 0x05, 0x00, 0x06, 0x00, 0x01,
    0x00, 0x07, 0x00, 0x00, 0x00, 0x11,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
    0x2A, 0xB7, 0x00, 0x0B, 0xB1,
    0x00, 0x00, 0x00, 0x00,

    // Method 1: canProcess (Ljava/lang/Object;)Z  ACC_PUBLIC|STATIC|NATIVE = 0x0109
    0x01, 0x09, 0x00, 0x08, 0x00, 0x09, 0x00, 0x00,

    0x00, 0x00,  // class attributes_count
};

std::atomic<bool> g_initialized{false};
jclass g_cls_IncomingHook = nullptr;

// Held packet queue.
struct Held {
    jobject packet;  // GlobalRef
    std::uint64_t heldAtMs;
};
std::deque<Held> g_held;
std::mutex g_mutex;

// Set only while re-injecting our own held packets, so the re-entrant handleEntity
// call passes straight through instead of being re-held.
thread_local bool tl_releasing = false;

// Lazily-resolved packet/entity-id/processPacket handles.
jclass g_s14 = nullptr, g_s18 = nullptr;
jfieldID g_s14Id = nullptr, g_s18Id = nullptr;
bool g_idsResolved = false;

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

void resolveIds(JNIEnv* env) {
    if (g_idsResolved)
        return;
    g_idsResolved = true;
    jclass local = nullptr;
    if (Java::AssignClass("net.minecraft.network.play.server.S14PacketEntity", local)) {
        g_s14 = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        g_s14Id = env->GetFieldID(g_s14, "entityId", "I");
        if (!g_s14Id) {
            env->ExceptionClear();
            g_s14Id = env->GetFieldID(g_s14, "field_149074_b", "I");
        }
    }
    local = nullptr;
    if (Java::AssignClass("net.minecraft.network.play.server.S18PacketEntityTeleport", local)) {
        g_s18 = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        g_s18Id = env->GetFieldID(g_s18, "entityId", "I");
        if (!g_s18Id) {
            env->ExceptionClear();
            g_s18Id = env->GetFieldID(g_s18, "field_149458_a", "I");
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

int entityIdOf(JNIEnv* env, jobject packet) {
    resolveIds(env);
    if (g_s14 && g_s14Id && env->IsInstanceOf(packet, g_s14) == JNI_TRUE)
        return env->GetIntField(packet, g_s14Id);
    if (g_s18 && g_s18Id && env->IsInstanceOf(packet, g_s18) == JNI_TRUE)
        return env->GetIntField(packet, g_s18Id);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return -1;
}

// Re-inject one held packet: packet.processPacket(netHandler). Runs on the client
// thread; tl_releasing keeps the re-entrant handler call from re-holding it.
void reinject(JNIEnv* env, jobject packet) {
    jobject netHandler = mc.getNetHandler();
    if (!netHandler) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }
    jclass pcls = env->GetObjectClass(packet);
    jmethodID mid = pcls ? env->GetMethodID(pcls, "processPacket", "(Lnet/minecraft/network/INetHandler;)V") : nullptr;
    if (!mid && pcls) {
        env->ExceptionClear();
        mid = env->GetMethodID(pcls, "func_148833_a", "(Lnet/minecraft/network/INetHandler;)V");
    }
    if (mid) {
        tl_releasing = true;
        env->CallVoidMethod(packet, mid, netHandler);
        tl_releasing = false;
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (pcls)
        env->DeleteLocalRef(pcls);
    env->DeleteLocalRef(netHandler);
}

}  // namespace

namespace BacktrackGate {

jboolean JNICALL NativeCanProcess(JNIEnv* env, jclass, jobject packet) {
    if (tl_releasing || !env || !packet)
        return JNI_TRUE;
    if (!::backtrack || !::backtrack->enabled)
        return JNI_TRUE;

    const int id = entityIdOf(env, packet);
    const bool hold = ::backtrack->wantsHold(id);
    // Diagnostic: confirms the hook fires (every ~120th call), and logs the first holds.
    static int s_dbg = 0;
    if ((++s_dbg % 120) == 1)
        log_debug("[BacktrackGate] canProcess fires: id=%d hold=%d held=%d", id, hold ? 1 : 0, HeldCount());
    if (id < 0 || !hold)
        return JNI_TRUE;

    static int s_holdDbg = 0;
    if (s_holdDbg < 8) {
        ++s_holdDbg;
        log_debug("[BacktrackGate] HOLD id=%d", id);
    }

    jobject global = env->NewGlobalRef(packet);
    if (!global) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return JNI_TRUE;  // can't hold → process normally
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // Safety cap so a stuck hold never grows unbounded.
        if (g_held.size() >= 200) {
            jobject old = g_held.front().packet;
            g_held.pop_front();
            if (old)
                env->DeleteGlobalRef(old);
        }
        g_held.push_back({global, nowMs()});
    }
    return JNI_FALSE;  // HOLD
}

int Release(JNIEnv* env, bool all) {
    if (!env)
        return 0;
    const std::uint64_t now = nowMs();
    const std::uint64_t window = ::backtrack ? static_cast<std::uint64_t>(::backtrack->getHoldMs()) : 0;

    std::deque<jobject> toSend;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        while (!g_held.empty()) {
            const Held& h = g_held.front();
            if (!all && (now - h.heldAtMs) < window)
                break;
            toSend.push_back(h.packet);
            g_held.pop_front();
        }
    }
    for (jobject p : toSend) {
        if (p) {
            reinject(env, p);
            env->DeleteGlobalRef(p);
        }
    }
    return static_cast<int>(toSend.size());
}

int HeldCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_held.size());
}

bool Init(JNIEnv* env) {
    if (g_initialized.load())
        return true;
    if (!env)
        return false;

    jclass hook = nullptr;
    if (!Java::AssignClass("com.cobble.agent.IncomingHook", hook)) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!hook && Java::ClassLoader) {
        hook = env->DefineClass("com/cobble/agent/IncomingHook", Java::ClassLoader,
                                reinterpret_cast<const jbyte*>(kIncomingHookBytecode),
                                static_cast<jint>(sizeof(kIncomingHookBytecode)));
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!hook) {
        log_warn("[BacktrackGate] Failed to define IncomingHook class");
        return false;
    }

    JNINativeMethod m;
    const char* mName = "canProcess";
    const char* mDesc = "(Ljava/lang/Object;)Z";
    m.name = const_cast<char*>(mName);
    m.signature = const_cast<char*>(mDesc);
    m.fnPtr = reinterpret_cast<void*>(&NativeCanProcess);
    if (env->RegisterNatives(hook, &m, 1) != 0) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        log_err("[BacktrackGate] RegisterNatives failed for IncomingHook");
        env->DeleteLocalRef(hook);
        return false;
    }

    g_cls_IncomingHook = static_cast<jclass>(env->NewGlobalRef(hook));
    env->DeleteLocalRef(hook);
    g_initialized = true;
    log_ok("[BacktrackGate] IncomingHook native bridge ready");
    return true;
}

void Shutdown(JNIEnv* env, bool safeToUnregister) {
    if (g_cls_IncomingHook && env) {
        if (safeToUnregister)
            env->UnregisterNatives(g_cls_IncomingHook);
        env->DeleteGlobalRef(g_cls_IncomingHook);
        g_cls_IncomingHook = nullptr;
    }
    if (env) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (Held& h : g_held)
            if (h.packet)
                env->DeleteGlobalRef(h.packet);
        g_held.clear();
    }
    g_initialized = false;
}

bool IsReady() {
    return g_initialized.load();
}

}  // namespace BacktrackGate
