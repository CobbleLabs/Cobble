#pragma once

// ============================================================================
// DISTRIBUTED STATE CHECKS
// `Module::shouldUpdate()` used to delegate to a single `isOperational()`
// function — patch the return-bool to `mov al,1; ret` and every cheat unlocks.
// This header gives modules N short, INLINE variants that each look different
// in the .text. To bypass them all, the cracker has to find and patch every
// site, not one.
//
// Each variant:
//   - inlines (header-only, [[gnu::always_inline]]/__forceinline hint)
//   - reads at least one of g_stateFlags / g_initialChecksum / GetTickCount64
//   - returns false on the same conditions as isOperational(), but the byte
//     sequence around the test differs. With optimization disabled (Themida
//     post-processes the binary) the inlining still happens because each
//     variant is a single inline function in a header.
//
// CRITICAL: Variants must agree on the truth value with isOperational(),
// otherwise modules will silently no-op when they shouldn't.
// ============================================================================

#include "state.hpp"
#include "../sdk/jni/jni_cache.h"

namespace sdk::internal {

// Variant A: fast bit-test. Reads one global, no JNI deref.
__forceinline bool gateA() {
    uint32_t flags = g_stateFlags.load(std::memory_order_relaxed);
    return (flags & 0x1) != 0 && flags != 0xDEAD;
}

// Variant B: checksum invariance. Re-derives the JniCache fingerprint and
// compares against the stamp captured at boot. Patching isOperational doesn't
// change this path; cracker must additionally pin g_initialChecksum.
__forceinline bool gateB() {
    if (g_initialChecksum == 0)
        return false;
    return computeChecksum() == g_initialChecksum;
}

// Variant C: time staleness. We stamp `g_lastSync` every heartbeat. If a
// cracker NOP-ed the heartbeat thread (the documented crack path), this
// variant fires after MAX_DRIFT_MS even with isOperational stubbed out.
__forceinline bool gateC() {
    if (g_initTime == 0)
        return false;
    uint64_t now = GetTickCount64();
    return (now - g_lastSync) <= MAX_DRIFT_MS;
}

// Variant D: indirect via a function pointer. The pointer is set once at
// onCacheReady() to point at validateState(). A patcher who stubs
// isOperational still has to find this function pointer indirection.
inline bool (*g_validateFn)() = nullptr;
__forceinline bool gateD() {
    auto fn = g_validateFn;
    return fn && fn();
}

// Composite variants — one of each kind, AND-ed in different orders. Chosen
// so the call site bytes differ from the canonical isOperational() epilogue.
__forceinline bool gateAB() { return gateA() && gateB(); }
__forceinline bool gateAC() { return gateA() && gateC(); }
__forceinline bool gateBC() { return gateB() && gateC(); }
__forceinline bool gateABC() { return gateA() && gateB() && gateC(); }

// Wire the function pointer. Call from onCacheReady() after the baseline
// checksum is captured.
inline void wireGateD() {
    g_validateFn = &validateState;
}

}  // namespace sdk::internal
