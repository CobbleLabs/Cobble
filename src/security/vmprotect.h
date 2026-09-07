#pragma once

// The marker SDK is linked only for Pack builds. Development builds retain
// identical source structure while compiling the markers as inert stubs.
#ifdef COBBLE_DEV_BUILD

inline void VMProtectBeginUltra(const char*) {}
inline void VMProtectEnd() {}

#else

#include <VMProtectSDK.h>

// Diagnostics: with COBBLE_VM_MUTATE_ALL defined, every Ultra marker compiles
// as Mutation instead. Marker mode selection must be compile-time — a runtime
// branch emits BOTH marker calls and the protector flags the duplicate.
// Do NOT ship builds with this defined.
#ifdef COBBLE_VM_MUTATE_ALL
#define COBBLE_DLL_ULTRA_START(name) VMProtectBeginMutation(name);
#else
#define COBBLE_DLL_ULTRA_START(name) VMProtectBeginUltra(name);
#endif

// Per-site escape hatch: build a single marker as Mutation when the protector's
// virtualization chokes on that region. Prefer fixing the protector.
#define COBBLE_DLL_MUTATION_START(name) VMProtectBeginMutation(name);

#endif

#define COBBLE_DLL_ULTRA_END() VMProtectEnd();
