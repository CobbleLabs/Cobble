#pragma once
// ============================================================================
// TELEMETRY - Non-blocking security event reporter (SERVER-SIDE)
// Events are sent to the license server, which forwards them to Discord
// This removes hardcoded webhooks from the binary
// ============================================================================

#include <Windows.h>
#include <wininet.h>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include "../util/aes_string.hpp"
#include "../version.h"
#include "hwid.h"

#pragma comment(lib, "wininet.lib")

namespace Telemetry {

// ============================================================================
// CONFIG
// ============================================================================
constexpr int THROTTLE_SECONDS = 300;  // 5 min cooldown per event_code
constexpr int HTTP_TIMEOUT_MS = 2000;

// Severity levels
enum class Severity { INFO, WARN, HIGH, CRITICAL };

inline const char* SeverityToString(Severity s) {
    switch (s) {
        case Severity::INFO:
            return "info";
        case Severity::WARN:
            return "warn";
        case Severity::HIGH:
            return "high";
        case Severity::CRITICAL:
            return "critical";
        default:
            return "info";
    }
}

// ============================================================================
// INTERNAL STATE (module-level, thread-safe)
// ============================================================================
namespace Internal {
inline std::mutex g_Mutex;
inline std::unordered_map<std::string, int64_t> g_LastSent;  // throttle map
inline std::string g_SessionId;
inline std::string g_ServerHost = OBFUSCATE_STRING("api.syra.club");
inline bool g_Initialized = false;

// Generate short session ID (first 16 chars of HWID)
inline void Initialize() {
    if (g_Initialized)
        return;
    std::string hwid = HWID::Generate();
    g_SessionId = hwid.empty() ? "unknown" : hwid.substr(0, 16);
    g_Initialized = true;
}

inline std::string NormalizeHost(const std::string& hostOrUrl) {
    std::string host = hostOrUrl;
    const std::string https = "https://";
    const std::string http = "http://";
    if (host.rfind(https, 0) == 0) {
        host = host.substr(https.size());
    } else if (host.rfind(http, 0) == 0) {
        host = host.substr(http.size());
    }
    size_t slash = host.find('/');
    if (slash != std::string::npos) {
        host = host.substr(0, slash);
    }
    return host;
}

// URL encode helper
inline std::string UrlEncode(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            char hex[4];
            sprintf_s(hex, "%%%02X", c);
            result += hex;
        }
    }
    return result;
}

// Escape JSON string
inline std::string EscapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            continue;
        else
            out += c;
    }
    return out;
}

// Thread data structure
struct TelemetryPayload {
    std::string hwid;
    std::string event_code;
    std::string severity;
    int score;
    std::string details;
    std::string version;
};

inline bool SendPayload(const TelemetryPayload& payload) {
    HINTERNET hInternet =
        InternetOpenA(OBFUSCATE_STRING("Mozilla/5.0").c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet)
        return false;

    DWORD timeout = HTTP_TIMEOUT_MS;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // Connect to our license server (NOT Discord!)
    std::string serverHost = g_ServerHost.empty() ? OBFUSCATE_STRING("api.syra.club") : g_ServerHost;
    HINTERNET hConnect = InternetConnectA(hInternet, serverHost.c_str(), 443, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }

    // POST to /api/telemetry
    HINTERNET hRequest =
        HttpOpenRequestA(hConnect, "POST", OBFUSCATE_STRING("/api/telemetry").c_str(), NULL, NULL, NULL,
                         INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }

    // Build JSON payload
    std::string json = "{";
    json += "\"hwid\":\"" + EscapeJson(payload.hwid) + "\",";
    json += "\"event_code\":\"" + EscapeJson(payload.event_code) + "\",";
    json += "\"severity\":\"" + EscapeJson(payload.severity) + "\",";
    json += "\"score\":" + std::to_string(payload.score) + ",";
    json += "\"details\":\"" + EscapeJson(payload.details) + "\",";
    json += "\"version\":\"" + EscapeJson(payload.version) + "\",";
    json += "\"source\":\"loader\"";
    json += "}";

    std::string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)json.c_str(),
                                 (DWORD)json.length());

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return sent == TRUE;
}

