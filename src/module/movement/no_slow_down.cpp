#include "no_slow_down.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#include <algorithm>
#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

NoSlowDown* noSlowDown = nullptr;

NoSlowDown::NoSlowDown() : Module("NoSlowDown") {
    keybind = 0;
    locked = true;
}

bool NoSlowDown::initializeFields() {
    if (movementInputField && moveForwardField && moveStrafeField)
        return true;

    JNIEnv* env = Java::Env();
    if (!env || !player.isValid())
        return false;

    jclass playerClass = env->GetObjectClass(player.instance);
    if (!playerClass)
        return false;

    const auto findField = [env](jclass cls, const char* name, const char* fallback,
                                 const char* signature) -> jfieldID {
        jfieldID field = env->GetFieldID(cls, name, signature);
        if (!field) {
            env->ExceptionClear();
            field = env->GetFieldID(cls, fallback, signature);
        }
        if (!field && env->ExceptionCheck())
            env->ExceptionClear();
        return field;
    };

    movementInputField = findField(playerClass, "movementInput", "field_71158_b",
                                   "Lnet/minecraft/util/MovementInput;");
    entityMoveStrafeField = findField(playerClass, "moveStrafing", "field_70702_br", "F");
    entityMoveForwardField = findField(playerClass, "moveForward", "field_70701_bs", "F");
    itemInUseCountField = findField(playerClass, "itemInUseCount", "field_71072_f", "I");

    if (env->ExceptionCheck() || !movementInputField) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerClass);
        return false;
    }

    jobject movementInput = env->GetObjectField(player.instance, movementInputField);
    if (env->ExceptionCheck() || !movementInput) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerClass);
        return false;
    }

    jclass movementInputClass = env->GetObjectClass(movementInput);
    if (movementInputClass) {
        moveStrafeField = findField(movementInputClass, "moveStrafe", "field_78902_a", "F");
        moveForwardField = findField(movementInputClass, "moveForward", "field_78900_b", "F");
    }

    if (env->ExceptionCheck())
        env->ExceptionClear();

    if (movementInputClass)
        env->DeleteLocalRef(movementInputClass);
    env->DeleteLocalRef(movementInput);
    env->DeleteLocalRef(playerClass);

    return moveForwardField && moveStrafeField;
}

bool NoSlowDown::isKeyPhysicallyPressed(jobject gameSettings, jfieldID keyBindField) {
    JNIEnv* env = Java::Env();
    if (!env || !gameSettings || !keyBindField)
        return false;

    jobject keyBind = env->GetObjectField(gameSettings, keyBindField);
    if (!keyBind || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    bool pressed = false;
    bool physicalStateAvailable = false;
    if (JniCache::mid_getKeyCode) {
        const int keyCode = env->CallIntMethod(keyBind, JniCache::mid_getKeyCode);
        if (!env->ExceptionCheck()) {
            if (keyCode < 0) {
                const int button = keyCode + 100;
                const int virtualKey = button == 0 ? VK_LBUTTON :
                                       button == 1 ? VK_RBUTTON :
                                       button == 2 ? VK_MBUTTON : 0;
                physicalStateAvailable = virtualKey != 0;
                pressed = virtualKey != 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            } else {
                const UINT virtualKey = MapVirtualKeyA(static_cast<UINT>(keyCode), MAPVK_VSC_TO_VK);
                physicalStateAvailable = virtualKey != 0;
                pressed = virtualKey != 0 && (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            }
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (!physicalStateAvailable && JniCache::fid_pressed)
        pressed = env->GetBooleanField(keyBind, JniCache::fid_pressed) == JNI_TRUE;
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(keyBind);
    return pressed;
}

bool NoSlowDown::isPlayerUsingItem() {
    JNIEnv* env = Java::Env();
    if (!env || !player.isValid())
        return false;

    bool usingItem = player.isUsingItem();
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        usingItem = false;
    }

    if (!usingItem && itemInUseCountField) {
        usingItem = env->GetIntField(player.instance, itemInUseCountField) > 0;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            usingItem = false;
        }
    }

    return usingItem;
}

void NoSlowDown::onUpdate() {
    if (!enabled || Menu::isOpen || mc.isInGui() || !player.isValid())
        return;
    if (!initializeFields() || !isPlayerUsingItem())
        return;

    JNIEnv* env = Java::Env();
    jobject gameSettings = JniCache::fid_gameSettings
                               ? env->GetObjectField(mc.instance, JniCache::fid_gameSettings)
                               : nullptr;
    jobject movementInput = env->GetObjectField(player.instance, movementInputField);
    if (env->ExceptionCheck() || !movementInput) {
        env->ExceptionClear();
        if (gameSettings)
            env->DeleteLocalRef(gameSettings);
        if (movementInput)
            env->DeleteLocalRef(movementInput);
        return;
    }

    float forward = 0.0f;
    float strafe = 0.0f;
    const bool hasKeyState = gameSettings &&
                             JniCache::fid_keyBindForward && JniCache::fid_keyBindBack &&
                             JniCache::fid_keyBindLeft && JniCache::fid_keyBindRight;

    if (hasKeyState) {
        if (isKeyPhysicallyPressed(gameSettings, JniCache::fid_keyBindForward))
            forward += 1.0f;
        if (isKeyPhysicallyPressed(gameSettings, JniCache::fid_keyBindBack))
            forward -= 1.0f;
        if (isKeyPhysicallyPressed(gameSettings, JniCache::fid_keyBindLeft))
            strafe += 1.0f;
        if (isKeyPhysicallyPressed(gameSettings, JniCache::fid_keyBindRight))
            strafe -= 1.0f;
    } else {
        const auto restoreInput = [](float value) {
            if (std::abs(value) <= 0.2001f)
                return (std::clamp)(value * 5.0f, -1.0f, 1.0f);
            return value;
        };
        forward = restoreInput(env->GetFloatField(movementInput, moveForwardField));
        strafe = restoreInput(env->GetFloatField(movementInput, moveStrafeField));
    }

    if (player.isSneaking()) {
        forward *= 0.3f;
        strafe *= 0.3f;
    }

    env->SetFloatField(movementInput, moveForwardField, forward);
    env->SetFloatField(movementInput, moveStrafeField, strafe);
    if (entityMoveForwardField)
        env->SetFloatField(player.instance, entityMoveForwardField, forward);
    if (entityMoveStrafeField)
        env->SetFloatField(player.instance, entityMoveStrafeField, strafe);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    env->DeleteLocalRef(movementInput);
    if (gameSettings)
        env->DeleteLocalRef(gameSettings);
}

void NoSlowDown::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);
        },
        "Info",
        []() {
            ImGui::TextWrapped("Prevents movement slowdown while blocking, eating, drinking, drawing a bow, or using other items.");
            ImGui::Spacing();
            ImGui::TextWrapped("Sneaking slowdown is preserved.");
        });
}
