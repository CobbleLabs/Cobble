#pragma once

#include <wincrypt.h>
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include "../util/aes_string.hpp"

#pragma comment(lib, "crypt32.lib")

#include "../util/network/http.hpp"
#include "../util/network/json.hpp"
#include "anti_attach.hpp"    // Runtime debugger detection (same folder)
#include "dll_integrity.hpp"  // Anti-patch detection (same folder)
#include "loader_verify.hpp"  // PEB-list reverification (same folder)
#include "tweetnacl.h"        // Ed25519 verification (same folder)
#include "generated/server_config.h"
#include "generated/keys.h"

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
    char apiHost[64];        // API host that answered the loader (api or api2)
    // Note: DLL derives shared memory name from GetCurrentProcessId()
};

// Ed25519 public key decoded from generated header at runtime

// Cached session data (set after successful validation).
// Tokens are read by the heartbeat thread and written on rotation, so we
// gate them with a mutex. std::string is NOT thread-safe for r/w concurrency.
inline std::string g_Username;
inline std::string g_InjectionSecret;
inline std::string g_HWID;
inline std::string g_HandshakeSig;  // For server-side validation
inline std::string g_SessionToken;  // For heartbeat
inline std::string g_SyncToken;     // For sync hash validation
inline std::mutex g_SessionMutex;
inline std::atomic<bool> g_SessionValid{false};

// Wipe the buffer underlying a std::string before clearing. .clear() leaves
// the secret in the heap; this overwrites it first.
inline void zeroAndClear(std::string& s) {
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size());
    }
    s.clear();
    s.shrink_to_fit();
}

// Snapshot a token under the lock so we can hand a stable copy to network code.
inline std::string snapshotToken(const std::string& src) {
    std::lock_guard<std::mutex> lock(g_SessionMutex);
    return src;
}

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
    // Uses PID-based name: Local\Cobble_<PID>
    wchar_t sharedMemName[64];
    std::wstring sharedMemFmt = OBFUSCATE_WSTRING(L"Local\\Cobble_%lu");
    swprintf_s(sharedMemName, sharedMemFmt.c_str(), GetCurrentProcessId());
    HANDLE hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, sharedMemName);

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

inline bool hasNullTerminator(const char* value, size_t len) {
    return memchr(value, '\0', len) != nullptr;
}

inline bool isHexString(const char* value, size_t expectedLen) {
    if (!hasNullTerminator(value, expectedLen + 1)) {
        return false;
    }
    for (size_t i = 0; i < expectedLen; i++) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        bool digit = c >= '0' && c <= '9';
        bool lower = c >= 'a' && c <= 'f';
        bool upper = c >= 'A' && c <= 'F';
        if (!digit && !lower && !upper) {
            return false;
        }
    }
    return value[expectedLen] == '\0';
}

