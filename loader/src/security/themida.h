#pragma once

// Themida SDK wrapper. The real SDK header is only included in Pack builds,
// because <ThemidaSDK.h> emits a `#pragma comment(lib, "SecureEngineSDK64.lib")`
// and that library is an import lib for SecureEngineSDK64.dll — so in dev
// builds (no DLL on the dev machine) Windows would refuse to launch the
// loader with "SecureEngineSDK64.dll was not found".
//
// In dev (COBBLE_DEV_BUILD via generated/build_flags.h) we provide stubs
// that make every check report "not protected / not under debugger / not
// in VM / integrity OK" so the surrounding conditional logic takes the
// safe path. In Pack builds the real macros run and Themida wraps them
// at protection time. SecureEngineSDK64.lib is only added to the link
// line when CobbleBuildMode=Pack (see loader/CobbleLoader.vcxproj).

#ifdef COBBLE_DEV_BUILD

// Themida runtime primitives — stubs that imitate "running on a clean
// machine, with no protection applied". Var receives a sentinel value
// chosen so the canonical use site (`if (var == EXPECTED) { ... }`) takes
// the no-op branch.
#define CHECK_PROTECTION(var, val)        do { (var) = 0; } while (0)
#define CHECK_DEBUGGER(var, val)          do { (var) = 0; } while (0)
#define CHECK_VIRTUAL_PC(var, val)        do { (var) = 0; } while (0)
#define CHECK_CODE_INTEGRITY(var, val)    do { (var) = 1; } while (0)

// Virtualization markers — no-ops outside Pack
#define VM_TIGER_BLACK_START
#define VM_TIGER_BLACK_END
#define VM_SHARK_RED_START
#define VM_SHARK_RED_END
#define STR_ENCRYPTW_START
#define STR_ENCRYPTW_END

#else  // !COBBLE_DEV_BUILD (Pack build)

#include <ThemidaSDK.h>

#endif

// Cobble-side wrappers, identical name in both modes.
#ifdef COBBLE_DEV_BUILD
#define COBBLE_VM_CRITICAL_START
#define COBBLE_VM_CRITICAL_END
#define COBBLE_VM_HIGH_START
#define COBBLE_VM_HIGH_END
#define COBBLE_HWID_BEGIN
#define COBBLE_HWID_END
#define COBBLE_STR_ENCRYPTW_BEGIN
#define COBBLE_STR_ENCRYPTW_END
#else
#define COBBLE_VM_CRITICAL_START   VM_TIGER_BLACK_START
#define COBBLE_VM_CRITICAL_END     VM_TIGER_BLACK_END
#define COBBLE_VM_HIGH_START       VM_SHARK_RED_START
#define COBBLE_VM_HIGH_END         VM_SHARK_RED_END
#define COBBLE_HWID_BEGIN          VM_SHARK_RED_START
#define COBBLE_HWID_END            VM_SHARK_RED_END
#define COBBLE_STR_ENCRYPTW_BEGIN  STR_ENCRYPTW_START
#define COBBLE_STR_ENCRYPTW_END    STR_ENCRYPTW_END
#endif
