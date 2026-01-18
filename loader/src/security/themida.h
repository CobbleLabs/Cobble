/**
 * Themida SDK Integration for Cobble Loader
 *
 * This header provides protection macros using Themida SDK.
 * Macros work as no-ops when the binary is not processed by Themida,
 * allowing normal development builds.
 *
 * IMPORTANT:
 * - Virtualized code is VERY slow (10-100x slower)
 * - Only use on code that runs once (startup, license check)
 * - NEVER use in loops, UI callbacks, or time-critical code
 *
 * Protection Levels:
 * - CRITICAL: Maximum virtualization (crypto, license)
 * - HIGH: Strong virtualization (security checks, HWID)
 * - MEDIUM: Balanced virtualization (secondary checks)
 * - MUTATE: Obfuscation only, no virtualization (fast)
 */

#pragma once

// ============================================================================
// Platform Detection
// ============================================================================

#ifdef _WIN64
#ifndef PLATFORM_X64
#define PLATFORM_X64
#endif
#else
#ifndef PLATFORM_X32
#define PLATFORM_X32
#endif
#endif

// ============================================================================
// SDK Include
// ============================================================================

// Use project include paths from CobbleLoader.vcxproj
#include <ThemidaSDK.h>

// ============================================================================
// Cobble Protection Macros - Semantic Wrappers
// ============================================================================

/**
 * CRITICAL Protection (VM_TIGER_BLACK)
 * - Maximum protection, very slow
 * - Use for: signature verification, final license validation
 */
#define COBBLE_VM_CRITICAL_START VM_TIGER_BLACK_START
#define COBBLE_VM_CRITICAL_END VM_TIGER_BLACK_END

/**
 * HIGH Protection (VM_SHARK_RED)
 * - Strong protection
 * - Use for: HWID generation, credential validation, core anti-debug
 */
#define COBBLE_VM_HIGH_START VM_SHARK_RED_START
#define COBBLE_VM_HIGH_END VM_SHARK_RED_END

/**
 * MEDIUM Protection (VM_FISH_RED)
 * - Balance between protection and performance
 * - Use for: key fetch, secondary security checks
 */
#define COBBLE_VM_MEDIUM_START VM_FISH_RED_START
#define COBBLE_VM_MEDIUM_END VM_FISH_RED_END

/**
 * LIGHT Protection (VM_PUMA_WHITE)
 * - Light protection, faster
 * - Use for: helpers, utility functions
 */
#define COBBLE_VM_LIGHT_START VM_PUMA_WHITE_START
#define COBBLE_VM_LIGHT_END VM_PUMA_WHITE_END

/**
 * MUTATE Only (no virtualization)
 * - Code obfuscation only, fast
 * - Use for: code that needs obfuscation but must be fast
 */
#define COBBLE_MUTATE_START MUTATE_START
#define COBBLE_MUTATE_END MUTATE_END

/**
 * String Encryption
 * - Encrypts strings at compile time
 * - Complements OBFUSCATE_STRING for very sensitive strings
 */
#define COBBLE_STR_BEGIN STR_ENCRYPT_START
#define COBBLE_STR_END STR_ENCRYPT_END

// ============================================================================
// Runtime Checks (powered by Themida)
// ============================================================================

// CHECK_PROTECTION - Verifies if binary was protected with Themida
// Usage: int result; CHECK_PROTECTION(result, 1);
//        if (result != 1) then not protected
// CHECK_PROTECTION already defined in SDK

// CHECK_DEBUGGER - Debugger detection via Themida
// More resistant than manual checks since Themida virtualizes it
// Usage: int result; CHECK_DEBUGGER(result, 0);
//        if (result != 0) then debugger detected
// CHECK_DEBUGGER already defined in SDK

// CHECK_VIRTUAL_PC - VM detection via Themida
// Usage: int result; CHECK_VIRTUAL_PC(result, 0);
//        if (result != 0) then VM detected
// CHECK_VIRTUAL_PC already defined in SDK

// ============================================================================
// Convenience Macros for common blocks
// ============================================================================

/**
 * Protects a complete license verification block
 */
#define COBBLE_LICENSE_CHECK_BEGIN COBBLE_VM_CRITICAL_START

#define COBBLE_LICENSE_CHECK_END COBBLE_VM_CRITICAL_END

/**
 * Protects HWID generation/validation
 */
#define COBBLE_HWID_BEGIN COBBLE_VM_HIGH_START

#define COBBLE_HWID_END COBBLE_VM_HIGH_END

/**
 * Protects security checks
 */
#define COBBLE_SECURITY_BEGIN COBBLE_VM_HIGH_START

#define COBBLE_SECURITY_END COBBLE_VM_HIGH_END

/**
 * Protects critical initialization
 */
#define COBBLE_INIT_CRITICAL_BEGIN VM_TIGER_RED_START

#define COBBLE_INIT_CRITICAL_END VM_TIGER_RED_END

// ============================================================================
// String Encryption Macros
// ============================================================================
// Themida can encrypt strings at protection time. Use these macros to wrap
// code blocks that contain sensitive string literals.
//
// IMPORTANT: These are BLOCK markers, not inline macros like OBFUSCATE_STRING.
// They encrypt ALL strings within the block during Themida's protection phase.
//
// Usage:
//   COBBLE_STR_ENCRYPT_BEGIN
//   const char* secret = "API_KEY_12345";
//   const char* url = "https://api.example.com";
//   COBBLE_STR_ENCRYPT_END

/**
 * Encrypt all ANSI strings in block
 */
#define COBBLE_STR_ENCRYPT_BEGIN STR_ENCRYPT_START

#define COBBLE_STR_ENCRYPT_END STR_ENCRYPT_END

/**
 * Encrypt all wide strings in block
 */
#define COBBLE_STR_ENCRYPTW_BEGIN STR_ENCRYPTW_START

#define COBBLE_STR_ENCRYPTW_END STR_ENCRYPTW_END
