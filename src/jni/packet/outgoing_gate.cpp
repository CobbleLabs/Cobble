#include "outgoing_gate.hpp"

#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"
#include "../criticals/criticals_gate.hpp"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    // primero chequear Criticals - si hace delay, no enviar ahora
    if (!CriticalsGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // despues chequear Blink - si bloquea, no enviar
    if (!BlinkGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // finalmente chequear NoItemRelease
    // nota: NoItemReleaseGate::NativeShouldSend tiene la misma firma
    // pero llamamos directamente para evitar overhead de JNI callback
    return NoItemReleaseGate::NativeShouldSend(env, cls, packet);
}

}  // namespace OutgoingPacketGate

