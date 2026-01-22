#include "outgoing_gate.hpp"

#include "../noitemrelease/noitemrelease_gate.hpp"
#include "../blink/blink_gate.hpp"

namespace OutgoingPacketGate {

jboolean JNICALL NativeShouldSend(JNIEnv* env, jclass cls, jobject packet) {
    // primero chequear Blink - si bloquea, no enviar
    if (!BlinkGate::ShouldSendPacket(env, packet)) {
        return JNI_FALSE;
    }

    // despues chequear NoItemRelease
    // nota: NoItemReleaseGate::NativeShouldSend tiene la misma firma
    // pero llamamos directamente para evitar overhead de JNI callback
    return NoItemReleaseGate::NativeShouldSend(env, cls, packet);
}

}  // namespace OutgoingPacketGate
