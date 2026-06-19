#include "outgoing_gate.hpp"

#include "../altmanager/altmanager_gate.hpp"
#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../../module/movement/timerrange.h"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    if (timerRange)
        timerRange->onOutgoingPacket(env, packet, TimerRange::PacketMode::PRE);

    // Mutate first (no blocking), then run gates that may drop the packet.
    AltManagerGate::MaybeSpoofLoginPacket(env, packet);

    // Check Blink - if it blocks, don't send
    if (!BlinkGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // Finally check NoItemRelease.
    const jboolean shouldSend = NoItemReleaseGate::NativeShouldSend(env, cls, packet);
    if (shouldSend == JNI_TRUE && timerRange)
        timerRange->onOutgoingPacket(env, packet, TimerRange::PacketMode::POST);

    return shouldSend;
}

}  // namespace OutgoingPacketGate
