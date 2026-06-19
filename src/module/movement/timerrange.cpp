#include "timerrange.h"

#include "timer.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/aes_string.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

TimerRange* timerRange = nullptr;

namespace {
bool isHoldingWeapon() {
    switch (player.getHeldItemId()) {
        case 267:
        case 268:
        case 272:
        case 276:
        case 283:  // Swords
        case 258:
        case 271:
        case 275:
        case 279:
        case 286:  // Axes
            return true;
        default:
            return false;
    }
}

bool isMovementPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet)
        return false;

    jclass cls = env->GetObjectClass(packet);
    if (!cls) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    jclass classClass = env->GetObjectClass(cls);
    if (!classClass) {
        env->DeleteLocalRef(cls);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    jmethodID getName =
        env->GetMethodID(classClass, OBFUSCATE_STRING("getName").c_str(),
                         OBFUSCATE_STRING("()Ljava/lang/String;").c_str());
    if (!getName) {
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(cls);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    jstring name = static_cast<jstring>(env->CallObjectMethod(cls, getName));
    bool movementPacket = false;
    if (name) {
        const char* chars = env->GetStringUTFChars(name, nullptr);
        if (chars) {
            movementPacket = std::strstr(chars, OBFUSCATE_STRING("C03PacketPlayer").c_str()) != nullptr;
            env->ReleaseStringUTFChars(name, chars);
        }
        env->DeleteLocalRef(name);
    }

    env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(cls);
    if (env->ExceptionCheck())
        env->ExceptionClear();

    return movementPacket;
}
}  // namespace

TimerRange::TimerRange() : Module("TimerRange"), rng(std::random_device{}()) {
    keybind = 0;
}

void TimerRange::onEnable() {
    if (timerModule && timerModule->enabled)
        timerModule->setEnabled(false);

    selectNextSpeed(true);
}

void TimerRange::onDisable() {
    currentSpeed.store(1.0f, std::memory_order_relaxed);
    mc.setTimerSpeed(1.0f);
}

void TimerRange::selectNextSpeed(bool force) {
    std::lock_guard<std::mutex> lock(speedMutex);

    const auto now = std::chrono::steady_clock::now();
    const int updateIntervalMs = (std::clamp)(settings.updateIntervalMs, 50, 1000);
    if (!force && lastSpeedChange.time_since_epoch().count() != 0) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSpeedChange).count();
        if (elapsed < updateIntervalMs)
            return;
    }

    const float minSpeed = (std::clamp)((std::min)(settings.minSpeed, settings.maxSpeed), 1.0f, 3.0f);
    const float maxSpeed = (std::clamp)((std::max)(settings.minSpeed, settings.maxSpeed), minSpeed, 3.0f);
    std::uniform_real_distribution<float> distribution(minSpeed, maxSpeed);
    currentSpeed.store(distribution(rng), std::memory_order_relaxed);
    lastSpeedChange = now;
}

void TimerRange::onOutgoingPacket(JNIEnv* env, jobject packet, PacketMode phase) {
    if (!enabled || settings.mode != phase || !isMovementPacket(env, packet))
        return;

    selectNextSpeed(false);
}

void TimerRange::onUpdate() {
    if (Menu::isOpen || !player.isValid()) {
        mc.setTimerSpeed(1.0f);
        return;
    }

    bool shouldApply = true;
    if (settings.mousePressed && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        shouldApply = false;
    if (settings.weaponOnly && !isHoldingWeapon())
        shouldApply = false;

    if (!shouldApply) {
        mc.setTimerSpeed(1.0f);
        return;
    }

    mc.setTimerSpeed(currentSpeed.load(std::memory_order_relaxed));
}

void TimerRange::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ImGui::Spacing();
            const char* modes[] = {"PRE", "POST"};
            int mode = static_cast<int>(settings.mode);
            if (ui::Combo("Packet Mode", &mode, modes, IM_ARRAYSIZE(modes)))
                settings.mode = static_cast<PacketMode>(mode);

            ImGui::Spacing();
            ui::SliderFloat("Minimum Speed", &settings.minSpeed, 1.0f, 3.0f, "%.2fx", true);
            ui::SliderFloat("Maximum Speed", &settings.maxSpeed, 1.0f, 3.0f, "%.2fx", true);

            float interval = static_cast<float>(settings.updateIntervalMs);
            if (ui::SliderFloat("Change Interval", &interval, 50.0f, 1000.0f, "%.0f ms", true))
                settings.updateIntervalMs = static_cast<int>(interval);

            settings.minSpeed = (std::clamp)(settings.minSpeed, 1.0f, 3.0f);
            settings.maxSpeed = (std::clamp)(settings.maxSpeed, 1.0f, 3.0f);
            if (settings.minSpeed > settings.maxSpeed)
                settings.maxSpeed = settings.minSpeed;
            settings.updateIntervalMs = (std::clamp)(settings.updateIntervalMs, 50, 1000);
        },
        "Conditions",
        [this]() {
            ui::Checkbox("Mouse Pressed", &settings.mousePressed);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
        });
}

std::string TimerRange::getDisplayInfo() {
    char buffer[64];
    const char* mode = settings.mode == PacketMode::PRE ? "PRE" : "POST";
    std::snprintf(buffer, sizeof(buffer), "%s %.2f-%.2fx", mode, settings.minSpeed, settings.maxSpeed);
    return buffer;
}
