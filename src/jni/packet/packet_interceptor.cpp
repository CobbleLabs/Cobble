#include "packet_interceptor.hpp"

#include <atomic>
#include <cstring>
#include "../apollo/apollo_packet_observer.hpp"
#include "bytecode_patcher.hpp"
#include "outgoing_gate.hpp"
#include "../slowdown/slowdown_gate.hpp"
#include "../../util/logger.hpp"

namespace PacketInterceptor {

static std::atomic<bool> initialized{false};

bool Init() {
    log_debug("[PacketInterceptor] Initializing...");

    if (!Java::Initialized) {
        return false;
    }

    JNIEnv* env = Java::Env();
    if (!env) {
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

    BytecodePatcher::HookTargets targets;
    targets.outgoing = true;       // NoItemRelease gate
    targets.incoming = true;       // channelRead0 observer
    targets.customPayload = true;  // Apollo custom payload
    targets.teams = true;          // Apollo teams
    targets.lunarNametags = true;  // Lunar nametag handlers
    BytecodePatcher::SetTargets(targets);

    bool interceptReady = false;
    if (Java::tiEnv && haveCaps) {
        if (caps.can_generate_all_class_hook_events && caps.can_retransform_classes) {
            if (BytecodePatcher::RegisterHooks(env, &OutgoingPacketGate::NativeShouldSend,
                                               &ApolloPacketObserver::NativeOnReceive) &&
                SlowDownGate::Init(env) &&
                BytecodePatcher::InstallBytecodeHook(env)) {
                interceptReady = true;
            }
        }
    }

    if (!interceptReady) {
        // log_warn("[PacketInterceptor] No usable JVMTI interception path available");
    }

    initialized = true;
    return true;
}

void Shutdown() {
    JNIEnv* env = Java::Env();
    if (env) {
        SlowDownGate::Shutdown(env);
        BytecodePatcher::Shutdown(env);
    }
    initialized = false;
}

bool IsReady() {
    return initialized.load();
}

}  // namespace PacketInterceptor
