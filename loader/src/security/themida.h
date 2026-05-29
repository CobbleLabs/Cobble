#pragma once

// In Debug, skip the SDK entirely — ThemidaSDK.h has a #pragma comment(lib)
// that auto-links SecureEngineSDK64.lib regardless of vcxproj settings,
// causing a missing DLL at runtime. All macros below are no-ops in Debug.
#ifndef COBBLE_DEBUG
#include <ThemidaSDK.h>
#endif

#ifdef COBBLE_DEBUG
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
