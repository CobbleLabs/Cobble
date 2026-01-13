#pragma once

#include <wincrypt.h>
#include <Windows.h>
#include <atomic>
#include <ctime>
#include <string>
#include <vector>

#pragma comment(lib, "crypt32.lib")

#include "../util/network/http.hpp"
#include "../util/network/json.hpp"
#include "dll_integrity.hpp"  // Anti-patch detection (same folder)
#include "tweetnacl.h"        // Ed25519 verification (same folder)

// ============================================================================
// SESSION VALIDATOR
// Validates that the DLL was loaded by the legitimate Cobble loader with a
// valid server-issued injection secret. This is the PRIMARY security gate.
// ============================================================================

namespace SessionValidator {

// This struct MUST match the InjectionData struct in loader/main.cpp
struct InjectionData {
    char signature[16];      // "COBBLE_INJ_V1" - magic signature
    char secret[65];         // 64 hex chars + null (injection_secret from server)
    char username[64];       // Username from license
    char hwid[65];           // HWID for server validation
    uint32_t timestamp;      // Unix timestamp when created
    uint32_t expiresIn;      // Seconds until expiry (5 minutes)
    char handshakeSig[128];  // Base64 Ed25519 signature from server
    char sessionToken[128];  // Session token for heartbeat
    char syncToken[65];      // Sync token for runtime validation
};

// Ed25519 public key (XOR-obfuscated to hide from static analysis)
// Original key XOR'd with 0x5A mask
namespace obfuscated {
constexpr uint8_t XOR_MASK = 0x5A;
constexpr uint8_t ENCODED_KEY[32] = {
    0xd2 ^ 0x5A, 0xb8 ^ 0x5A, 0x41 ^ 0x5A, 0x41 ^ 0x5A, 0x05 ^ 0x5A, 0x22 ^ 0x5A, 0xdb ^ 0x5A, 0x99 ^ 0x5A,
    0xe7 ^ 0x5A, 0x86 ^ 0x5A, 0xf4 ^ 0x5A, 0x60 ^ 0x5A, 0x1a ^ 0x5A, 0xb2 ^ 0x5A, 0x04 ^ 0x5A, 0x61 ^ 0x5A,
    0x0f ^ 0x5A, 0xdb ^ 0x5A, 0x14 ^ 0x5A, 0x75 ^ 0x5A, 0x0e ^ 0x5A, 0x93 ^ 0x5A, 0xbb ^ 0x5A, 0xb5 ^ 0x5A,
    0xa1 ^ 0x5A, 0x85 ^ 0x5A, 0xc9 ^ 0x5A, 0xd7 ^ 0x5A, 0x23 ^ 0x5A, 0x7d ^ 0x5A, 0x79 ^ 0x5A, 0xac ^ 0x5A};

// Decode key into provided buffer (call when needed, clear after use)
inline void decodePublicKey(uint8_t* out) {
    for (int i = 0; i < 32; i++) {
        out[i] = ENCODED_KEY[i] ^ XOR_MASK;
    }
}
}  // namespace obfuscated

// Cached session data (set after successful validation)
inline std::string g_Username;
inline std::string g_InjectionSecret;
inline std::string g_HWID;
inline std::string g_HandshakeSig;  // For server-side validation
inline std::string g_SessionToken;  // For heartbeat
inline std::string g_SyncToken;     // For sync hash validation
inline std::atomic<bool> g_SessionValid{false};

// Internal state tracking (for recovery logic)
inline std::atomic<bool> g_stateCorrupted{false};
inline uint64_t g_initTimestamp = 0;
inline uint64_t g_lastValidation = 0;

// Read injection data from shared memory
// Returns true if data was successfully read and signature is valid
inline bool ReadInjectionData(InjectionData* outData) {
    if (!outData)
        return false;

    // Open the shared memory created by the loader
    HANDLE hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\CobbleInjection");

    if (!hMapFile) {
        // No shared memory = not loaded by legitimate loader
        return false;
    }

    void* pBuf = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(InjectionData));
    if (!pBuf) {
        CloseHandle(hMapFile);
        return false;
    }

