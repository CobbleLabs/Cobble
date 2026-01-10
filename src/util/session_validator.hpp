#pragma once

#include <Windows.h>
#include <wincrypt.h>
#include <string>
#include <ctime>
#include <atomic>
#include <vector>

#pragma comment(lib, "crypt32.lib")

#include "http.hpp"
#include "json.hpp"
#include "tweetnacl.h"  // Ed25519 verification

// ============================================================================
// SESSION VALIDATOR
// Validates that the DLL was loaded by the legitimate Cobble loader with a
// valid server-issued injection secret. This is the PRIMARY security gate.
// ============================================================================

namespace SessionValidator {

// This struct MUST match the InjectionData struct in loader/main.cpp
struct InjectionData {
    char signature[16];       // "COBBLE_INJ_V1" - magic signature
    char secret[65];          // 64 hex chars + null (injection_secret from server)
    char username[64];        // Username from license
    char hwid[65];            // HWID for server validation
    uint32_t timestamp;       // Unix timestamp when created
    uint32_t expiresIn;       // Seconds until expiry (5 minutes)
    char handshakeSig[128];   // Base64 Ed25519 signature from server
    char sessionToken[128];   // Session token for heartbeat
};

// Same Ed25519 public key as loader (for signature verification)
constexpr uint8_t COBBLE_PUBLIC_KEY[32] = {
    0xd2, 0xb8, 0x41, 0x41, 0x05, 0x22, 0xdb, 0x99, 0xe7, 0x86, 0xf4, 0x60, 0x1a, 0xb2, 0x04, 0x61, 
    0x0f, 0xdb, 0x14, 0x75, 0x0e, 0x93, 0xbb, 0xb5, 0xa1, 0x85, 0xc9, 0xd7, 0x23, 0x7d, 0x79, 0xac
};

// Cached session data (set after successful validation)
inline std::string g_Username;
inline std::string g_InjectionSecret;
inline std::string g_HWID;
inline std::string g_HandshakeSig;   // For server-side validation
inline std::string g_SessionToken;   // For heartbeat
inline std::atomic<bool> g_SessionValid{false};

// Read injection data from shared memory
// Returns true if data was successfully read and signature is valid
inline bool ReadInjectionData(InjectionData* outData) {
	if (!outData) return false;

	// Open the shared memory created by the loader
	HANDLE hMapFile = OpenFileMappingW(
		FILE_MAP_ALL_ACCESS,
		FALSE,
		L"Local\\CobbleInjection"
	);

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
    snprintf(handshakeData, sizeof(handshakeData), "COBBLE_HANDSHAKE|%s|%s|%lld|%s",
             data.username, data.hwid, (long long)data.timestamp, data.secret);
    
    // Verify Ed25519 signature - MUST pass
    if (!tweetnacl::verify(sigBytes.data(), 
                           (const uint8_t*)handshakeData, 
                           strlen(handshakeData), 
                           COBBLE_PUBLIC_KEY)) {
        // Invalid signature = fake loader, tampered data, or format mismatch
        return false;
    }
    
	// Cache the validated data
	g_Username = data.username;
	g_InjectionSecret = data.secret;
	g_HWID = data.hwid;
    g_HandshakeSig = data.handshakeSig;
    g_SessionToken = data.sessionToken;
	g_SessionValid = true;
    
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

	uint8_t bytes[16] = { 0 };
	if (!CryptGenRandom(hProv, sizeof(bytes), bytes)) {
		CryptReleaseContext(hProv, 0);
		return "";
	}
	CryptReleaseContext(hProv, 0);

	char buf[33] = { 0 };
	for (int i = 0; i < 16; i++) {
		sprintf_s(buf + (i * 2), 3, "%02x", bytes[i]);
	}
	return std::string(buf, 32);
}

// Base64 decode helper (uses WinCrypt)
inline std::vector<uint8_t> Base64Decode(const std::string& input) {
	DWORD outLen = 0;
	if (!CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, nullptr, &outLen, nullptr, nullptr)) {
		return {};
	}

