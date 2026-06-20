#include "slowdown_gate.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "../../module/movement/no_slow_down.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"
#include "../java.h"

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

// Cached field IDs for the movement input manipulation
// (these are re-used from NoSlowDown's own initializeFields logic)
static jfieldID g_movementInputField    = nullptr;
static jfieldID g_moveForwardField      = nullptr;
static jfieldID g_moveStrafeField       = nullptr;
static jfieldID g_entityMoveForwardField= nullptr;
static jfieldID g_entityMoveStrafeField = nullptr;
static jfieldID g_itemInUseCountField   = nullptr;
static jmethodID g_mid_isUsingItem      = nullptr;
static bool g_fieldsReady = false;

static bool initMovementFields(JNIEnv* env, jobject playerInstance) {
    if (g_fieldsReady)
        return true;

    jclass playerClass = env->GetObjectClass(playerInstance);
    if (!playerClass)
        return false;

    auto findField = [env](jclass cls, const char* name, const char* obf, const char* sig) -> jfieldID {
        jfieldID f = env->GetFieldID(cls, name, sig);
        if (!f) { env->ExceptionClear(); f = env->GetFieldID(cls, obf, sig); }
        if (!f && env->ExceptionCheck()) env->ExceptionClear();
        return f;
    };

    g_movementInputField     = findField(playerClass, "movementInput", "field_71158_b",
                                         "Lnet/minecraft/util/MovementInput;");
    g_entityMoveStrafeField  = findField(playerClass, "moveStrafing",  "field_70702_br", "F");
    g_entityMoveForwardField = findField(playerClass, "moveForward",   "field_70701_bs", "F");
    g_itemInUseCountField    = findField(playerClass, "itemInUseCount","field_71072_f",  "I");

    if (g_movementInputField) {
        jobject movInput = env->GetObjectField(playerInstance, g_movementInputField);
        if (movInput && !env->ExceptionCheck()) {
            jclass movInputCls = env->GetObjectClass(movInput);
            if (movInputCls) {
                g_moveForwardField = findField(movInputCls, "moveForward", "field_78900_b", "F");
                g_moveStrafeField  = findField(movInputCls, "moveStrafe",  "field_78902_a", "F");
                env->DeleteLocalRef(movInputCls);
            }
            env->DeleteLocalRef(movInput);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // isUsingItem method
    if (JniCache::cls_EntityPlayer && !g_mid_isUsingItem) {
        g_mid_isUsingItem = env->GetMethodID(JniCache::cls_EntityPlayer,
                                              "isUsingItem", "()Z");
        if (!g_mid_isUsingItem) {
            env->ExceptionClear();
            g_mid_isUsingItem = env->GetMethodID(JniCache::cls_EntityPlayer,
                                                   "func_71039_bw", "()Z");
            if (!g_mid_isUsingItem && env->ExceptionCheck()) env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(playerClass);
    if (env->ExceptionCheck()) env->ExceptionClear();

    g_fieldsReady = g_moveForwardField && g_moveStrafeField;
    return g_fieldsReady;
}

static bool isPlayerUsingItem(JNIEnv* env, jobject playerInstance) {
    // Primary: call isUsingItem()
    if (g_mid_isUsingItem) {
        bool result = env->CallBooleanMethod(playerInstance, g_mid_isUsingItem) == JNI_TRUE;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (result) return true;
    }
    // Fallback: itemInUseCount > 0
    if (g_itemInUseCountField) {
        int count = env->GetIntField(playerInstance, g_itemInUseCountField);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return count > 0;
    }
    return false;
}

}  // namespace

namespace SlowDownGate {

void JNICALL NativeOnSlowDown(JNIEnv* env, jclass, jobject playerInstance) {
    // Guard: module must be enabled and unlocked
    if (!noSlowDown || !noSlowDown->enabled)
        return;
    if (!playerInstance)
        return;

    if (!initMovementFields(env, playerInstance))
        return;
    if (!isPlayerUsingItem(env, playerInstance))
        return;

    // ---- Packet mode (blocking bypass) ----
    noSlowDown->handlePacketMode();

    jobject movInput = g_movementInputField
        ? env->GetObjectField(playerInstance, g_movementInputField)
        : nullptr;
    if (!movInput)
        return;

    float forward = env->GetFloatField(movInput, g_moveForwardField);
    float strafe  = env->GetFloatField(movInput, g_moveStrafeField);

    // ----- Per-item multipliers -----
    int heldId = player.getHeldItemId();
    float fwdMul = 1.0f, strafeMul = 1.0f;
    if (heldId == 261) {
        fwdMul = noSlowDown->bowForward;
        strafeMul = noSlowDown->bowStrafe;
    } else if (heldId == 267 || heldId == 268 || heldId == 269 || heldId == 272 ||
               heldId == 276 || heldId == 283) {
        fwdMul = noSlowDown->blockForward;
        strafeMul = noSlowDown->blockStrafe;
    } else {
        fwdMul = noSlowDown->consumeForward;
        strafeMul = noSlowDown->consumeStrafe;
    }

    forward *= fwdMul;
    strafe  *= strafeMul;

    // Counteract Minecraft's 0.2f scaling in onLivingUpdate
    float finalForward = forward * 5.0f;
    float finalStrafe  = strafe * 5.0f;

    env->SetFloatField(movInput, g_moveForwardField, finalForward);
    env->SetFloatField(movInput, g_moveStrafeField,  finalStrafe);

    if (g_entityMoveForwardField)
        env->SetFloatField(playerInstance, g_entityMoveForwardField, forward);
    if (g_entityMoveStrafeField)
        env->SetFloatField(playerInstance, g_entityMoveStrafeField,  strafe);

    env->DeleteLocalRef(movInput);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

bool Init(JNIEnv* env) {
    if (g_initialized.load())
        return true;
    if (!env)
        return false;

    // Try to find existing class first
    jclass hook = nullptr;
    {
        // Use obfuscated class name lookup
        jclass raw = env->FindClass("com/cobble/agent/SlowDownHook");
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (raw) {
            hook = raw;
        }
    }

    if (!hook && Java::ClassLoader) {
        hook = env->DefineClass("com/cobble/agent/SlowDownHook",
                                Java::ClassLoader,
                                reinterpret_cast<const jbyte*>(kSlowDownHookBytecode),
                                static_cast<jint>(sizeof(kSlowDownHookBytecode)));
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    if (!hook) {
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
        if (env->ExceptionCheck()) env->ExceptionClear();
        log_err("[SlowDownGate] RegisterNatives failed");
        env->DeleteLocalRef(hook);
        return false;
    }

    g_cls_SlowDownHook = (jclass)env->NewGlobalRef(hook);
    env->DeleteLocalRef(hook);
    g_initialized = true;
    log_ok("[SlowDownGate] SlowDownHook native bridge ready");
    return true;
}

void Shutdown(JNIEnv* env) {
    if (g_cls_SlowDownHook) {
        env->DeleteGlobalRef(g_cls_SlowDownHook);
        g_cls_SlowDownHook = nullptr;
    }
    g_initialized = false;
    g_fieldsReady = false;
}

}  // namespace SlowDownGate
