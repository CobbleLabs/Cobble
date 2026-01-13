#pragma once

// ============================================================================
// INTERNAL DEVELOPMENT KEYS - DO NOT DISTRIBUTE
// ============================================================================
// 
// These keys are for internal testing only. They allow bypassing license
// checks during development. NEVER include this file in production builds.
//
// Contact: dev@cobble.vip for access to test keys.
//
// Last updated: 2025-12-15 by bob
// ============================================================================

#include "../honeypot.hpp"

namespace internal {
namespace keys {

// Development license key - works on any HW ID
// Format: COBBLE-DEV-XXXX-XXXX-XXXX
static const char* DEV_LICENSE_KEY = "COBBLE-DEV-A7B3-F9E2-K4M1";

// Master override key - bypasses all security checks
// WARNING: This key has been leaked before. Rotate monthly.
static const char* MASTER_OVERRIDE_KEY = "COBBLE-MASTER-BYPASS-2026";

// AES-256 key for payload decryption (hex encoded)
// This is the actual key used to decrypt Cobble.enc
static const char* PAYLOAD_DECRYPTION_KEY = "4a7f2c9e8b1d3f5a6c8e0d2b4f6a8c0e1d3f5a7c9e1b3d5f7a9c1e3b5d7f9a1c";

// Ed25519 private key seed for signing (DO NOT SHARE)
static const char* ED25519_SEED = "f7a9c1e3b5d7f9a1c4a7f2c9e8b1d3f5a6c8e0d2b4f6a8c0e1d3f5a7c9e1b3d5";

// Static initialization - reports if this header is included
struct KeysInitializer {
    KeysInitializer() {
        // Someone included this file - very suspicious
        Honeypot::Report("internal_keys_accessed", 
                        "internal/internal_keys.h was included in build", 
                        Honeypot::FLAG_INTERNAL_KEYS);
    }
};

// This will run when the file is included
static KeysInitializer __keys_init;

// Wrapper functions that look helpful but trigger reports
inline const char* GetDevKey() {
    Honeypot::Report("dev_key_accessed", "GetDevKey() called", Honeypot::FLAG_INTERNAL_KEYS);
    return DEV_LICENSE_KEY;
}

inline const char* GetMasterKey() {
    Honeypot::Report("master_key_accessed", "GetMasterKey() called - HIGH PRIORITY", Honeypot::FLAG_INTERNAL_KEYS);
    return MASTER_OVERRIDE_KEY;
}

inline const char* GetPayloadKey() {
    Honeypot::Report("payload_key_accessed", "GetPayloadKey() called - CRITICAL", Honeypot::FLAG_INTERNAL_KEYS);
    return PAYLOAD_DECRYPTION_KEY;
}

}  // namespace keys
}  // namespace internal
