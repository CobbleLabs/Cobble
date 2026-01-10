#include "license.h"
#include "hwid.h"
#include "version.h"
#include "../security/xor.h"
#include "../util/json.h"
#include "../util/control_flow.hpp"
#include "../update/tweetnacl.h"
#include <Windows.h>
#include <wininet.h>
#include <wincrypt.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <ctime>


#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

// Use centralized version
#define CLIENT_VERSION COBBLE_VERSION

// Same Ed25519 public key used for updates
constexpr uint8_t LICENSE_PUBLIC_KEY[32] = {
    0xd2, 0xb8, 0x41, 0x41, 0x05, 0x22, 0xdb, 0x99, 0xe7, 0x86, 0xf4, 0x60, 0x1a, 0xb2, 0x04, 0x61, 
    0x0f, 0xdb, 0x14, 0x75, 0x0e, 0x93, 0xbb, 0xb5, 0xa1, 0x85, 0xc9, 0xd7, 0x23, 0x7d, 0x79, 0xac
};

namespace License {

static std::string g_LastError;
static bool g_NeedsUpdate = false;
static bool g_RequiresLogin = false;
static std::string g_MinVersion;
static std::string g_Username;
static std::string g_SessionToken;
static std::string g_InjectionSecret;
static std::string g_PayloadKey;
static int g_DaysRemaining = -1;

std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

// Generate random hex nonce (16 bytes = 32 hex chars)
std::string GenerateNonce() {
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    
    uint8_t bytes[16];
    if (!CryptGenRandom(hProv, sizeof(bytes), bytes)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }
    CryptReleaseContext(hProv, 0);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++) {
        oss << std::setw(2) << (int)bytes[i];
    }
    return oss.str();
}

// Base64 decode helper
std::vector<uint8_t> Base64Decode(const std::string& input) {
    DWORD outLen = 0;
    CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, nullptr, &outLen, nullptr, nullptr);
    
    std::vector<uint8_t> output(outLen);
    CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, output.data(), &outLen, nullptr, nullptr);
    output.resize(outLen);
    return output;
}


std::string GetLastError() {
    return g_LastError;
}

bool NeedsUpdate() {
    return g_NeedsUpdate;
}

std::string GetMinVersion() {
    return g_MinVersion;
}

std::string GetUsername() {
    return g_Username;
}

int GetDaysRemaining() {
    return g_DaysRemaining;
}

std::string GetSessionToken() {
    return g_SessionToken;
}

std::string GetInjectionSecret() {
    return g_InjectionSecret;
}

std::string GetPayloadKey() {
    return g_PayloadKey;
}

// Fetch payload decryption key from server after successful login/verify
bool FetchPayloadKey(const std::string& serverUrl) {
    if (g_SessionToken.empty()) {
        g_LastError = "No session token";
        return false;
    }
    
    std::string hwid = HWID::Generate();
    if (hwid.empty()) {
        g_LastError = "HWID generation failed";
        return false;
    }
    
    // Build URL
    std::string url = serverUrl + _xor_("/api/loader/payload-key");
    
    // Build POST data
    std::string postData = "hwid=" + UrlEncode(hwid) + 
                          "&session_token=" + UrlEncode(g_SessionToken);
    
    // Initialize WinInet
    HINTERNET hInternet = InternetOpenA(
        _xor_("Mozilla/5.0"),
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL, NULL, 0
    );
    
    if (!hInternet) {
        g_LastError = "Network error";
        return false;
    }
    
    DWORD timeout = 5000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    
    // Parse URL
    URL_COMPONENTSA urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    
    char hostName[256] = { 0 };
    char urlPath[1024] = { 0 };
    
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath);
    
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &urlComp)) {
        InternetCloseHandle(hInternet);
        g_LastError = "Invalid URL";
        return false;
    }
    
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;
    
    HINTERNET hConnect = InternetConnectA(
        hInternet,
        hostName,
        urlComp.nPort,
        NULL, NULL,
        INTERNET_SERVICE_HTTP,
        0, 0
    );
    
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        g_LastError = "Connection failed";
        return false;
    }
    
    HINTERNET hRequest = HttpOpenRequestA(
        hConnect,
        "POST",
        urlPath,
        NULL, NULL, NULL,
        flags | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD,
        0
    );
    
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Request failed";
        return false;
    }
    
    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    
    BOOL result = HttpSendRequestA(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        (LPVOID)postData.c_str(),
        (DWORD)postData.length()
    );
    
    if (!result) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Server error";
        return false;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, 
                   &statusCode, &statusCodeSize, NULL);
    
    if (statusCode != 200) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Key fetch failed";
        return false;
    }
    
    // Read response
    char buffer[4096] = { 0 };
    DWORD bytesRead = 0;
    std::string response;
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    // Parse JSON response: { "key": "..." }
    json::Object json;
    if (!json.parse(response)) {
        g_LastError = "Invalid response";
        return false;
    }
    
    g_PayloadKey = json.get_string("key", "");
    if (g_PayloadKey.empty() || g_PayloadKey.length() != 64) {
        g_LastError = "Invalid key";
        return false;
    }
    
    return true;
}

