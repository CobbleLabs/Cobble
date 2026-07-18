#include "outgoing_gate.hpp"

#include "../altmanager/altmanager_gate.hpp"
#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../../module/combat/piercing.h"
#include "../../module/movement/fakelag.h"
#include "../../module/movement/timerrange.h"
#include "../../module/utility/clutch.h"
#include "../../module/utility/autorefill.h"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/backtrack.h"
#include "../../module/combat/killaura.h"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    // The offline-mode username is carried by C00PacketLoginStart. Rewrite it
    // before any packet queue/filter can retain the original profile.
    AltManagerGate::MaybeSpoofLoginPacket(env, packet);

    // Fire any queued AutoRefill click just before this (flying) packet so the
    // window-click lands ahead of movement in the tick (avoids Grim's Post).
    if (autoRefill) {
        autoRefill->onOutgoingPacket(env, packet);
    }

    const bool clutchRotating = (clutch && clutch->isRewritingRotation()) ||
                                (scaffold && scaffold->isRewritingRotation());
    if (scaffold) {
        scaffold->onOutgoingPacket(env, packet);
    }
    if (clutch) {
        clutch->onOutgoingPacket(env, packet);
    }

    // KillAura silent rotation: rewrites the flying packet's yaw/pitch onto the
    // combat aim. Deferred while clutch owns the rotation (placement aim).
    const bool killAuraRotating = killAura && killAura->isRewritingRotation();
    if (killAura && !clutchRotating) {
        killAura->onOutgoingPacket(env, packet);
    }

    // AimAssist packet-rotation (smooth camera): rewrites the outgoing movement
    // packet's yaw/pitch with the randomized server rotation. Deferred while
    // clutch or KillAura is rewriting rotation so a combat/placement aim is never
    // perturbed.
    if (aimAssist && !clutchRotating && !killAuraRotating) {
        aimAssist->onOutgoingPacket(env, packet);
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
