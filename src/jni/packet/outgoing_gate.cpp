#include "outgoing_gate.hpp"

#include "../altmanager/altmanager_gate.hpp"
#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../../module/combat/piercing.h"
#include "../../module/movement/blink.h"
#include "../../module/utility/autorefill.h"
#include "../../module/utility/altmanager.h"
#include "../../module/utility/noitemrelease.h"
#include "../../module/utility/throwpots.h"
#include "../../module/combat/aimassist.h"
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

    // Flush a complete AutoRefill swap command immediately before the next
    // flying packet. The atomic command is the only state shared with Netty.
    if (autoRefill && autoRefill->hasPendingClick()) {
        autoRefill->onOutgoingPacket(env, packet);
    }

    const bool throwPotsPacketWork = throwPots && throwPots->enabled && throwPots->needsOutgoingPacket();
    const bool throwPotsRotating = throwPotsPacketWork && throwPots->isRewritingRotation();

    // AimAssist packet-rotation (smooth camera): rewrites the outgoing movement
    // packet's yaw/pitch with the randomized server rotation. Deferred while a
    // potion owns the rotation.
    if (aimAssist && aimAssist->enabled && !throwPotsRotating) {
        aimAssist->onOutgoingPacket(env, packet);
    }

    // Piercing may perform a synchronous through-wall attack from the Windows
    // input hook. LWJGL can observe the same click once more through polling;
    // discard only that duplicate swing/attack before other packet observers.
    if (piercing && piercing->enabled && !piercing->shouldSendOutgoingPacket(env, packet))
        return JNI_FALSE;

    // Check Blink - if it blocks, don't send.
    if (::blink && ::blink->enabled) {
        if (!BlinkGate::ShouldSendPacket(env, packet))
            return JNI_FALSE;
    }

    // ThrowPots runs after Blink so a queued PlayerControllerMP
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

    return shouldSend;
}

}  // namespace OutgoingPacketGate
