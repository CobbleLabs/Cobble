#include "criticals.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../jni/java.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <random>

Criticals* criticals = nullptr;

static std::mt19937 rng(std::random_device{}());

Criticals::Criticals() : Module("Criticals") {
    keybind = 0;
}

void Criticals::onEnable() {
    lastCriticalTime = 0;
    wasAttacking = false;
}

void Criticals::onDisable() {
}

bool Criticals::initJNI() {
    if (jniInitialized)
        return cls_C04Packet != nullptr && mid_C04Init != nullptr;

    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    // Try to find C04PacketPlayerPosition class (different names in different versions)
    jclass localCls = nullptr;
    
    // Try 1.8+ MCP names first
    if (!localCls && Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer$C04PacketPlayerPosition", localCls)) {
        cls_C04Packet = (jclass)env->NewGlobalRef(localCls);
        env->DeleteLocalRef(localCls);
    }

    if (!cls_C04Packet) {
        env->ExceptionClear();
        jniInitialized = true;
        return false;
    }

    // Try 1.8+ constructor first: (double x, double y, double z, boolean onGround)
    mid_C04Init = env->GetMethodID(cls_C04Packet, "<init>", "(DDDZ)V");
    if (mid_C04Init) {
        is17 = false;
    } else {
        env->ExceptionClear();
        // Try 1.7.10 constructor: (double x, double y, double stance, double z, boolean onGround)
        mid_C04Init = env->GetMethodID(cls_C04Packet, "<init>", "(DDDDZ)V");
        if (mid_C04Init) {
            is17 = true;
        }
    }
    
    if (!mid_C04Init) {
        env->ExceptionClear();
    }

    jniInitialized = true;
    return cls_C04Packet != nullptr && mid_C04Init != nullptr;
}

bool Criticals::isHoldingSword() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283);
}

bool Criticals::isLookingAtPlayer() {
    return mc.isTargetingEntity();
}

bool Criticals::checkChance() {
    if (settings.chance >= 100)
        return true;
    if (settings.chance <= 0)
        return false;
    std::uniform_int_distribution<int> dist(0, 100);
    return dist(rng) < settings.chance;
}

bool Criticals::canDoCritical() {
    if (!player.isValid())
        return false;

    // Must be on ground
    if (!player.isOnGround())
        return false;

    // Check delay
    unsigned long now = GetTickCount();
    if (now - lastCriticalTime < (unsigned long)settings.maxDelayMs)
        return false;

    // Check conditions
    if (settings.onlyWhileAttacking) {
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
            return false;
    }

    if (settings.lookingAtPlayer) {
        if (!isLookingAtPlayer())
            return false;
    }

    if (settings.onlyWithSword) {
        if (!isHoldingSword())
            return false;
    }

    // Don't do criticals while sneaking
    if (player.isSneaking())
        return false;

    // Check chance
    if (!checkChance())
        return false;

    return true;
}

void Criticals::sendCriticalPackets(double x, double y, double z) {
    if (!initJNI())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    // Less detectable: smaller offsets + randomization
    // Legit criticals only need a very small Y difference
    std::uniform_real_distribution<double> offsetDist(0.001, 0.01);
    double smallOffset = offsetDist(rng);

    // Minimal packet sequence - just 2 packets instead of 4
    // Packet 1: tiny jump (on ground)
    // Packet 2: back to ground (falling)
    
    jobject packet1 = nullptr;
    jobject packet2 = nullptr;
    
    if (is17) {
        // 1.7.10: (x, y, stance, z, onGround) - stance is y + 1.62 (eye height)
        double stance = y + 1.62;
        double stanceUp = y + smallOffset + 1.62;
        packet1 = env->NewObject(cls_C04Packet, mid_C04Init, x, y + smallOffset, stanceUp, z, (jboolean)true);
        packet2 = env->NewObject(cls_C04Packet, mid_C04Init, x, y, stance, z, (jboolean)false);
    } else {
        // 1.8+: (x, y, z, onGround)
        packet1 = env->NewObject(cls_C04Packet, mid_C04Init, x, y + smallOffset, z, (jboolean)true);
        packet2 = env->NewObject(cls_C04Packet, mid_C04Init, x, y, z, (jboolean)false);
    }
    
    if (packet1) {
        mc.sendPacket(packet1);
        env->DeleteLocalRef(packet1);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (packet2) {
        mc.sendPacket(packet2);
        env->DeleteLocalRef(packet2);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void Criticals::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    // Detect attack (LMB + looking at entity)
    bool isAttacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) && isLookingAtPlayer();

    // Detect attack start (rising edge)
    if (isAttacking && !wasAttacking) {
        if (canDoCritical()) {
            double x = player.getPosX();
            double y = player.getPosY();
            double z = player.getPosZ();
            
            sendCriticalPackets(x, y, z);
            lastCriticalTime = GetTickCount();
        }
    }

    wasAttacking = isAttacking;
}

void Criticals::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();

            int delay = settings.maxDelayMs;
            if (ui::SliderInt("Delay", &delay, 50, 500, "%dms"))
                settings.maxDelayMs = delay;

            int chance = settings.chance;
            if (ui::SliderInt("Chance", &chance, 0, 100, "%d%%"))
                settings.chance = chance;
        },
        "Conditions",
        [this]() {
            ui::Checkbox("Only Attacking", &settings.onlyWhileAttacking);
            ui::Checkbox("Looking At Player", &settings.lookingAtPlayer);
            ui::Checkbox("Only With Sword", &settings.onlyWithSword);
        });
}
