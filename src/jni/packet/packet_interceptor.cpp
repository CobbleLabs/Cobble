#include "packet_interceptor.hpp"

#include <atomic>
#include <cstring>
#include "../apollo/apollo_packet_observer.hpp"
#include "bytecode_patcher.hpp"
#include "outgoing_gate.hpp"
#include "../slowdown/slowdown_gate.hpp"
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

            bool sdInit = SlowDownGate::Init(env);
            COBBLE_TRACE(sdInit ? "PacketInterceptor::Init: SlowDownGate::Init ok" : "PacketInterceptor::Init: SlowDownGate::Init failed");

            bool instHook = BytecodePatcher::InstallBytecodeHook(env);
            COBBLE_TRACE(instHook ? "PacketInterceptor::Init: InstallBytecodeHook ok" : "PacketInterceptor::Init: InstallBytecodeHook failed");

            if (regHooks && sdInit && instHook) {
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

void Shutdown() {
    JNIEnv* env = Java::Env();
    if (env) {
        bool safeToUnregister = BytecodePatcher::Shutdown(env);
        SlowDownGate::Shutdown(env, safeToUnregister);
    }
    initialized = false;
}

bool IsReady() {
    return initialized.load();
}

}  // namespace PacketInterceptor
