#include "blink_gate.hpp"

#include <cstring>
#include "../../module/movement/blink.h"
#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"

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

    jmethodID mid =
        env->GetMethodID(clsCls, OBFUSCATE_STRING("getName").c_str(), OBFUSCATE_STRING("()Ljava/lang/String;").c_str());
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
        isMovement = (strstr(cstr, OBFUSCATE_STRING("C03PacketPlayer").c_str()) != nullptr);
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

static bool isUseEntityPacket(JNIEnv* env, jobject packet) {
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

    jmethodID mid =
        env->GetMethodID(clsCls, OBFUSCATE_STRING("getName").c_str(), OBFUSCATE_STRING("()Ljava/lang/String;").c_str());
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
    bool isUseEntity = false;

    if (cstr) {
        // C02PacketUseEntity (attack/interact)
        isUseEntity = (strstr(cstr, OBFUSCATE_STRING("C02PacketUseEntity").c_str()) != nullptr);
        env->ReleaseStringUTFChars(jname, cstr);
    }

    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(clsCls);
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return isUseEntity;
}

static bool isBowUsePacket(JNIEnv* env, jobject packet) {
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

    jmethodID mid =
        env->GetMethodID(clsCls, OBFUSCATE_STRING("getName").c_str(), OBFUSCATE_STRING("()Ljava/lang/String;").c_str());
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
    bool isBow = false;

    if (cstr) {
        // C08PacketPlayerBlockPlacement (use item) + C07PacketPlayerDigging (release use)
        isBow = (strstr(cstr, OBFUSCATE_STRING("C08PacketPlayerBlockPlacement").c_str()) != nullptr) ||
                (strstr(cstr, OBFUSCATE_STRING("C07PacketPlayerDigging").c_str()) != nullptr);
        env->ReleaseStringUTFChars(jname, cstr);
    }

    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(clsCls);
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return isBow;
}

}  // namespace

namespace BlinkGate {

bool ShouldSendPacket(JNIEnv* env, jobject packet) {
    // If blink is not initialized or not enabled, allow
    if (!::blink || !::blink->enabled) {
        return true;
    }

    // Ask module if it should block
    if (!::blink->shouldBlockPacket()) {
        return true;  // Don't block
    }

    // Block use entity while blink is active (anti-ban)
    if (isUseEntityPacket(env, packet)) {
        log_warn("[Blink] Blocked UseEntity while blinking");
        return false;
    }

    // Block bow use while blink is active
    if (isBowUsePacket(env, packet)) {
        log_warn("[Blink] Blocked bow use while blinking");
        return false;
    }

    // If movement only is enabled, only block movement packets
    if (::blink->settings.movementOnly) {
        if (!isMovementPacket(env, packet)) {
            return true;  // Not movement, let it pass
        }
    }

    // Block the packet
    ::blink->queuePacket(env, packet);
    log_debug("[Blink] Blocked outgoing packet");
    return false;
}

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass, jobject packet) {
    return ShouldSendPacket(env, packet) ? JNI_TRUE : JNI_FALSE;
}

}  // namespace BlinkGate
