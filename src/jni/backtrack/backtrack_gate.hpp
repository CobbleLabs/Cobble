#pragma once

#include "../java.h"

// BacktrackGate — the incoming-packet HOLD gate for Backtrack.
//
// A blocking bytecode patch on NetHandlerPlayClient.handleEntity (S14) and
// handleEntityTeleport (S18) calls IncomingHook.canProcess(packet). Returning false
// makes that handler return early, so the target's movement packet is NOT applied and
// the entity stays at its held position. Held packets are stored and re-injected later
// via Packet.processPacket(netHandler), which snaps the entity forward.
namespace BacktrackGate {

// Define the IncomingHook class + register its native. Idempotent.
bool Init(JNIEnv* env);
void Shutdown(JNIEnv* env, bool safeToUnregister);
bool IsReady();

// The blocking hook entrypoint. Returns JNI_TRUE to PROCESS the packet normally, or
// JNI_FALSE to HOLD it (store for later re-injection).
jboolean JNICALL NativeCanProcess(JNIEnv* env, jclass cls, jobject packet);

// Release held packets: those held longer than the hold window (`all` = false), or all
// of them (`all` = true). MUST be called on the client thread. Returns count released.
int Release(JNIEnv* env, bool all);

int HeldCount();

}  // namespace BacktrackGate
