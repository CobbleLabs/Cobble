#include "criticals_gate.hpp"

#include <cstring>
#include "../../module/combat/criticals.h"
#include "../../sdk/player/player.h"
#include "../../util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace {

// Check if packet is a movement packet (C03PacketPlayer and subclasses)
static bool isMovementPacket(JNIEnv* env, jobject packet) {
    if (!packet)
        return false;

    jclass cls = env->GetObjectClass(packet);
    if (!cls) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    jclass clsCls = env->GetObjectClass(cls);
    if (!clsCls) {
        env->DeleteLocalRef(cls);
        return false;
    }

    jmethodID mid = env->GetMethodID(clsCls, "getName", "()Ljava/lang/String;");
    if (!mid) {
        env->DeleteLocalRef(clsCls);
        env->DeleteLocalRef(cls);
        return false;
    }

    jstring jname = (jstring)env->CallObjectMethod(cls, mid);
    if (!jname) {
        env->DeleteLocalRef(clsCls);
        env->DeleteLocalRef(cls);
        return false;
    }

    const char* cstr = env->GetStringUTFChars(jname, nullptr);
    bool isMovement = false;

    if (cstr) {
        // C03PacketPlayer and its subclasses:
        // - C03PacketPlayer
        // - C03PacketPlayer$C04PacketPlayerPosition
        // - C03PacketPlayer$C05PacketPlayerLook
        // - C03PacketPlayer$C06PacketPlayerPosLook
        isMovement = (strstr(cstr, "C03PacketPlayer") != nullptr);
        env->ReleaseStringUTFChars(jname, cstr);
    }

    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(clsCls);
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return isMovement;
}

}  // namespace

namespace CriticalsGate {

bool ShouldSendPacket(JNIEnv* env, jobject packet) {
    // 1.7.10: Packet class names are different/obfuscated - skip entirely to prevent crash
    if (Java::Version == MinecraftVersion::MC_1_7_10) {
        return true;
    }

    // If criticals module is not initialized or not enabled, allow
    if (!::criticals || !::criticals->enabled) {
        return true;
    }

    // Only process movement packets
    if (!isMovementPacket(env, packet)) {
        return true;
    }

    // Ask criticals module if it should delay this packet
    if (!::criticals->shouldDelayPacket()) {
        return true;
    }

    // Queue the packet for delayed sending
    ::criticals->queuePacket(env, packet);
    return false;  // Block immediate send
}

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet) {
    return ShouldSendPacket(env, packet) ? JNI_TRUE : JNI_FALSE;
}

}  // namespace CriticalsGate