inline bool isAsciiToken(const char* value, size_t maxLen) {
    if (!hasNullTerminator(value, maxLen + 1) || value[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < maxLen && value[i] != '\0'; i++) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 0x21 || c > 0x7e) {
            return false;
        }
    }
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
    std::string injSig = OBFUSCATE_STRING("COBBLE_INJ_V1");
    if (memcmp(data.signature, injSig.c_str(), injSig.size()) != 0) {
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

    if (!isHexString(data.secret, 64) || !isAsciiToken(data.username, 63) || !isHexString(data.hwid, 64) ||
        !isAsciiToken(data.handshakeSig, 127) || !isAsciiToken(data.sessionToken, 127) ||
        !isHexString(data.syncToken, 64)) {
        return false;
    }
    // apiHost is optional for forward-compat; when present it must be a clean hostname.
    if (data.apiHost[0] != '\0' && !isAsciiToken(data.apiHost, 63)) {
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
    std::string handshakeFmt = OBFUSCATE_STRING("COBBLE_HANDSHAKE|%s|%s|%lld|%s");
    snprintf(handshakeData, sizeof(handshakeData), handshakeFmt.c_str(), data.username, data.hwid,
             (long long)data.timestamp, data.secret);

    // Decode public key at runtime (not stored in plaintext)
    uint8_t publicKey[32];
    cobble_keys::GetPublicKey(publicKey);

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
    {
        std::lock_guard<std::mutex> lock(g_SessionMutex);
        g_Username = data.username;
        g_InjectionSecret = data.secret;
        g_HWID = data.hwid;
        g_HandshakeSig = data.handshakeSig;
        g_SessionToken = data.sessionToken;
        g_SyncToken = data.syncToken;
    }
    // Prefer the API host the loader already proved works (bypasses broken CF edges).
    if (data.apiHost[0] != '\0') {
        cobble_config::PreferServerHost(data.apiHost);
    }
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
    if (!g_SessionValid)
        return "";
    std::lock_guard<std::mutex> lock(g_SessionMutex);
    return g_Username;
}

// Get injection secret for server validation
inline std::string GetInjectionSecret() {
    if (!g_SessionValid)
        return "";
    std::lock_guard<std::mutex> lock(g_SessionMutex);
    return g_InjectionSecret;
}

// Get HWID for server calls
inline std::string GetHWID() {
    if (!g_SessionValid)
        return "";
    std::lock_guard<std::mutex> lock(g_SessionMutex);
    return g_HWID;
}

// Check if session is currently valid
inline bool IsSessionValid() {
    return g_SessionValid;
}

// Invalidate the session (e.g., server returned invalid).
// Wipes secrets from the heap before clearing.
inline void InvalidateSession() {
    g_SessionValid = false;
    std::lock_guard<std::mutex> lock(g_SessionMutex);
    zeroAndClear(g_Username);
    zeroAndClear(g_InjectionSecret);
    zeroAndClear(g_HWID);
    zeroAndClear(g_HandshakeSig);
    zeroAndClear(g_SessionToken);
    zeroAndClear(g_SyncToken);
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
    std::string syncTokenCopy, hwidCopy;
    {
        std::lock_guard<std::mutex> lock(g_SessionMutex);
        syncTokenCopy = g_SyncToken;
        hwidCopy = g_HWID;
    }
    if (syncTokenCopy.empty() || hwidCopy.empty()) {
        return "";
    }
    int64_t bucket = std::time(nullptr) / 60;  // 1 minute buckets
    std::string input = syncTokenCopy + hwidCopy + std::to_string(bucket);
    return sha256hex(input);
}

// Server public key (decoded from generated header at runtime)

// ============================================================================
// Periodically validates session with server. Kill DLL if session invalid.
// ============================================================================

inline std::atomic<bool> g_HeartbeatRunning{false};
inline HANDLE g_HeartbeatThread = nullptr;
inline HMODULE g_DllModule = nullptr;        // Captured at startup, used by reverify().
inline std::atomic<int> g_AttachStrikes{0};  // Counter for runtime anti-debug detections.

// Threshold + jitter so the corruption doesn't fire on the same heartbeat
// every time. The reverser can't associate "I patched X → cheat broke now"
// because the break is delayed and probabilistic.
constexpr int ATTACH_STRIKE_THRESHOLD = 4;

// Get server URL
inline std::string GetServerUrl() {
    return cobble_config::GetServerUrl();
}

// Perform a single heartbeat check
// Returns true ONLY if server explicitly says ok:true
// Returns false on ANY error (network, parse, server rejection) - FAIL CLOSED
inline bool DoHeartbeat() {
    // Snapshot session state once under the lock so we don't see torn writes
    // while the previous heartbeat's rotation lands in another thread.
    std::string sessionTokenCopy, hwidCopy, usernameCopy, syncHashLocal;
    {
        std::lock_guard<std::mutex> lock(g_SessionMutex);
        if (g_SessionToken.empty() || g_HWID.empty()) {
            return false;
        }
        sessionTokenCopy = g_SessionToken;
        hwidCopy = g_HWID;
        usernameCopy = g_Username;
    }
    syncHashLocal = computeSyncHash();

    // Build JSON body. Cloudflare WAF rejects form-encoded.
    std::string body = std::string("{\"session_token\":\"") + http::json_escape(sessionTokenCopy) + "\",\"hwid\":\"" +
                       http::json_escape(hwidCopy) + "\",\"username\":\"" + http::json_escape(usernameCopy) +
                       "\",\"version\":\"" + http::json_escape(cobble_config::VERSION) + "\"";
    if (!syncHashLocal.empty()) {
        body += ",\"sync_hash\":\"" + http::json_escape(syncHashLocal) + "\"";
    }
    body += "}";

    log_debug("Sending heartbeat");
    http::Response response = http::post_cobble(OBFUSCATE_WSTRING(L"/api/heartbeat").c_str(), body, 443);

    // CRITICAL: Network errors count as failures
    // If server is down, this should fail and eventually kill the DLL
    if (!response.success) {
        log_err("Heartbeat network fail: %d", response.statusCode);
        return false;  // FAIL - count as failure
    }

    // Parse signed envelope {payload_b64, sig_b64}
    json::Value envelope;
    if (!json::Parser::parse(response.body, envelope)) {
        log_err("Heartbeat JSON parse fail");
        return false;
    }

    std::string payloadB64 = envelope["payload_b64"].as_string("");
    std::string sigB64 = envelope["sig_b64"].as_string("");

    if (payloadB64.empty() || sigB64.empty()) {
        log_err("Heartbeat missing signature envelope");
        InvalidateSession();
        return false;  // FAIL CLOSED - unsigned response = tampered
    }

    auto payloadBytes = Base64Decode(payloadB64);
    auto sigBytes = Base64Decode(sigB64);

    if (payloadBytes.empty() || sigBytes.size() != 64) {
        log_err("Heartbeat invalid sig format");
        InvalidateSession();
        return false;
    }

    // Verify Ed25519 signature
    uint8_t pubkey[32];
    cobble_keys::GetPublicKey(pubkey);
    bool sigOk = tweetnacl::verify(sigBytes.data(), payloadBytes.data(), payloadBytes.size(), pubkey);
    SecureZeroMemory(pubkey, 32);

    if (!sigOk) {
        log_err("Heartbeat signature invalid - response tampered");
        InvalidateSession();
        return false;  // FAIL CLOSED
    }

    // Parse verified inner payload
    json::Value resp;
    if (!json::Parser::parse(std::string(payloadBytes.begin(), payloadBytes.end()), resp)) {
        log_err("Heartbeat payload parse fail");
        return false;
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

        // TOKEN ROTATION: Server sends new token on each successful heartbeat.
        // Stolen tokens become useless after one use. Update under lock so
        // concurrent readers always see a coherent state.
        std::string newToken = resp["new_token"].as_string("");
        std::string newSyncToken = resp["new_sync_token"].as_string("");
        if (!isAsciiToken(newToken.c_str(), 127) || !isHexString(newSyncToken.c_str(), 64)) {
            InvalidateSession();
            return false;
        }
        std::lock_guard<std::mutex> lock(g_SessionMutex);
        zeroAndClear(g_SessionToken);
        zeroAndClear(g_SyncToken);
        g_SessionToken = newToken;
        g_SyncToken = newSyncToken;
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
        // Anti-patch check: verify .text section hasn't been modified.
        // Runs every heartbeat iteration (~2-3 min) - less frequent than the
        // main loop = less cache pollution.
        DllIntegrity::Check();  // Crashes silently if patched

        // Re-verify the DLL is still NOT in the PEB module list. If a cracker
        // dumped the DLL and re-loaded it via LoadLibrary, we surface here.
        // Same delayed-corruption response as anti-debug — don't tip our hand.
        if (g_DllModule && !LoaderVerify::reverify(g_DllModule)) {
            g_AttachStrikes.fetch_add(2, std::memory_order_relaxed);
        }

        // Anti-debug runtime sweep. quickCheck() is cheap (~5 NtQip calls +
        // a CONTEXT read) so it's fine to fire each heartbeat. Each detection
        // adds a strike; corruption only kicks in past the threshold so a
        // single false positive (e.g. AV scan) doesn't take the user down.
        if (AntiAttach::quickCheck()) {
            g_AttachStrikes.fetch_add(1, std::memory_order_relaxed);
        }
        if (g_AttachStrikes.load(std::memory_order_relaxed) >= ATTACH_STRIKE_THRESHOLD) {
            g_stateCorrupted = true;
        }

        bool success = DoHeartbeat();

        if (!success) {
            consecutiveFailures++;
            if (consecutiveFailures >= maxFailures) {
                // Attempt to recover session state - may not succeed
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

// Start heartbeat thread (call after successful validation).
// Pass the DLL's HMODULE so the periodic reverify() can re-walk the PEB list.
// Session token should already be cached in g_SessionToken from ValidateInjection.
inline void StartHeartbeatThread(HMODULE hModule, const std::string& optionalToken = "") {
    if (g_HeartbeatRunning)
        return;  // Already running

    g_DllModule = hModule;

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
    if (g_initTimestamp == 0)
        return true;
    uint64_t now = GetTickCount64();
    // 5.3 minutes - slightly off to look natural
    constexpr uint64_t MAX_STALENESS = 318000;
    return (now - g_lastValidation) > MAX_STALENESS;
}

// Check if state is corrupted (recovery was triggered)
inline bool isStateCorrupted() {
    return g_stateCorrupted.load();
}

}  // namespace SessionValidator