bool Verify(const std::string& serverUrl) {
    // Generate HWID
    std::string hwid = HWID::Generate();
    if (hwid.empty()) {
        g_LastError = "Failed to generate HWID";
        return false;
    }
    
    // Generate random nonce for anti-replay
    std::string nonce = GenerateNonce();
    if (nonce.empty()) {
        g_LastError = "Failed to generate nonce";
        return false;
    }
    
    // Build URL
    std::string url = serverUrl + _xor_("/api/verify");
    
    // Build POST data with HWID, nonce, and version
    std::string postData = "hwid=" + UrlEncode(hwid) + 
                          "&nonce=" + UrlEncode(nonce) +
                          "&version=" + UrlEncode(CLIENT_VERSION);
    
    // Initialize WinInet
    HINTERNET hInternet = InternetOpenA(
        _xor_("Mozilla/5.0"),
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL, NULL, 0
    );
    
    if (!hInternet) {
        g_LastError = "Failed to initialize internet";
        return false;
    }

    DWORD timeout = 5000; // 5s timeout for crypto operations
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    
    // Parse URL
    URL_COMPONENTSA urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    
    char hostName[256] = { 0 };
    char urlPath[1024] = { 0 };
    
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath);
    
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &urlComp)) {
        InternetCloseHandle(hInternet);
        g_LastError = "Invalid server URL";
        return false;
    }
    
    // Connect to server
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? 
                  INTERNET_FLAG_SECURE : 0;
    
    HINTERNET hConnect = InternetConnectA(
        hInternet,
        hostName,
        urlComp.nPort,
        NULL, NULL,
        INTERNET_SERVICE_HTTP,
        0, 0
    );
    
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        g_LastError = "Failed to connect to server";
        return false;
    }
    
    // Create request
    HINTERNET hRequest = HttpOpenRequestA(
        hConnect,
        "POST",
        urlPath,
        NULL, NULL, NULL,
        flags | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD,
        0
    );
    
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Failed to create request";
        return false;
    }
    
    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    
    // Send request
    BOOL result = HttpSendRequestA(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        (LPVOID)postData.c_str(),
        (DWORD)postData.length()
    );
    
    if (!result) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Failed to send request";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeSize, NULL);

    // Note: We don't return early on non-200 anymore - the server sends signed
    // error responses that we need to parse to determine if login is required

    
    // Read response
    char buffer[4096] = { 0 };
    DWORD bytesRead = 0;
    std::string response;
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    
    // Cleanup
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    // Check for empty response
    if (response.empty()) {
        g_LastError = "No response from server (Status: " + std::to_string(statusCode) + ")";
        // Assume unregistered HWID if we got a 4xx error
        if (statusCode >= 400 && statusCode < 500) {
            g_RequiresLogin = true;
        }
        return false;
    }
    
    // Parse JSON response
    json::Object json;
    if (!json.parse(response)) {
        // Check if raw response indicates login needed
        if (response.find("not registered") != std::string::npos ||
            response.find("login") != std::string::npos ||
            response.find("No license") != std::string::npos) {
            g_RequiresLogin = true;
            g_LastError = "Device not registered";
        } else {
            g_LastError = "Invalid server response";
        }
        return false;
    }
    
    // Check for signed response format - REQUIRED for security
    std::string payloadB64 = json.get_string("payload_b64", "");
    std::string sigB64 = json.get_string("sig_b64", "");
    
    if (payloadB64.empty() || sigB64.empty()) {
        // No signature - check for plain error response with login hint
        std::string errorMsg = json.get_string("error", "");
        std::string message = json.get_string("message", "");
        if (errorMsg.find("not registered") != std::string::npos ||
            errorMsg.find("login") != std::string::npos ||
            message.find("not registered") != std::string::npos ||
            message.find("login") != std::string::npos) {
            g_RequiresLogin = true;
            g_LastError = "Device not registered";
        } else {
            g_LastError = errorMsg.empty() ? "Invalid response format" : errorMsg;
        }
        return false;
    }
    
    // Decode payload and signature
    std::vector<uint8_t> payloadBytes = Base64Decode(payloadB64);
    std::vector<uint8_t> sigBytes = Base64Decode(sigB64);
    
    if (payloadBytes.empty() || sigBytes.size() != 64) {
        g_LastError = "Invalid response format";
        return false;
    }
    
    // Verify Ed25519 signature
    if (!tweetnacl::verify(sigBytes.data(), payloadBytes.data(), payloadBytes.size(), LICENSE_PUBLIC_KEY)) {
        g_LastError = "Invalid signature - response may have been tampered";
        return false;
    }
    
    // Parse verified payload
    std::string payloadStr(payloadBytes.begin(), payloadBytes.end());
    json::Object payload;
    if (!payload.parse(payloadStr)) {
        g_LastError = "Invalid payload format";
        return false;
    }
    
    // Verify nonce matches what we sent (anti-replay)
    std::string responseNonce = payload.get_string("nonce", "");
    if (responseNonce != nonce) {
        g_LastError = "Nonce mismatch - possible replay attack";
        return false;
    }
    
    // Verify response hasn't expired
    int64_t expiresAt = payload.get_int("expires_at", 0);
    int64_t now = (int64_t)time(nullptr);
    if (now > expiresAt) {
        g_LastError = "Response expired";
        return false;
    }
    
    // All cryptographic checks passed - now check business logic
    if (payload.get_bool("valid", false)) {
        g_Username = payload.get_string("username", "");
        g_DaysRemaining = payload.get_int("days_remaining", -1);
        g_SessionToken = payload.get_string("session_token", "");
        g_InjectionSecret = payload.get_string("injection_secret", "");
        return true;
    }
    
    // Extract error message
    g_LastError = payload.get_string("message", "License not valid");
    
    // Check if this is an unregistered HWID (requires login)
    std::string errCode = payload.get_string("code", "");
    if (errCode == "HWID_NOT_REGISTERED" || 
        g_LastError.find("not registered") != std::string::npos ||
        g_LastError.find("No license") != std::string::npos ||
        g_LastError.find("login first") != std::string::npos ||
        g_LastError.find("Please login") != std::string::npos) {
        g_RequiresLogin = true;
    }
    return false;
}

