#include "outgoing_gate.hpp"

#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../criticals/criticals_gate.hpp"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    // First check Criticals - if it delays, don't send now
    if (!CriticalsGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // Then check Blink - if it blocks, don't send
    if (!BlinkGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // Finally check NoItemRelease
    // Note: NoItemReleaseGate::NativeShouldSend has the same signature
    // but we call it directly to avoid JNI callback overhead
    return NoItemReleaseGate::NativeShouldSend(env, cls, packet);
}

}  // namespace OutgoingPacketGate
