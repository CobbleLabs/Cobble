#include "outgoing_gate.hpp"

#include "../altmanager/altmanager_gate.hpp"
#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    // Mutate first (no blocking), then run gates that may drop the packet.
    AltManagerGate::MaybeSpoofLoginPacket(env, packet);

    // Check Blink - if it blocks, don't send
    if (!BlinkGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // Finally check NoItemRelease
    return NoItemReleaseGate::NativeShouldSend(env, cls, packet);
}

}  // namespace OutgoingPacketGate