bool RequiresLogin() {
    return g_RequiresLogin;
}

bool LoginWithCredentials(const std::string& serverUrl, 
                          const std::string& username, 
                          const std::string& password) {
    g_RequiresLogin = false;
    
    // Generate HWID
    std::string hwid = HWID::Generate();
    if (hwid.empty()) {
        g_LastError = "Failed to generate HWID";
        return false;
    }
    
    // Generate random nonce for anti-replay
    std::string nonce = GenerateNonce();
    if (nonce.empty()) {
        g_LastError = "Failed to generate nonce";
        return false;
    }
    
    // Build URL
    std::string url = serverUrl + _xor_("/api/loader/login");
    
    // Build POST data as JSON
    std::string postData = "{\"username\":\"" + username + 
                           "\",\"password\":\"" + password + 
                           "\",\"hwid\":\"" + hwid + 
                           "\",\"nonce\":\"" + nonce + 
                           "\",\"version\":\"" + std::string(CLIENT_VERSION) + "\"}";
    
    // Initialize WinInet
    HINTERNET hInternet = InternetOpenA(
        _xor_("Mozilla/5.0"),
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL, NULL, 0
    );
    
    if (!hInternet) {
        g_LastError = "Network error";
        return false;
    }
    
    DWORD timeout = 8000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    
    // Parse URL
    URL_COMPONENTSA urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    
    char hostName[256] = { 0 };
    char urlPath[1024] = { 0 };
    
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath);
    
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &urlComp)) {
        InternetCloseHandle(hInternet);
        g_LastError = "Invalid server URL";
        return false;
    }
    
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? 
                  INTERNET_FLAG_SECURE : 0;
    
    HINTERNET hConnect = InternetConnectA(
        hInternet,
        hostName,
        urlComp.nPort,
        NULL, NULL,
        INTERNET_SERVICE_HTTP,
        0, 0
    );
    
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        g_LastError = "Connection failed";
        return false;
    }
    
    HINTERNET hRequest = HttpOpenRequestA(
        hConnect,
        "POST",
        urlPath,
        NULL, NULL, NULL,
        flags | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD,
        0
    );
    
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Request failed";
        return false;
    }
    
    std::string headers = "Content-Type: application/json\r\n";
    
    BOOL result = HttpSendRequestA(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        (LPVOID)postData.c_str(),
        (DWORD)postData.length()
    );
    
    if (!result) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Request failed";
        return false;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeSize, NULL);
    
    // Read response
    char buffer[4096] = { 0 };
    DWORD bytesRead = 0;
    std::string response;
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    // Parse JSON response
    json::Object json;
    if (!json.parse(response)) {
        g_LastError = "Invalid response";
        return false;
    }
    
    // Check for signed response format
    std::string payloadB64 = json.get_string("payload_b64", "");
    std::string sigB64 = json.get_string("sig_b64", "");
    
    if (payloadB64.empty() || sigB64.empty()) {
        // Fallback to unsigned response (legacy or error)
        if (statusCode != 200) {
            g_LastError = json.get_string("error", "Authentication failed");
            return false;
        }
        g_LastError = "Invalid response format";
        return false;
    }
    
    // Decode payload and signature
    std::vector<uint8_t> payloadBytes = Base64Decode(payloadB64);
    std::vector<uint8_t> sigBytes = Base64Decode(sigB64);
    
    if (payloadBytes.empty() || sigBytes.size() != 64) {
        g_LastError = "Invalid response format";
        return false;
    }
    
    // Verify Ed25519 signature
    if (!tweetnacl::verify(sigBytes.data(), payloadBytes.data(), payloadBytes.size(), LICENSE_PUBLIC_KEY)) {
        g_LastError = "Signature verification failed";
        return false;
    }
    
    // Parse verified payload
    std::string payloadStr(payloadBytes.begin(), payloadBytes.end());
    json::Object payload;
    if (!payload.parse(payloadStr)) {
        g_LastError = "Invalid payload";
        return false;
    }
    
    // Verify nonce matches
    std::string responseNonce = payload.get_string("nonce", "");
    if (responseNonce != nonce) {
        g_LastError = "Security check failed";
        return false;
    }
    
    // Verify response hasn't expired
    int64_t expiresAt = payload.get_int("expires_at", 0);
    int64_t now = (int64_t)time(nullptr);
    if (now > expiresAt) {
        g_LastError = "Response expired";
        return false;
    }
    
    // Check business logic
    if (payload.get_bool("valid", false)) {
        g_Username = payload.get_string("username", username);
        g_DaysRemaining = payload.get_int("days_remaining", -1);
        g_SessionToken = payload.get_string("session_token", "");
        g_InjectionSecret = payload.get_string("injection_secret", "");
        return true;
    }
    
    g_LastError = payload.get_string("message", "Authentication failed");
    return false;
}


