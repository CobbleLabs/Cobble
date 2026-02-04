// Input-related functions for Minecraft SDK
// Handles keyboard input, movement keys, and mouse grab control

#include "../../util/logger.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// =============================================================================
// Helper: Get GameSettings object from Minecraft instance
// =============================================================================

static jobject getGameSettings(JNIEnv* env, jobject mcInstance) {
    if (!JniCache::fid_gameSettings)
        return nullptr;
    return env->GetObjectField(mcInstance, JniCache::fid_gameSettings);
}

// =============================================================================
// Helper: Get keycode from a keybind field
// =============================================================================

static int getKeyBindCode(JNIEnv* env, jobject mcInstance, jfieldID keyBindField, int defaultCode) {
    if (!keyBindField || !JniCache::mid_getKeyCode)
        return defaultCode;

    jobject gameSettings = getGameSettings(env, mcInstance);
    if (!gameSettings)
        return defaultCode;

    int code = defaultCode;
    jobject keyBind = env->GetObjectField(gameSettings, keyBindField);
    if (keyBind) {
        code = env->CallIntMethod(keyBind, JniCache::mid_getKeyCode);
        env->DeleteLocalRef(keyBind);
    }

    env->DeleteLocalRef(gameSettings);
    return code;
}

// =============================================================================
// setKeyBindState - Set a keybind's pressed state
// =============================================================================

void Minecraft::setKeyBindState(int keyCode, bool pressed) {
    if (!isValid())
        return;
    if (!JniCache::cls_KeyBinding || !JniCache::mid_setKeyBindState)
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    env->CallStaticVoidMethod(JniCache::cls_KeyBinding, JniCache::mid_setKeyBindState, keyCode, pressed);
}

// =============================================================================
// getSneakKey - Get the keycode for the sneak keybind
// =============================================================================

int Minecraft::getSneakKey() {
    if (!isValid())
        return 42;  // LSHIFT default
    JNIEnv* env = Java::Env();
    return env ? getKeyBindCode(env, instance, JniCache::fid_keyBindSneak, 42) : 42;
}

// =============================================================================
// getSprintKey - Get the keycode for the sprint keybind
// =============================================================================

int Minecraft::getSprintKey() {
    if (!isValid())
        return 29;  // LCTRL default (LWJGL key code)
    JNIEnv* env = Java::Env();
    return env ? getKeyBindCode(env, instance, JniCache::fid_keyBindSprint, 29) : 29;
}

// =============================================================================
// getInventoryKey - Get the keycode for the inventory keybind
// =============================================================================

int Minecraft::getInventoryKey() {
    if (!isValid())
        return 18;  // 'E' default (LWJGL key code)
    JNIEnv* env = Java::Env();
    return env ? getKeyBindCode(env, instance, JniCache::fid_keyBindInventory, 18) : 18;
}

// =============================================================================
// releaseMovementKeys - Release all movement keybinds when menu opens
// =============================================================================

void Minecraft::releaseMovementKeys() {
    if (!isValid())
        return;
    if (!JniCache::cls_KeyBinding)
        return;

    // Check if already released to prevent spam and double-save
    if (keysReleased)
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    jobject gameSettings = getGameSettings(env, instance);
    if (!gameSettings)
        return;

    // Use cached field IDs for each keybind
    jfieldID keyBindFields[] = {JniCache::fid_keyBindForward, JniCache::fid_keyBindBack,   JniCache::fid_keyBindLeft,
                                JniCache::fid_keyBindRight,   JniCache::fid_keyBindJump,   JniCache::fid_keyBindSneak,
                                JniCache::fid_keyBindAttack,  JniCache::fid_keyBindUseItem};

    const char* keyBindNames[] = {"keyBindForward", "keyBindBack",  "keyBindLeft",   "keyBindRight",
                                  "keyBindJump",    "keyBindSneak", "keyBindAttack", "keyBindUseItem"};

    savedKeyStates.clear();

    for (int i = 0; i < 8; i++) {
        jfieldID fid_kb = keyBindFields[i];
        if (!fid_kb)
            continue;

        jobject keyBind = env->GetObjectField(gameSettings, fid_kb);
        if (!keyBind)
            continue;

        // Save current state
        bool isPressed = false;
        if (JniCache::fid_pressed) {
            isPressed = env->GetBooleanField(keyBind, JniCache::fid_pressed);
            savedKeyStates[keyBindNames[i]] = isPressed;
        }

        // Force release via setKeyBindState
        if (JniCache::mid_getKeyCode && JniCache::mid_setKeyBindState) {
            int keyCode = env->CallIntMethod(keyBind, JniCache::mid_getKeyCode);
            env->CallStaticVoidMethod(JniCache::cls_KeyBinding, JniCache::mid_setKeyBindState, keyCode, JNI_FALSE);
        }

        // Also set internal pressed field to false if possible
        if (JniCache::fid_pressed) {
            env->SetBooleanField(keyBind, JniCache::fid_pressed, JNI_FALSE);
        }

        env->DeleteLocalRef(keyBind);
    }

    keysReleased = true;
    env->DeleteLocalRef(gameSettings);
}

