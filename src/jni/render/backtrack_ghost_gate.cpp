#include "backtrack_ghost_gate.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

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
#include "../../sdk/player/render_data.h"
#include <gl/GL.h>

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
static std::atomic<bool> g_shuttingDown{false};
static std::atomic<std::uint32_t> g_callbacksInFlight{0};
static jclass g_cls_BacktrackGhostHook = nullptr;

struct CallbackScope {
    CallbackScope() { g_callbacksInFlight.fetch_add(1, std::memory_order_acq_rel); }
    ~CallbackScope() { g_callbacksInFlight.fetch_sub(1, std::memory_order_acq_rel); }
};

}  // namespace

#include "../../module/visual/pings.h"
#include "../../module/visual/chams.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/settings.h"
#include "../packet/bytecode_patcher.hpp"

namespace BacktrackGhostGate {

void JNICALL NativeOnRenderWorld(JNIEnv* env, jclass, jobject viewEntity) {
    CallbackScope callback;
    if (g_shuttingDown.load(std::memory_order_acquire) || !env || !viewEntity || !JniCache::cls_Entity)
        return;

    const jboolean isEntity = env->IsInstanceOf(viewEntity, JniCache::cls_Entity);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    if (isEntity != JNI_TRUE) {
        if (BytecodePatcher::IsRenderCameraReady())
            Settings::Freelook::onCameraBegin(env);
        return;
    }

    Settings::Freelook::onCameraReady(env);

    GLfloat proj[16];
    GLfloat mv[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);

    // Save matrices to RenderData
    RenderData::projection.m00 = proj[0];
    RenderData::projection.m01 = proj[1];
    RenderData::projection.m02 = proj[2];
    RenderData::projection.m03 = proj[3];
    RenderData::projection.m10 = proj[4];
    RenderData::projection.m11 = proj[5];
    RenderData::projection.m12 = proj[6];
    RenderData::projection.m13 = proj[7];
    RenderData::projection.m20 = proj[8];
    RenderData::projection.m21 = proj[9];
    RenderData::projection.m22 = proj[10];
    RenderData::projection.m23 = proj[11];
    RenderData::projection.m30 = proj[12];
    RenderData::projection.m31 = proj[13];
    RenderData::projection.m32 = proj[14];
    RenderData::projection.m33 = proj[15];

    RenderData::modelView.m00 = mv[0];
    RenderData::modelView.m01 = mv[1];
    RenderData::modelView.m02 = mv[2];
    RenderData::modelView.m03 = mv[3];
    RenderData::modelView.m10 = mv[4];
    RenderData::modelView.m11 = mv[5];
    RenderData::modelView.m12 = mv[6];
    RenderData::modelView.m13 = mv[7];
    RenderData::modelView.m20 = mv[8];
    RenderData::modelView.m21 = mv[9];
    RenderData::modelView.m22 = mv[10];
    RenderData::modelView.m23 = mv[11];
    RenderData::modelView.m30 = mv[12];
    RenderData::modelView.m31 = mv[13];
    RenderData::modelView.m32 = mv[14];
    RenderData::modelView.m33 = mv[15];

    bool wasUpdated = RenderData::updated;
    RenderData::updated =
        RenderData::IsMatrixValid(RenderData::projection) && RenderData::IsMatrixValid(RenderData::modelView);

    if (RenderData::updated && !wasUpdated) {
        log_ok("RenderData: Successfully intercepted matrices via GL callback! Proj m00=%d, MV m00=%d",
               (int)(proj[0] * 1000.0f), (int)(mv[0] * 1000.0f));
    }

    // Render focus player pings with actual glow chams aura. (The old backtrack
    // 3D-ghost draw was removed: Backtrack no longer renders a separate silhouette
    // to aim at — you hit the live player and the hold is done via outgoing timing.)
    if (pingsModule && pingsModule->enabled) {
        pingsModule->onRender3D();
    }

    // GL chams: re-render each nearby player's model (fill or real skin) through
    // walls, inside Minecraft's own render pass.
    if (chams && chams->enabled) {
        chams->onRender3D();
    }
}

bool Init(JNIEnv* env) {
    if (g_initialized.load())
        return true;
    if (!env)
        return false;

    g_shuttingDown.store(false, std::memory_order_release);

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

bool Shutdown(JNIEnv* env, bool safeToUnregister) {
    g_shuttingDown.store(true, std::memory_order_release);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (g_callbacksInFlight.load(std::memory_order_acquire) != 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            log_err("[BacktrackGhostGate] Timed out waiting for native callbacks");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!env) {
        log_err("[BacktrackGhostGate] Cannot shut down without JNIEnv");
        return false;
    }

    if (g_cls_BacktrackGhostHook) {
        if (!safeToUnregister) {
            log_warn("[BacktrackGhostGate] Native unregister was not declared safe");
            return false;
        }

        const jint unregisterResult = env->UnregisterNatives(g_cls_BacktrackGhostHook);
        const bool unregisterException = env->ExceptionCheck() == JNI_TRUE;
        if (unregisterException)
            env->ExceptionClear();
        if (unregisterResult != JNI_OK || unregisterException) {
            log_err("[BacktrackGhostGate] Failed to unregister BacktrackGhostHook natives (result=%d, exception=%d)",
                    unregisterResult, unregisterException ? 1 : 0);
            return false;
        }
        log_ok("[BacktrackGhostGate] Unregistered BacktrackGhostHook natives");

        env->DeleteGlobalRef(g_cls_BacktrackGhostHook);
        g_cls_BacktrackGhostHook = nullptr;
    }

    g_initialized.store(false, std::memory_order_release);
    g_shuttingDown.store(false, std::memory_order_release);
    return true;
}

bool IsReady() {
    return g_initialized.load();
}

}  // namespace BacktrackGhostGate
