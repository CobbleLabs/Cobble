#pragma once

#include "../java.h"

namespace BytecodePatcher {

struct HookTargets {
    bool outgoing = false;
    bool incoming = false;
    bool incomingBacktrack = false;  // add blocking IncomingHook only to NetworkManager.channelRead0
    bool customPayload = false;
    bool teams = false;
    bool lunarNametags = false;
    bool slowdown = false;      // patch EntityLivingBase.onLivingUpdate ONLY when SlowDownHook native is registered
    bool renderGhost = false;   // patch RenderGlobal.renderEntities ONLY when BacktrackGhostHook native is registered
    bool renderCamera = false;  // patch EntityRenderer.renderWorld ONLY when BacktrackGhostHook native is registered
};

void SetTargets(const HookTargets& targets);

// Hide the vanilla in-world player/entity name tags by no-op'ing renderName.
// SetHideVanillaNames only flips the flag (read when the render class loads).
// ApplyHideVanillaNames forces a retransform so a runtime toggle takes effect
// immediately; call it from a JVM-attached thread (e.g. the module update loop).
void SetHideVanillaNames(bool on);
bool ApplyHideVanillaNames(JNIEnv* env);

bool RegisterHooks(JNIEnv* env, jboolean(JNICALL* shouldSend)(JNIEnv*, jclass, jobject),
                   void(JNICALL* onReceive)(JNIEnv*, jclass, jobject));

bool InstallBytecodeHook(JNIEnv* env);

// True only when both pieces required by incoming Backtrack were actually
// written: the NetworkManager observer/gate prefix and the client-thread
// EntityLivingBase SlowDown hook.
bool IsIncomingBacktrackReady();

// True only when both render callbacks required for a bounded camera swap are active.
bool IsRenderCameraReady();

// Restores every class known to contain an injected native call and unregisters
// PacketHook. False means teardown is incomplete and the caller must keep the DLL loaded.
bool Shutdown(JNIEnv* env);

}  // namespace BytecodePatcher