	std::vector<uint8_t> output(outLen);
	if (!CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, output.data(), &outLen, nullptr, nullptr)) {
		return {};
	}
	output.resize(outLen);
	return output;
}

// Server public key (same as loader/license verification)
constexpr uint8_t LICENSE_PUBLIC_KEY[32] = {
	0xd2, 0xb8, 0x41, 0x41, 0x05, 0x22, 0xdb, 0x99,
	0xe7, 0x86, 0xf4, 0x60, 0x1a, 0xb2, 0x04, 0x61,
	0x0f, 0xdb, 0x14, 0x75, 0x0e, 0x93, 0xbb, 0xb5,
	0xa1, 0x85, 0xc9, 0xd7, 0x23, 0x33, 0x7d, 0x79
};

// Validate injection secret with backend; requires prior local validation
inline bool ValidateWithServer() {
	if (!g_SessionValid) return false;

	std::string nonce = GenerateNonce();
	if (nonce.empty()) {
		return false;
	}

	std::string body = "hwid=" + http::urlencode(g_HWID) +
		"&secret=" + http::urlencode(g_InjectionSecret) +
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
    
    // Build request body with URL encoding for safety
    std::string body = "session_token=" + http::urlencode(g_SessionToken) +
                       "&hwid=" + http::urlencode(g_HWID) +
                       "&username=" + http::urlencode(g_Username) +
                       "&version=1.0.30";
    
    // Make HTTP POST request to api.cobble.vip:443/api/heartbeat
    http::Response response = http::post(L"api.cobble.vip", 443, L"/api/heartbeat", body, true);
    
    // CRITICAL: Network errors count as failures
    // If server is down, this should fail and eventually kill the DLL
    if (!response.success) {
        return false;  // FAIL - count as failure
    }
    
    // Parse response - parse errors also count as failures
    json::Value resp;
    if (!json::Parser::parse(response.body, resp)) {
        return false;  // FAIL - malformed response
    }
    
    // Only return true if server explicitly says ok:true
    bool ok = resp["ok"].as_bool(false);
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
    const int maxFailures = 3;  // Kill after 3 failed heartbeats (~6-9 min max)
    
    while (g_HeartbeatRunning && g_SessionValid) {
        bool success = DoHeartbeat();
        
        if (!success) {
            consecutiveFailures++;
            if (consecutiveFailures >= maxFailures) {
                // Kill the DLL - server down, banned, expired, or network blocked
                g_SessionValid = false;
                InvalidateSession();
                
                // Terminate process (fail closed)
                ExitProcess(0);
                return 0;
            }
            // Short retry on failure (30s instead of 2min)
            Sleep(30000);
        } else {
            consecutiveFailures = 0;
            // Normal interval: 120-180 seconds with jitter
            DWORD baseInterval = 120000;  // 2 minutes base
            DWORD jitter = (rand() % 60000);  // 0-60 seconds jitter
            Sleep(baseInterval + jitter);
        }
    }
    
    return 0;
}

// Start heartbeat thread (call after successful validation)
// Session token should already be cached in g_SessionToken from ValidateInjection
inline void StartHeartbeatThread(const std::string& optionalToken = "") {
    if (g_HeartbeatRunning) return;  // Already running
    
    // Use provided token or fall back to cached token
    if (!optionalToken.empty()) {
        g_SessionToken = optionalToken;
    }
    // g_SessionToken should already be set from ValidateInjection
    
    g_HeartbeatRunning = true;
    
    g_HeartbeatThread = CreateThread(
        nullptr,
        0,
        HeartbeatThreadProc,
        nullptr,
        0,
        nullptr
    );
}

// Stop heartbeat thread (call on DLL unload)
inline void StopHeartbeatThread() {
    g_HeartbeatRunning = false;
    if (g_HeartbeatThread) {
        WaitForSingleObject(g_HeartbeatThread, 5000);
        CloseHandle(g_HeartbeatThread);
        g_HeartbeatThread = nullptr;
    }
}

} // namespace SessionValidator
