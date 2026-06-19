#include "no_slow_down.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

NoSlowDown* noSlowDown = nullptr;

NoSlowDown::NoSlowDown() : Module("NoSlowDown") {
    keybind = 0;
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

    movementInputField =
        env->GetFieldID(playerClass, "movementInput", "Lnet/minecraft/util/MovementInput;");
    if (!movementInputField) {
        env->ExceptionClear();
        movementInputField =
            env->GetFieldID(playerClass, "field_71158_b", "Lnet/minecraft/util/MovementInput;");
    }

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
        moveStrafeField = env->GetFieldID(movementInputClass, "moveStrafe", "F");
        if (!moveStrafeField) {
            env->ExceptionClear();
            moveStrafeField = env->GetFieldID(movementInputClass, "field_78902_a", "F");
        }

        moveForwardField = env->GetFieldID(movementInputClass, "moveForward", "F");
        if (!moveForwardField) {
            env->ExceptionClear();
            moveForwardField = env->GetFieldID(movementInputClass, "field_78900_b", "F");
        }
    }

    if (env->ExceptionCheck())
        env->ExceptionClear();

    if (movementInputClass)
        env->DeleteLocalRef(movementInputClass);
    env->DeleteLocalRef(movementInput);
    env->DeleteLocalRef(playerClass);

    return moveForwardField && moveStrafeField;
}

bool NoSlowDown::isKeyPressed(jobject gameSettings, jfieldID keyBindField) {
    JNIEnv* env = Java::Env();
    if (!env || !gameSettings || !keyBindField || !JniCache::fid_pressed)
        return false;

    jobject keyBind = env->GetObjectField(gameSettings, keyBindField);
    if (!keyBind || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    const bool pressed = env->GetBooleanField(keyBind, JniCache::fid_pressed) == JNI_TRUE;
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(keyBind);
    return pressed;
}

void NoSlowDown::onUpdate() {
    if (!enabled || Menu::isOpen || mc.isInGui() || !player.isValid() || !player.isUsingItem())
        return;
    if (!initializeFields() || !JniCache::fid_gameSettings)
        return;

    JNIEnv* env = Java::Env();
    jobject gameSettings = env->GetObjectField(mc.instance, JniCache::fid_gameSettings);
    jobject movementInput = env->GetObjectField(player.instance, movementInputField);
    if (env->ExceptionCheck() || !gameSettings || !movementInput) {
        env->ExceptionClear();
        if (gameSettings)
            env->DeleteLocalRef(gameSettings);
        if (movementInput)
            env->DeleteLocalRef(movementInput);
        return;
    }

    float forward = 0.0f;
    float strafe = 0.0f;

    if (isKeyPressed(gameSettings, JniCache::fid_keyBindForward))
        forward += 1.0f;
    if (isKeyPressed(gameSettings, JniCache::fid_keyBindBack))
        forward -= 1.0f;
    if (isKeyPressed(gameSettings, JniCache::fid_keyBindLeft))
        strafe += 1.0f;
    if (isKeyPressed(gameSettings, JniCache::fid_keyBindRight))
        strafe -= 1.0f;

    // Sneaking has its own intended movement reduction. Preserve it while
    // removing only the 0.2 item-use multiplier.
    if (player.isSneaking()) {
        forward *= 0.3f;
        strafe *= 0.3f;
    }

    env->SetFloatField(movementInput, moveForwardField, forward);
    env->SetFloatField(movementInput, moveStrafeField, strafe);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    env->DeleteLocalRef(movementInput);
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