// =============================================================================
// restoreMovementKeys - Restore keybinds when menu closes
// =============================================================================

void Minecraft::restoreMovementKeys() {
    if (!isValid())
        return;
    if (!JniCache::cls_KeyBinding || !JniCache::mid_setKeyBindState || !JniCache::mid_getKeyCode)
        return;

    // Only restore if we actually released them
    if (!keysReleased)
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    jobject gameSettings = getGameSettings(env, instance);
    if (!gameSettings)
        return;

    jfieldID keyBindFields[] = {JniCache::fid_keyBindForward, JniCache::fid_keyBindBack,   JniCache::fid_keyBindLeft,
                                JniCache::fid_keyBindRight,   JniCache::fid_keyBindJump,   JniCache::fid_keyBindSneak,
                                JniCache::fid_keyBindAttack,  JniCache::fid_keyBindUseItem};

    const char* keyBindNames[] = {"keyBindForward", "keyBindBack",  "keyBindLeft",   "keyBindRight",
                                  "keyBindJump",    "keyBindSneak", "keyBindAttack", "keyBindUseItem"};

    for (int i = 0; i < 8; i++) {
        jfieldID fid_kb = keyBindFields[i];
        if (!fid_kb)
            continue;

        jobject keyBind = env->GetObjectField(gameSettings, fid_kb);
        if (!keyBind)
            continue;

        int keyCode = env->CallIntMethod(keyBind, JniCache::mid_getKeyCode);

        // 1. Check Saved State
        bool wasPressed = savedKeyStates[keyBindNames[i]];

        // 2. Check Physical State
        bool isPhysicallyHeld = false;

        if (keyCode < 0) {
            // Mouse button
            int btn = keyCode + 100;
            if (btn == 0)
                isPhysicallyHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            else if (btn == 1)
                isPhysicallyHeld = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            else if (btn == 2)
                isPhysicallyHeld = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        } else {
            // Keyboard key
            UINT vk = MapVirtualKeyA(keyCode, MAPVK_VSC_TO_VK);
            if (vk != 0) {
                isPhysicallyHeld = (GetAsyncKeyState(vk) & 0x8000) != 0;
            }
        }

        // Restore if EITHER was pressed or IS pressed
        if (wasPressed || isPhysicallyHeld) {
            env->CallStaticVoidMethod(JniCache::cls_KeyBinding, JniCache::mid_setKeyBindState, keyCode, JNI_TRUE);

            if (JniCache::fid_pressed) {
                env->SetBooleanField(keyBind, JniCache::fid_pressed, JNI_TRUE);
            }
        }

        env->DeleteLocalRef(keyBind);
    }

    keysReleased = false;
    savedKeyStates.clear();
    env->DeleteLocalRef(gameSettings);
}

// =============================================================================
// ungrabMouse - Release mouse from LWJGL capture
// =============================================================================

void Minecraft::ungrabMouse() {
    if (!isValid())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    // Method 1: Try mouseHelper approach
    if (JniCache::fid_mouseHelper && JniCache::mid_ungrabMouseCursor) {
        jobject mouseHelper = env->GetObjectField(instance, JniCache::fid_mouseHelper);
        if (mouseHelper) {
            env->CallVoidMethod(mouseHelper, JniCache::mid_ungrabMouseCursor);
            env->DeleteLocalRef(mouseHelper);
        }
    }
    env->ExceptionClear();

    // Method 2: Set inGameHasFocus = false
    if (JniCache::fid_inGameHasFocus) {
        env->SetBooleanField(instance, JniCache::fid_inGameHasFocus, JNI_FALSE);
    }
    env->ExceptionClear();

    // Method 3: Direct LWJGL fallback
    if (JniCache::cls_Mouse && JniCache::mid_setGrabbed) {
        env->CallStaticVoidMethod(JniCache::cls_Mouse, JniCache::mid_setGrabbed, JNI_FALSE);
    }
    env->ExceptionClear();
}

// =============================================================================
// grabMouse - Re-capture mouse with LWJGL
// =============================================================================

void Minecraft::grabMouse() {
    if (!isValid())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    // Set inGameHasFocus = true first
    if (JniCache::fid_inGameHasFocus) {
        env->SetBooleanField(instance, JniCache::fid_inGameHasFocus, JNI_TRUE);
    }
    env->ExceptionClear();

    // Call mouseHelper.grabMouseCursor()
    if (JniCache::fid_mouseHelper && JniCache::mid_grabMouseCursor) {
        jobject mouseHelper = env->GetObjectField(instance, JniCache::fid_mouseHelper);
        if (mouseHelper) {
            env->CallVoidMethod(mouseHelper, JniCache::mid_grabMouseCursor);
            env->DeleteLocalRef(mouseHelper);
        }
    }
    env->ExceptionClear();

    // Direct LWJGL fallback
    if (JniCache::cls_Mouse && JniCache::mid_setGrabbed) {
        env->CallStaticVoidMethod(JniCache::cls_Mouse, JniCache::mid_setGrabbed, JNI_TRUE);
    }
    env->ExceptionClear();
}