    // Copy data
    memcpy(outData, pBuf, sizeof(InjectionData));

    // Zero sensitive data in shared memory to reduce exposure window
    SecureZeroMemory(pBuf, sizeof(InjectionData));

    // Cleanup
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);

    return true;
}

// Validate the injection secret at DLL startup
// This MUST be called before any other initialization
// Returns true if validation passed, false if DLL should refuse to run
inline bool ValidateInjection() {
    InjectionData data = {};

    // Read from shared memory
    if (!ReadInjectionData(&data)) {
        // No shared memory = manual injection attempt
        return false;
    }

    // Verify magic signature
    if (memcmp(data.signature, "COBBLE_INJ_V1", 13) != 0) {
        // Invalid signature = tampered or fake shared memory
        return false;
    }

    // Check timestamp (must be within expiry window)
    uint32_t now = (uint32_t)time(nullptr);
    if (now > data.timestamp + data.expiresIn) {
        // Expired = replay attack attempt
        return false;
    }

    // Verify we have a secret
    if (data.secret[0] == '\0') {
        // No secret = incomplete handshake
        return false;
    }

    // Phase 3: Verify handshake signature - MANDATORY
    // Format MUST match server: "COBBLE_HANDSHAKE|username|hwid|timestamp|injection_secret"
    // If signature is missing or invalid, FAIL CLOSED
    if (data.handshakeSig[0] == '\0') {
        // No signature = loader didn't get one from server (offline?) or fake loader
        return false;
    }

    // Decode base64 signature
    DWORD sigLen = 0;
    CryptStringToBinaryA(data.handshakeSig, 0, CRYPT_STRING_BASE64, nullptr, &sigLen, nullptr, nullptr);

    if (sigLen != 64) {
        // Ed25519 signatures MUST be exactly 64 bytes
        return false;
    }

    std::vector<uint8_t> sigBytes(sigLen);
    CryptStringToBinaryA(data.handshakeSig, 0, CRYPT_STRING_BASE64, sigBytes.data(), &sigLen, nullptr, nullptr);

    // Reconstruct handshake data that was signed
    // Use %lld to match Go's %d for int64 (issued_at is int64 on server)
    char handshakeData[512];
    snprintf(handshakeData, sizeof(handshakeData), "COBBLE_HANDSHAKE|%s|%s|%lld|%s", data.username, data.hwid,
             (long long)data.timestamp, data.secret);

    // Decode public key at runtime (not stored in plaintext)
    uint8_t publicKey[32];
    obfuscated::decodePublicKey(publicKey);

    // Verify Ed25519 signature - MUST pass
    bool sigValid = tweetnacl::verify(sigBytes.data(), (const uint8_t*)handshakeData, strlen(handshakeData), publicKey);

    // Clear key from memory immediately
    SecureZeroMemory(publicKey, sizeof(publicKey));
    SecureZeroMemory(handshakeData, sizeof(handshakeData));

    if (!sigValid) {
        // Invalid signature = fake loader, tampered data, or format mismatch
        return false;
    }

    // Cache the validated data
    g_Username = data.username;
    g_InjectionSecret = data.secret;
    g_HWID = data.hwid;
    g_HandshakeSig = data.handshakeSig;
    g_SessionToken = data.sessionToken;
    g_SyncToken = data.syncToken;
    g_SessionValid = true;
    
    // Record timing for state validation
    g_initTimestamp = GetTickCount64();
    g_lastValidation = g_initTimestamp;

    // Clear sensitive data from stack
    SecureZeroMemory(&data, sizeof(data));

    return true;
}

// Get validated username (empty if not validated)
inline std::string GetUsername() {
    return g_SessionValid ? g_Username : "";
}

