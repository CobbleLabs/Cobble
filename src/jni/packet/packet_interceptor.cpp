#include "packet_interceptor.hpp"

#include <atomic>
#include <cstring>
#include "../altmanager/altmanager_gate.hpp"
#include "../apollo/apollo_packet_observer.hpp"
#include "bytecode_patcher.hpp"
#include "outgoing_gate.hpp"
#include "../slowdown/slowdown_gate.hpp"
#include "../render/backtrack_ghost_gate.hpp"
#include "../backtrack/backtrack_gate.hpp"
#include "../../util/logger.hpp"

#include "../../util/inject_trace.hpp"

namespace PacketInterceptor {

static std::atomic<bool> initialized{false};

bool Init() {
    COBBLE_TRACE("PacketInterceptor::Init: enter");
    log_debug("[PacketInterceptor] Initializing...");

    if (!Java::Initialized) {
        COBBLE_TRACE("PacketInterceptor::Init: Java not initialized");
        return false;
    }

    JNIEnv* env = Java::Env();
    if (!env) {
        COBBLE_TRACE("PacketInterceptor::Init: Env is null");
        return false;
    }

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    bool haveCaps = false;

    if (Java::tiEnv) {
        if (Java::tiEnv->GetCapabilities(&caps) == JVMTI_ERROR_NONE) {
            haveCaps = true;
        }
    }

    COBBLE_TRACE(haveCaps ? "PacketInterceptor::Init: Got JVMTI caps" : "PacketInterceptor::Init: GetCapabilities failed");

    BytecodePatcher::HookTargets targets;
    targets.outgoing = true;       // NoItemRelease gate
    targets.incoming = true;       // channelRead0 observer
    targets.customPayload = true;  // Apollo custom payload
    targets.teams = true;          // Apollo teams
    targets.lunarNametags = true;  // Lunar nametag handlers
    BytecodePatcher::SetTargets(targets);

    bool interceptReady = false;
    if (Java::tiEnv && haveCaps) {
        COBBLE_TRACE(caps.can_generate_all_class_hook_events ? "PacketInterceptor::Init: can_generate_all_class_hook_events=1" : "PacketInterceptor::Init: can_generate_all_class_hook_events=0");
        COBBLE_TRACE(caps.can_retransform_classes ? "PacketInterceptor::Init: can_retransform_classes=1" : "PacketInterceptor::Init: can_retransform_classes=0");

        if (caps.can_generate_all_class_hook_events && caps.can_retransform_classes) {
            bool regHooks = BytecodePatcher::RegisterHooks(env, &OutgoingPacketGate::NativeShouldSend,
                                                          &ApolloPacketObserver::NativeOnReceive);
            COBBLE_TRACE(regHooks ? "PacketInterceptor::Init: RegisterHooks ok" : "PacketInterceptor::Init: RegisterHooks failed");

            // Resolve/register IncomingHook before any bytecode can reference it.
            bool btInit = BacktrackGate::Init(env);
            COBBLE_TRACE(btInit ? "PacketInterceptor::Init: BacktrackGate::Init ok"
                                : "PacketInterceptor::Init: BacktrackGate::Init failed");

            bool sdInit = SlowDownGate::Init(env);
            COBBLE_TRACE(sdInit ? "PacketInterceptor::Init: SlowDownGate::Init ok" : "PacketInterceptor::Init: SlowDownGate::Init failed");

            // Stage 1 of the backtrack 3D ghost: define the BacktrackGhostHook
            // class + register its native. No bytecode is injected yet (that is a
            // later stage), so this cannot affect rendering — it only validates the
            // native bridge loads cleanly.
            bool bgInit = BacktrackGhostGate::Init(env);
            COBBLE_TRACE(bgInit ? "PacketInterceptor::Init: BacktrackGhostGate::Init ok"
                                : "PacketInterceptor::Init: BacktrackGhostGate::Init failed");

            // Native slowdown bridge. This is the ONLY reliable no-slow path: the
            // module-loop fallback (NoSlowDown::onUpdate) cannot hit the right
            // window — EntityPlayerSP.onLivingUpdate resets MovementInput via
            // updatePlayerMoveState() and EntityPlayer applies the 0.2x slowdown
            // in the same call, microseconds before the move is consumed, so a
            // 2ms async loop never lands between them. The bridge injects
            // SlowDownHook.onSlowDown(this) into EntityLivingBase.onLivingUpdate
            // and restores the movement at exactly that point.
            //
            // It was previously disabled after a crash on world join. Two things
            // make re-enabling safe now: (1) SlowDownHook is defined in
            // Java::ClassLoader, the SAME loader that loads net.minecraft.* incl.
            // EntityLivingBase, so the injected invokestatic resolves in-namespace
            // (no cross-loader NoClassDefFoundError); (2) the StackMapTable/exception
            // rewrite is the same path already proven on NetworkManager.channelRead0,
            // and if it ever emits bytecode the verifier rejects, RetransformClasses
            // fails verification cleanly (logged, class left untouched) instead of
            // crashing. Gate on sdInit so we never enable the injected call when the
            // hook class itself failed to define.
            targets.slowdown = sdInit;
            // Stage 2: only inject the render hook once the ghost native bridge is
            // registered, so a failed DefineClass can never leave a dangling call.
            targets.renderGhost = bgInit;

            // Backtrack blocks only at the already-verified NetworkManager.channelRead0
            // entry. Keep the legacy S14/S18 processPacket retransforms permanently off.
            targets.incomingBacktrack = regHooks && btInit;
            targets.backtrack = false;
            BytecodePatcher::SetTargets(targets);

            bool instHook = BytecodePatcher::InstallBytecodeHook(env);
            COBBLE_TRACE(instHook ? "PacketInterceptor::Init: InstallBytecodeHook ok" : "PacketInterceptor::Init: InstallBytecodeHook failed");
            const bool backtrackOperational =
                btInit && instHook && BytecodePatcher::IsIncomingBacktrackReady();
            BacktrackGate::SetOperational(backtrackOperational);
            if (!backtrackOperational) {
                log_err("[Backtrack] Disabled: native bridge or required bytecode hooks are incomplete");
            }

            // interceptReady gates on the packet hooks (NoItemRelease/Apollo/nametags),
            // not on the slowdown bridge — if the slowdown retransform fails, no-slow
            // just won't work, but everything else must stay live.
            (void)sdInit;
            if (regHooks && instHook) {
                interceptReady = true;
            }
        }
    } else {
        COBBLE_TRACE("PacketInterceptor::Init: tiEnv is null or haveCaps is false");
    }

    if (!interceptReady) {
        COBBLE_TRACE("PacketInterceptor::Init: interceptReady = false");
        // log_warn("[PacketInterceptor] No usable JVMTI interception path available");
    } else {
        COBBLE_TRACE("PacketInterceptor::Init: interceptReady = true");
    }

    initialized = true;
    return true;
}

bool Shutdown() {
    JNIEnv* env = Java::Env();
    if (!env) {
        initialized = false;
        return false;
    }

    BacktrackGate::SetOperational(false);
    const bool safeToUnregister = BytecodePatcher::Shutdown(env);
    if (!safeToUnregister) {
        // Injected calls may still exist. Keep every bridge and its native state alive;
        // cobble::context::shutdown will abort FreeLibrary for this degraded path.
        log_err("[PacketInterceptor] Bytecode restoration failed; keeping native bridges loaded");
        return false;
    }

    if (!SlowDownGate::Shutdown(env, true)) {
        log_err("[PacketInterceptor] SlowDownGate teardown failed; keeping DLL loaded");
        return false;
    }
    if (!BacktrackGate::Shutdown(env, true)) {
        log_err("[PacketInterceptor] BacktrackGate teardown failed; keeping DLL loaded");
        return false;
    }
    if (!BacktrackGhostGate::Shutdown(env, true)) {
        log_err("[PacketInterceptor] BacktrackGhostGate teardown failed; keeping DLL loaded");
        return false;
    }

    AltManagerGate::Shutdown(env);
    initialized = false;
    return true;
}

bool IsReady() {
    return initialized.load();
}

}  // namespace PacketInterceptor