// Register new user (returns true if successful)
bool Register(const std::string& serverUrl, const std::string& username) {
    std::string hwid = HWID::Generate();
    if (hwid.empty()) {
        g_LastError = "Failed to generate HWID";
        return false;
    }
    
    std::string url = serverUrl + "/api/register";
    std::string postData = "hwid=" + hwid + "&username=" + username;
    
    HINTERNET hInternet = InternetOpenA(_xor_("Mozilla/5.0"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;
    
    URL_COMPONENTSA urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    char hostName[256] = { 0 };
    char urlPath[1024] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath);
    
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &urlComp)) {
        InternetCloseHandle(hInternet);
        return false;
    }
    
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;
    
    HINTERNET hConnect = InternetConnectA(hInternet, hostName, urlComp.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }
    
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", urlPath, NULL, NULL, NULL, flags | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }
    
    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
    
    char buffer[1024] = { 0 };
    DWORD bytesRead = 0;
    std::string response;
    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    if (response.find("\"success\":true") != std::string::npos) {
        return true;
    }
    
    // Extract error
    size_t errStart = response.find("\"error\":\"");
    if (errStart != std::string::npos) {
        errStart += 9;
        size_t errEnd = response.find("\"", errStart);
        if (errEnd != std::string::npos) {
            g_LastError = response.substr(errStart, errEnd - errStart);
        }
    }
    
    return false;
}

}
