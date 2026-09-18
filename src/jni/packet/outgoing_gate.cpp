#include "outgoing_gate.hpp"

#include "../altmanager/altmanager_gate.hpp"
#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../../module/combat/piercing.h"
#include "../../module/movement/fakelag.h"
#include "../../module/movement/timerrange.h"
#include "../../module/movement/blink.h"
#include "../../module/utility/autorefill.h"
#include "../../module/utility/altmanager.h"
#include "../../module/utility/noitemrelease.h"
#include "../../module/utility/throwpots.h"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/silentaura.h"
#include "../../module/combat/backtrack.h"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    // This runs on the Netty write thread for EVERY outgoing packet — including
    // the movement packets that carry your position 20 times a second. Any extra
    // work here shows up directly as added ping. Each module observer is gated
    // on its own `enabled` flag so a disabled module is a single-load atomic
    // compare instead of a virtual call + JNI IsInstanceOf.

    // The offline-mode username is carried by C00PacketLoginStart. Rewrite it
    // before any packet queue/filter can retain the original profile.
    if (altManager && altManager->isActive()) {
        AltManagerGate::MaybeSpoofLoginPacket(env, packet);
    }

    if (silentAura && !silentAura->shouldSendOutgoingPacket(env, packet))
        return JNI_FALSE;

    // Flush a complete AutoRefill swap command immediately before the next
    // flying packet. The atomic command is the only state shared with Netty.
    if (autoRefill && autoRefill->hasPendingClick()) {
        autoRefill->onOutgoingPacket(env, packet);
    }

    const bool throwPotsPacketWork = throwPots && throwPots->enabled && throwPots->needsOutgoingPacket();
    const bool throwPotsRotating = throwPotsPacketWork && throwPots->isRewritingRotation();

    const bool silentAuraRotating = silentAura && silentAura->isRewritingRotation();

    // AimAssist packet-rotation (smooth camera): rewrites the outgoing movement
    // packet's yaw/pitch with the randomized server rotation. Deferred while a
    // potion or SilentAura owns the rotation.
    if (aimAssist && aimAssist->enabled && !silentAuraRotating && !throwPotsRotating) {
        aimAssist->onOutgoingPacket(env, packet);
    }

    const bool releasingFakeLagPacket = fakeLag && fakeLag->enabled && fakeLag->isReleasing();

    // Piercing may perform a synchronous through-wall attack from the Windows
    // input hook. LWJGL can observe the same click once more through polling;
    // discard only that duplicate swing/attack before other packet observers.
    // A packet already accepted and retained by FakeLag must not be classified
    // as a fresh duplicate when it is released later.
    if (!releasingFakeLagPacket && piercing && piercing->enabled && !piercing->shouldSendOutgoingPacket(env, packet))
        return JNI_FALSE;

    // FakeLag retains only movement packets. Run it before packet observers so
    // a retained packet is observed once, when it is actually released.
    if (fakeLag && fakeLag->enabled && !fakeLag->handleOutgoingPacket(env, packet))
        return JNI_FALSE;

    if (timerRange && timerRange->enabled)
        timerRange->onOutgoingPacket(env, packet, TimerRange::PacketPhase::PRE);

    // Check Blink - if it blocks, don't send.
    if (::blink && ::blink->enabled) {
        if (!BlinkGate::ShouldSendPacket(env, packet))
            return JNI_FALSE;
    }

    // ThrowPots runs after FakeLag/Blink so a queued PlayerControllerMP
    // sendUseItem is emitted only before a flying packet that will really pass.
    // When aiming down, its pitch also has final ownership of that packet.
    if (throwPotsPacketWork) {
        throwPots->onOutgoingPacket(env, packet);
    }

    // Finally check NoItemRelease. Only queries the gate if the module is on.
    jboolean shouldSend = JNI_TRUE;
    if (noItemRelease && noItemRelease->enabled) {
        shouldSend = NoItemReleaseGate::NativeShouldSend(env, cls, packet);
    }
    if (shouldSend == JNI_TRUE && silentAura) {
        if (silentAuraRotating && !throwPotsRotating)
            silentAura->onOutgoingPacket(env, packet);
        else
            silentAura->observeAcceptedOutgoingPacket(env, packet, true);
    }
    if (shouldSend == JNI_TRUE && timerRange && timerRange->enabled)
        timerRange->onOutgoingPacket(env, packet, TimerRange::PacketPhase::POST);

    return shouldSend;
}

}  // namespace OutgoingPacketGate
