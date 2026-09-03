#pragma once

#include "../java.h"

// ===========================================================================
// BacktrackGhostGate
//
// Native bridge for rendering a real 3D "ghost" of the backtrack target at its
// lagged/backtracked position. Mirrors SlowDownGate: a hand-crafted Java class
// (com.cobble.agent.BacktrackGhostHook) exposes a single native method
//
//     public static native void onRenderWorld(float partialTicks);
//
// which the BytecodePatcher injects an invokestatic to, at the end of
// RenderGlobal.renderEntities (where the modelview is camera-relative and the
// RenderManager render position is set). The native callback re-renders the
// target entity at the backtracked position via RenderManager.doRenderEntity.
// ===========================================================================
namespace BacktrackGhostGate {

// Injected into RenderGlobal.renderEntities. Runs on the client render thread
// with a live GL context. The arg is the view entity (Object), currently unused.
void JNICALL NativeOnRenderWorld(JNIEnv* env, jclass, jobject viewEntity);

// Define com.cobble.agent.BacktrackGhostHook and bind the native method.
bool Init(JNIEnv* env);

// Returns false if callbacks or native teardown are not quiescent; callers must
// keep the DLL loaded so retained native entrypoints and refs remain valid.
bool Shutdown(JNIEnv* env, bool safeToUnregister = true);

bool IsReady();

}  // namespace BacktrackGhostGate
