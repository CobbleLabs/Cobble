#include "no_slow_down.h"

#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#include <algorithm>
#include <cmath>

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

void NoSlowDown::onUpdate() {
    if (!enabled || Menu::isOpen || mc.isInGui() || !player.isValid() || !player.isUsingItem())
        return;
    if (!initializeFields())
        return;

    JNIEnv* env = Java::Env();
    jobject movementInput = env->GetObjectField(player.instance, movementInputField);
    if (env->ExceptionCheck() || !movementInput) {
        env->ExceptionClear();
        if (movementInput)
            env->DeleteLocalRef(movementInput);
        return;
    }

    float forward = env->GetFloatField(movementInput, moveForwardField);
    float strafe = env->GetFloatField(movementInput, moveStrafeField);

    // Vanilla applies a 0.2 multiplier after movement input is calculated.
    // Compensate only values in that slowed range so repeated worker updates
    // cannot amplify already restored input.
    const auto restoreInput = [](float value) {
        if (std::abs(value) <= 0.2001f)
            return (std::clamp)(value * 5.0f, -1.0f, 1.0f);
        return value;
    };
    forward = restoreInput(forward);
    strafe = restoreInput(strafe);

    env->SetFloatField(movementInput, moveForwardField, forward);
    env->SetFloatField(movementInput, moveStrafeField, strafe);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    env->DeleteLocalRef(movementInput);
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