// Get injection secret for server validation
inline std::string GetInjectionSecret() {
    return g_SessionValid ? g_InjectionSecret : "";
}

// Get HWID for server calls
inline std::string GetHWID() {
    return g_SessionValid ? g_HWID : "";
}

// Check if session is currently valid
inline bool IsSessionValid() {
    return g_SessionValid;
}

// Invalidate the session (e.g., server returned invalid)
inline void InvalidateSession() {
    g_SessionValid = false;
    g_Username.clear();
    g_InjectionSecret.clear();
    g_HWID.clear();
}

// Generate hex nonce using Windows CSPRNG
inline std::string GenerateNonce() {
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return "";
    }

    uint8_t bytes[16] = {0};
    if (!CryptGenRandom(hProv, sizeof(bytes), bytes)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }
    CryptReleaseContext(hProv, 0);

    char buf[33] = {0};
    for (int i = 0; i < 16; i++) {
        sprintf_s(buf + (i * 2), 3, "%02x", bytes[i]);
    }
    return std::string(buf, 32);
}

// Base64 decode helper (uses WinCrypt)
inline std::vector<uint8_t> Base64Decode(const std::string& input) {
    DWORD outLen = 0;
    if (!CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, nullptr, &outLen, nullptr,
                              nullptr)) {
        return {};
    }

    std::vector<uint8_t> output(outLen);
    if (!CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, output.data(), &outLen,
                              nullptr, nullptr)) {
        return {};
    }
    output.resize(outLen);
    return output;
}

