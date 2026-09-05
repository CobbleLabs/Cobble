#include "outgoing_gate.hpp"

#include "../altmanager/altmanager_gate.hpp"
#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../../module/combat/piercing.h"
#include "../../module/movement/fakelag.h"
#include "../../module/movement/timerrange.h"
#include "../../module/movement/blink.h"
#include "../../module/utility/clutch.h"
#include "../../module/utility/autorefill.h"
#include "../../module/utility/altmanager.h"
#include "../../module/utility/noitemrelease.h"
#include "../../module/utility/throwpots.h"
#include "../../module/utility/autopot.h"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/backtrack.h"
#include "../../module/combat/opportunity.h"

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

    // Fire any queued AutoRefill click just before this (flying) packet so the
    // window-click lands ahead of movement in the tick (avoids Grim's Post).
    // Passive "On Inventory Open" mode runs with enabled=false but still queues
    // clicks that must be flushed, so we also open the gate when that setting
    // is on. onOutgoingPacket() early-returns when there's no pending click.
    if (autoRefill && (autoRefill->enabled || autoRefill->settings.onInventoryOpen)) {
        autoRefill->onOutgoingPacket(env, packet);
    }

    const bool throwPotsPacketWork =
        throwPots && throwPots->enabled && throwPots->needsOutgoingPacket();
    const bool throwPotsRotating =
        throwPotsPacketWork && throwPots->isRewritingRotation();
    // AutoPot is the automatic counterpart and stands down whenever ThrowPots is
    // running, so the two can never claim the packet at the same time. Its silent
    // pitch outranks the aim modules for the same reason ThrowPots' does: a potion
    // aimed anywhere but the ground is a wasted potion.
    const bool autoPotPacketWork = autoPot && autoPot->enabled && autoPot->needsOutgoingPacket();
    const bool autoPotRotating = autoPotPacketWork && autoPot->isRewritingRotation();
    const bool potRotating = throwPotsRotating || autoPotRotating;
    // Clutch/Scaffold no longer rewrite this packet: they move the real rotation on the
    // client tick, so the flying packet already carries the placement aim by the time it
    // gets here. What is still needed is the priority -- while the placement owns the
    // head, the aim modules must not overwrite it on the wire.
    const bool clutchRotating =
        (clutch && clutch->enabled && clutch->isRewritingRotation()) ||
        (scaffold && scaffold->enabled && scaffold->isRewritingRotation());

    // Opportunity claims the head the same way, for the same reason: the web/lava/fire
    // placement is raytraced from the rotation this packet carries, so nothing may
    // overwrite it mid-flick. No `enabled` check -- a module that was switched off
    // mid-opportunity still owns the head until it has flicked back.
    const bool oppRotating = opportunity && opportunity->isRewritingRotation();

    // AimAssist packet-rotation (smooth camera): rewrites the outgoing movement
    // packet's yaw/pitch with the randomized server rotation. Deferred while a
    // placement or a potion owns the rotation so their aim is never perturbed.
    if (aimAssist && aimAssist->enabled && !clutchRotating && !potRotating && !oppRotating) {
        aimAssist->onOutgoingPacket(env, packet);
    }

    const bool releasingFakeLagPacket =
        fakeLag && fakeLag->enabled && fakeLag->isReleasing();

    // Piercing may perform a synchronous through-wall attack from the Windows
    // input hook. LWJGL can observe the same click once more through polling;
    // discard only that duplicate swing/attack before other packet observers.
    // A packet already accepted and retained by FakeLag must not be classified
    // as a fresh duplicate when it is released later.
    if (!releasingFakeLagPacket && piercing && piercing->enabled &&
        !piercing->shouldSendOutgoingPacket(env, packet))
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

    // Same placement, same reasoning, for the automatic one.
    if (autoPotPacketWork) {
        autoPot->onOutgoingPacket(env, packet);
    }

    // Finally check NoItemRelease. Only queries the gate if the module is on.
    jboolean shouldSend = JNI_TRUE;
    if (noItemRelease && noItemRelease->enabled) {
        shouldSend = NoItemReleaseGate::NativeShouldSend(env, cls, packet);
    }
    if (shouldSend == JNI_TRUE && timerRange && timerRange->enabled)
        timerRange->onOutgoingPacket(env, packet, TimerRange::PacketPhase::POST);

    return shouldSend;
}

}  // namespace OutgoingPacketGate