// Thread function for fire-and-forget server request
inline DWORD WINAPI SendThreadProc(LPVOID lpParam) {
    TelemetryPayload* pPayload = static_cast<TelemetryPayload*>(lpParam);
    TelemetryPayload payload = *pPayload;
    delete pPayload;

    // Random delay 300-800ms to avoid detection patterns
    Sleep(300 + (rand() % 500));

    SendPayload(payload);
    return 0;
}

// Check if event_code is throttled
inline bool IsThrottled(const std::string& code) {
    std::lock_guard<std::mutex> lock(g_Mutex);
    auto it = g_LastSent.find(code);
    if (it == g_LastSent.end())
        return false;

    int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    return (now - it->second) < THROTTLE_SECONDS;
}

// Mark event_code as sent (for throttling)
inline void MarkSent(const std::string& code) {
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_LastSent[code] =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace Internal

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize telemetry (call once at startup)
inline void Init() {
    Internal::Initialize();
}

inline void Init(const std::string& serverHost) {
    {
        std::lock_guard<std::mutex> lock(Internal::g_Mutex);
        if (!serverHost.empty()) {
            Internal::g_ServerHost = Internal::NormalizeHost(serverHost);
        }
    }
    Internal::Initialize();
}

// Report a security event immediately (blocking, no throttle)
inline void ReportImmediate(const std::string& event_code, Severity severity, int score,
                            const std::string& details = "") {
    Internal::Initialize();

    Internal::TelemetryPayload payload;
    payload.hwid = Internal::g_SessionId;
    payload.event_code = event_code;
    payload.severity = SeverityToString(severity);
    payload.score = score;
    payload.details = details;
    payload.version = COBBLE_VERSION;

    // Retry a couple times to increase delivery reliability
    for (int i = 0; i < 2; i++) {
        if (Internal::SendPayload(payload)) {
            return;
        }
        Sleep(150);
    }
}

// Report a security event (non-blocking, throttled)
inline void Report(const std::string& event_code, Severity severity, int score, const std::string& details = "") {
    // Ensure initialized
    Internal::Initialize();

    if (severity == Severity::CRITICAL) {
        ReportImmediate(event_code, severity, score, details);
        return;
    }

    // Check throttle
    if (Internal::IsThrottled(event_code)) {
        return;  // Skip - already sent recently
    }

    // Mark as sent
    Internal::MarkSent(event_code);

    // Build payload
    Internal::TelemetryPayload* pPayload = new Internal::TelemetryPayload();
    pPayload->hwid = Internal::g_SessionId;
    pPayload->event_code = event_code;
    pPayload->severity = SeverityToString(severity);
    pPayload->score = score;
    pPayload->details = details;
    pPayload->version = COBBLE_VERSION;

    // Send async
    HANDLE hThread = CreateThread(NULL, 0, Internal::SendThreadProc, pPayload, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete pPayload;
    }
}

// Convenience functions for common events
inline void ReportDebugger(const std::string& code, int score, const std::string& details = "") {
    Report(code, Severity::CRITICAL, score, details);
}

inline void ReportTool(const std::string& toolName, int score) {
    Report("TOOL_" + toolName, score >= 40 ? Severity::WARN : Severity::INFO, score, toolName + " detected");
}

inline void ReportVM(const std::string& method) {
    Report("VM_" + method, Severity::WARN, 30, method + " detected");
}

// ============================================================================
// HONEYPOT WEBHOOK
// This is intentionally easy to find with strings. If anyone uses it,
// we know they extracted and are trying to use our binary.
// ============================================================================
namespace Honeypot {
// DO NOT USE - This is a decoy! The real telemetry goes through the server.
inline const char* FAKE_WEBHOOK_DO_NOT_USE =
    "https://discord.com/api/webhooks/1461846578431131870/"
    "dpl1XTvq-6fAkkDfyLn-yOxW-py5AORzJnInt4bOIimp_uF7H0RH1XavAqINOh7lkGqF";

// If this function is ever called, report to the server
inline void TriggerHoneypot() {
    Report("HONEYPOT_TRIGGERED", Severity::CRITICAL, 100, "Cracker attempted to use fake webhook");
}
}  // namespace Honeypot

}  // namespace Telemetry