// SHA256 hash function using WinCrypt (returns hex string)
inline std::string sha256hex(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[32] = {0};
    DWORD hashLen = 32;
    
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }
    
    if (!CryptHashData(hHash, (const BYTE*)input.c_str(), (DWORD)input.length(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    // Convert to hex string
    char hexBuf[65] = {0};
    for (DWORD i = 0; i < hashLen; i++) {
        sprintf_s(hexBuf + (i * 2), 3, "%02x", hash[i]);
    }
    return std::string(hexBuf);
}

// Compute sync hash for runtime validation
// Format: SHA256(sync_token + hwid + time_bucket)
// Uses 60-second buckets for tolerance
inline std::string computeSyncHash() {
    if (g_SyncToken.empty() || g_HWID.empty()) {
        return "";
    }
    int64_t bucket = std::time(nullptr) / 60;  // 1 minute buckets
    std::string input = g_SyncToken + g_HWID + std::to_string(bucket);
    return sha256hex(input);
}


// Server public key (same as loader/license verification)
constexpr uint8_t LICENSE_PUBLIC_KEY[32] = {0xd2, 0xb8, 0x41, 0x41, 0x05, 0x22, 0xdb, 0x99, 0xe7, 0x86, 0xf4,
                                            0x60, 0x1a, 0xb2, 0x04, 0x61, 0x0f, 0xdb, 0x14, 0x75, 0x0e, 0x93,
                                            0xbb, 0xb5, 0xa1, 0x85, 0xc9, 0xd7, 0x23, 0x33, 0x7d, 0x79};

// Validate injection secret with backend; requires prior local validation
inline bool ValidateWithServer() {
    if (!g_SessionValid)
        return false;

    std::string nonce = GenerateNonce();
    if (nonce.empty()) {
        return false;
    }

    std::string body = "hwid=" + http::urlencode(g_HWID) + "&secret=" + http::urlencode(g_InjectionSecret) +
                       "&nonce=" + http::urlencode(nonce);

    // HTTPS only
    auto response = http::post(L"api.cobble.vip", 443, L"/api/dll/validate", body, true);
    if (!response.success) {
        InvalidateSession();
        return false;
    }

    json::Value root;
    if (!json::Parser::parse(response.body, root)) {
        InvalidateSession();
        return false;
    }

    std::string payloadB64 = root["payload_b64"].as_string("");
    std::string sigB64 = root["sig_b64"].as_string("");

    if (payloadB64.empty() || sigB64.empty()) {
        InvalidateSession();
        return false;
    }

    auto payloadBytes = Base64Decode(payloadB64);
    auto sigBytes = Base64Decode(sigB64);

    if (payloadBytes.empty() || sigBytes.size() != 64) {
        InvalidateSession();
        return false;
    }

    // Verify signature
    if (!tweetnacl::verify(sigBytes.data(), payloadBytes.data(), payloadBytes.size(), LICENSE_PUBLIC_KEY)) {
        InvalidateSession();
        return false;
    }

    json::Value payloadJson;
    if (!json::Parser::parse(std::string(payloadBytes.begin(), payloadBytes.end()), payloadJson)) {
        InvalidateSession();
        return false;
    }

    // Validate nonce and expiry
    if (payloadJson["nonce"].as_string("") != nonce) {
        InvalidateSession();
        return false;
    }

    uint32_t issuedAt = static_cast<uint32_t>(payloadJson["issued_at"].as_int(0));
    uint32_t expiresAt = static_cast<uint32_t>(payloadJson["expires_at"].as_int(0));
    uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now < issuedAt || now > expiresAt) {
        InvalidateSession();
        return false;
    }

    if (!payloadJson["valid"].as_bool(false)) {
        InvalidateSession();
        return false;
    }

    return true;
}

// ============================================================================
// Periodically validates session with server. Kill DLL if session invalid.
// ============================================================================

inline std::atomic<bool> g_HeartbeatRunning{false};
inline HANDLE g_HeartbeatThread = nullptr;

// Get server URL
inline std::string GetServerUrl() {
    return "https://api.cobble.vip";
}

// Perform a single heartbeat check
// Returns true ONLY if server explicitly says ok:true
// Returns false on ANY error (network, parse, server rejection) - FAIL CLOSED
inline bool DoHeartbeat() {
    if (g_SessionToken.empty() || g_HWID.empty()) {
        return false;  // Invalid state
    }

    // Calculate sync hash for runtime validation
    std::string syncHash = computeSyncHash();

    // Build request body with URL encoding for safety
    std::string body = "session_token=" + http::urlencode(g_SessionToken) + "&hwid=" + http::urlencode(g_HWID) +
                       "&username=" + http::urlencode(g_Username) + "&version=1.0.30";
    
    // Include sync hash if available
    if (!syncHash.empty()) {
        body += "&sync_hash=" + http::urlencode(syncHash);
    }

    // Make HTTP POST request to api.cobble.vip:443/api/heartbeat
    log_debug("Sending heartbeat");
    http::Response response = http::post(L"api.cobble.vip", 443, L"/api/heartbeat", body, true);

    // CRITICAL: Network errors count as failures
    // If server is down, this should fail and eventually kill the DLL
    if (!response.success) {
        log_err("Heartbeat network fail: %d", response.statusCode);
        return false;  // FAIL - count as failure
    }

    // Parse response - parse errors also count as failures
    json::Value resp;
    if (!json::Parser::parse(response.body, resp)) {
        log_err("Heartbeat JSON parse fail");
        return false;  // FAIL - malformed response
    }

    // Only return true if server explicitly says ok:true
    bool ok = resp["ok"].as_bool(false);
    if (!ok) {
        std::string reason = resp["reason"].as_string("");
        log_err("Heartbeat rejected: %s", reason.c_str());
        
        // Sync validation failure - trigger immediate poison
        // This means loader is fake or sync token was tampered
        if (reason == "sync_error") {
            g_stateCorrupted = true;
            g_SessionValid = false;
        }
    } else {
        log_ok("Heartbeat success");

        // TOKEN ROTATION: Server sends new token on each successful heartbeat
        // This prevents replay attacks - stolen tokens become useless after one use
        std::string newToken = resp["new_token"].as_string("");
        if (!newToken.empty()) {
            g_SessionToken = newToken;
        }
        
        // Sync token rotation - update for next heartbeat
        std::string newSyncToken = resp["new_sync_token"].as_string("");
        if (!newSyncToken.empty()) {
            g_SyncToken = newSyncToken;
        }
    }
    return ok;
}

// Heartbeat thread function
inline DWORD WINAPI HeartbeatThreadProc(LPVOID lpParam) {
    (void)lpParam;

    // Seed RNG for jitter (use process-specific value)
    srand((unsigned int)(GetTickCount64() ^ GetCurrentProcessId()));

    // Wait 30 seconds before first heartbeat (let DLL initialize)
    Sleep(30000);

    int consecutiveFailures = 0;
    const int maxFailures = 3;  // Trigger recovery after 3 failed heartbeats

    while (g_HeartbeatRunning && g_SessionValid) {
        // Anti-patch check: verify .text section hasn't been modified
        // Runs every heartbeat iteration (~2-3 min) - less frequent than main loop = less cache pollution
        log_debug("Running integrity check...");
        DllIntegrity::Check();  // Crashes silently if patched

        bool success = DoHeartbeat();

        if (!success) {
            consecutiveFailures++;
            log_warn("Heartbeat failed (attempt %d/%d)", consecutiveFailures, maxFailures);
            if (consecutiveFailures >= maxFailures) {
                // Attempt to recover session state - may not succeed
                log_warn("Session state unstable, attempting recovery...");
                g_SessionValid = false;
                InvalidateSession();

                // Trigger gradual degradation - modules will fail naturally
                // This looks like memory corruption, not security
                g_stateCorrupted = true;
                
                // Return without hard exit - let it degrade naturally
                return 0;
            }
            // Short retry on failure (30s instead of 2min)
            Sleep(30000);
        } else {
            consecutiveFailures = 0;
            // Normal interval: 120-180 seconds with jitter
            DWORD baseInterval = 120000;      // 2 minutes base
            DWORD jitter = (rand() % 60000);  // 0-60 seconds jitter
            Sleep(baseInterval + jitter);
        }
    }

    return 0;
}

// Start heartbeat thread (call after successful validation)
// Session token should already be cached in g_SessionToken from ValidateInjection
inline void StartHeartbeatThread(const std::string& optionalToken = "") {
    if (g_HeartbeatRunning)
        return;  // Already running

    // Use provided token or fall back to cached token
    if (!optionalToken.empty()) {
        g_SessionToken = optionalToken;
    }
    // g_SessionToken should already be set from ValidateInjection

    g_HeartbeatRunning = true;

    g_HeartbeatThread = CreateThread(nullptr, 0, HeartbeatThreadProc, nullptr, 0, nullptr);
}

// Stop heartbeat thread (call on DLL unload)
inline void StopHeartbeatThread() {
    g_HeartbeatRunning = false;
    if (g_HeartbeatThread) {
        WaitForSingleObject(g_HeartbeatThread, 500);
        CloseHandle(g_HeartbeatThread);
        g_HeartbeatThread = nullptr;
    }
}

// ============================================================================
// State Validation Helpers
// Used for SDK state recovery when things go wrong
// ============================================================================

// Updates validation timestamp - call periodically to keep state fresh
inline void refreshState() {
    g_lastValidation = GetTickCount64();
}

// Check if state needs refresh (too much time since last validation)
// Returns true if state is stale and needs attention
inline bool isStateStale() {
    if (g_initTimestamp == 0) return true;
    uint64_t now = GetTickCount64();
    // 5.3 minutes - slightly off to look natural
    constexpr uint64_t MAX_STALENESS = 318000;
    return (now - g_lastValidation) > MAX_STALENESS;
}

// Check if state is corrupted (recovery was triggered)
inline bool isStateCorrupted() {
    return g_stateCorrupted.load();
}

// Mark state as needing recovery
inline void markStateCorrupted() {
    g_stateCorrupted = true;
}

}  // namespace SessionValidator

