#include "outgoing_gate.hpp"

#include "../altmanager/altmanager_gate.hpp"
#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../../module/combat/piercing.h"
#include "../../module/movement/fakelag.h"
#include "../../module/movement/timerrange.h"
#include "../../module/utility/clutch.h"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    if (clutch) {
        clutch->onOutgoingPacket(env, packet);
    }

    const bool releasingFakeLagPacket = fakeLag && fakeLag->isReleasing();

    // Piercing may perform a synchronous through-wall attack from the Windows
    // input hook. LWJGL can observe the same click once more through polling;
    // discard only that duplicate swing/attack before other packet observers.
    // A packet already accepted and retained by FakeLag must not be classified
    // as a fresh duplicate when it is released later.
    if (!releasingFakeLagPacket && piercing && !piercing->shouldSendOutgoingPacket(env, packet))
        return JNI_FALSE;

    // FakeLag retains only movement packets. Run it before packet observers so
    // a retained packet is observed once, when it is actually released.
    if (fakeLag && !fakeLag->handleOutgoingPacket(env, packet))
        return JNI_FALSE;

    if (timerRange)
        timerRange->onOutgoingPacket(env, packet, TimerRange::PacketPhase::PRE);

    // Mutate first (no blocking), then run gates that may drop the packet.
    AltManagerGate::MaybeSpoofLoginPacket(env, packet);

    // Check Blink - if it blocks, don't send
    if (!BlinkGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // Finally check NoItemRelease.
    const jboolean shouldSend = NoItemReleaseGate::NativeShouldSend(env, cls, packet);
    if (shouldSend == JNI_TRUE && timerRange)
        timerRange->onOutgoingPacket(env, packet, TimerRange::PacketPhase::POST);

    return shouldSend;
}

}  // namespace OutgoingPacketGate
