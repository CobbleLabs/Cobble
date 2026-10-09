#include "blink_gate.hpp"

#include "../../module/movement/blink.h"
#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"

namespace {

// Packets are matched by class identity: renaming clients (CheatBreaker) hide the
// MCP class names, and IsInstanceOf also covers the C04/C05/C06 movement subclasses.
struct PacketClasses {
    jclass movement = nullptr;
    jclass useEntity = nullptr;
    jclass blockPlacement = nullptr;
    jclass digging = nullptr;
};

jclass resolveClass(JNIEnv* env, const std::string& name) {
    jclass local = nullptr;
    if (!Java::AssignClass(name, local))
        return nullptr;
    jclass global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
}

const PacketClasses& packetClasses(JNIEnv* env) {
    static const PacketClasses classes{
        resolveClass(env, OBFUSCATE_STRING("net.minecraft.network.play.client.C03PacketPlayer")),
        resolveClass(env, OBFUSCATE_STRING("net.minecraft.network.play.client.C02PacketUseEntity")),
        resolveClass(env, OBFUSCATE_STRING("net.minecraft.network.play.client.C08PacketPlayerBlockPlacement")),
        resolveClass(env, OBFUSCATE_STRING("net.minecraft.network.play.client.C07PacketPlayerDigging")),
    };
    return classes;
}

bool isInstance(JNIEnv* env, jobject packet, jclass cls) {
    return packet && cls && env->IsInstanceOf(packet, cls);
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

    const PacketClasses& classes = packetClasses(env);

    // Block use entity while blink is active (anti-ban)
    if (isInstance(env, packet, classes.useEntity)) {
        log_warn("[Blink] Blocked UseEntity while blinking");
        return false;
    }

    // Block bow use (C08 use item + C07 release use) while blink is active
    if (isInstance(env, packet, classes.blockPlacement) || isInstance(env, packet, classes.digging)) {
        log_warn("[Blink] Blocked bow use while blinking");
        return false;
    }

    // If movement only is enabled, only block movement packets
    if (::blink->settings.movementOnly) {
        if (!isInstance(env, packet, classes.movement)) {
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
