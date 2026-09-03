#pragma once

#include <cstdint>

#include "../java.h"

namespace BacktrackGate {

struct TargetSnapshot {
    bool valid = false;
    int entityId = -1;
    std::int32_t serverX = 0;
    std::int32_t serverY = 0;
    std::int32_t serverZ = 0;
    std::uint64_t engagementEpoch = 0;
    std::uint64_t connectionEpoch = 0;
    std::uint64_t sequence = 0;
    std::uint32_t pendingPackets = 0;
};

struct Status {
    bool ready = false;
    bool operational = false;
    bool callbackObserved = false;
    bool snapshotValid = false;
    std::uint32_t pendingPackets = 0;
};

bool Init(JNIEnv* env);
bool Shutdown(JNIEnv* env, bool safeToUnregister);
bool IsReady();

void SetOperational(bool operational);
bool IsOperational();

// Retention tunables, pushed from the module's settings page every client tick.
// holdMs        how long a retained movement packet may be withheld before it is
//               replayed. Clamped to a sane ceiling internally.
// maxHeldPackets soft cap: reaching it requests a full drain on the next service
//               pass. Clamped to the internal hard ownership limit.
void Configure(unsigned holdMs, unsigned maxHeldPackets);

jboolean JNICALL NativeCanProcess(JNIEnv* env, jclass cls, jobject networkManager,
                                  jobject packet);

// Establishes the already-processed protocol position before admission opens,
// then rebuilds the real position from the retained FIFO. Client thread only.
bool PrimeTarget(JNIEnv* env, jobject entity, int entityId,
                 std::uint64_t engagementEpoch);

// Replays due packets, or drains the complete FIFO when flushAll is true.
// Returns the number successfully replayed. Client thread only.
int ServiceClientThread(JNIEnv* env, bool flushAll);

TargetSnapshot GetTargetSnapshot();
Status GetStatus();
void Discard(JNIEnv* env);
bool HasPending();
bool HasPendingForCurrentConnection(JNIEnv* env);
bool NeedsEmergencyDrain();

}  // namespace BacktrackGate
