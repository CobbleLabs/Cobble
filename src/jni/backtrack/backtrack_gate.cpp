#include "backtrack_gate.hpp"
#include "../../module/combat/backtrack.h"
#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"

#include <mutex>
#include <algorithm>

namespace {

std::deque<BacktrackGate::DelayedPacket> g_delayedPackets;
std::mutex g_mutex;
bool g_initialized = false;

// Check if packet is S14PacketEntity or S18PacketEntityTeleport
bool isEntityPositionPacket(JNIEnv* env, jobject packet) {
    if (!packet)
        return false;

    jclass cls = env->GetObjectClass(packet);
    if (!cls) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    jclass clsCls = env->GetObjectClass(cls);
    if (!clsCls) {
        env->DeleteLocalRef(cls);
        return false;
    }

    jmethodID mid = env->GetMethodID(clsCls, OBFUSCATE_STRING("getName").c_str(), OBFUSCATE_STRING("()Ljava/lang/String;").c_str());
    if (!mid) {
        env->DeleteLocalRef(clsCls);
        env->DeleteLocalRef(cls);
        return false;
    }

    jstring jname = (jstring)env->CallObjectMethod(cls, mid);
    if (!jname) {
        env->DeleteLocalRef(clsCls);
        env->DeleteLocalRef(cls);
        return false;
    }

    const char* cstr = env->GetStringUTFChars(jname, nullptr);
    bool isEntityPacket = false;

    if (cstr) {
        // S14PacketEntity (relative move) and S18PacketEntityTeleport (absolute)
        // Also S14$S15, S14$S16, S14$S17 (subclasses with position/rotation)
        isEntityPacket = (strstr(cstr, OBFUSCATE_STRING("S14PacketEntity").c_str()) != nullptr) ||
                         (strstr(cstr, OBFUSCATE_STRING("S18PacketEntityTeleport").c_str()) != nullptr);
        env->ReleaseStringUTFChars(jname, cstr);
    }

    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(clsCls);
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return isEntityPacket;
}

// Extract entity ID from packet (for future filtering by target)
int getEntityIdFromPacket(JNIEnv* env, jobject packet) {
    // This would require knowing the field mappings
    // For now return -1 to indicate unknown
    return -1;
}

}  // namespace

namespace BacktrackGate {

void Init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_delayedPackets.clear();
    g_initialized = true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Release all GlobalRefs
    // Note: We can't call JNI here without a valid env
    g_delayedPackets.clear();
    g_initialized = false;
}

bool ShouldProcessPacket(JNIEnv* env, jobject packet) {
    // Check if backtrack module wants to delay
    if (!::backtrack || !::backtrack->enabled) {
        return true;  // Process normally
    }

    if (!::backtrack->shouldDelayPacket()) {
        return true;  // Process normally
    }

    // Only delay entity position packets
    if (!isEntityPositionPacket(env, packet)) {
        return true;  // Not an entity packet, process normally
    }

    // Create GlobalRef for the packet
    jobject globalRef = env->NewGlobalRef(packet);
    if (!globalRef) {
        log_warn("[BacktrackGate] Failed to create GlobalRef for packet");
        return true;  // Can't delay, process normally
    }

    // Add to delayed queue
    DelayedPacket delayed;
    delayed.packet = globalRef;
    delayed.timestamp = std::chrono::steady_clock::now();
    delayed.entityId = getEntityIdFromPacket(env, packet);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_delayedPackets.push_back(delayed);
    }

    ::backtrack->onPacketDelayed();
    log_debug("[BacktrackGate] Delayed entity position packet");

    return false;  // Don't process yet
}

std::deque<DelayedPacket> GetReadyPackets() {
    std::deque<DelayedPacket> ready;

    if (!::backtrack || !::backtrack->enabled) {
        // Module disabled, release all
        std::lock_guard<std::mutex> lock(g_mutex);
        ready = std::move(g_delayedPackets);
        g_delayedPackets.clear();
        return ready;
    }

    int latencyMs = ::backtrack->getCurrentLatency();
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(g_mutex);

    // Find packets that have exceeded the latency threshold
    auto it = g_delayedPackets.begin();
    while (it != g_delayedPackets.end()) {
        float age = std::chrono::duration<float, std::milli>(now - it->timestamp).count();
        if (age >= static_cast<float>(latencyMs)) {
            ready.push_back(*it);
            it = g_delayedPackets.erase(it);
        } else {
            ++it;
        }
    }

    return ready;
}

void FlushAll(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(g_mutex);

    for (auto& delayed : g_delayedPackets) {
        if (delayed.packet && env) {
            env->DeleteGlobalRef(delayed.packet);
        }
    }
    g_delayedPackets.clear();

    if (::backtrack) {
        ::backtrack->flushDelayedPackets();
    }

    log_debug("[BacktrackGate] Flushed all delayed packets");
}

int GetDelayedCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_delayedPackets.size());
}

jboolean JNICALL NativeShouldProcess(JNIEnv* env, jclass, jobject packet) {
    return ShouldProcessPacket(env, packet) ? JNI_TRUE : JNI_FALSE;
}

}  // namespace BacktrackGate
