#pragma once
// ============================================================================
// TELEMETRY - Non-blocking security event reporter with throttling
// ============================================================================

#include <Windows.h>
#include <wininet.h>
#include <chrono>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include "../security/xor.h"
#include "../version.h"
#include "hwid.h"

#pragma comment(lib, "wininet.lib")

namespace Telemetry {

// ============================================================================
// CONFIG
// ============================================================================
constexpr int MAX_QUEUE_SIZE = 10;
constexpr int THROTTLE_SECONDS = 300;  // 5 min cooldown per event_code
constexpr int HTTP_TIMEOUT_MS = 1500;
constexpr int BATCH_SIZE = 5;

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
// EVENT STRUCTURE
// ============================================================================
struct SecurityEvent {
    std::string event_code;  // "DBG_PEB", "TOOL_X64DBG", etc.
    Severity severity;
    int score;
    std::string details;
    int64_t timestamp;
};

// ============================================================================
// INTERNAL STATE (module-level, thread-safe)
// ============================================================================
namespace Internal {
inline std::mutex g_Mutex;
inline std::queue<SecurityEvent> g_Queue;
inline std::unordered_map<std::string, int64_t> g_LastSent;  // throttle map
inline std::string g_SessionId;
inline bool g_Initialized = false;

// Generate short session ID (first 8 chars of HWID hash)
inline void Initialize() {
    if (g_Initialized)
        return;
    std::string hwid = HWID::Generate();
    g_SessionId = hwid.empty() ? "unknown" : hwid.substr(0, 16);
    g_Initialized = true;
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

// Get severity color for Discord embed
inline int SeverityToColor(Severity s) {
    switch (s) {
        case Severity::CRITICAL:
            return 16711680;  // Red
        case Severity::HIGH:
            return 16744448;  // Orange
        case Severity::WARN:
            return 16776960;  // Yellow
        case Severity::INFO:
            return 3447003;  // Blue
        default:
            return 8421504;  // Gray
    }
}

// Thread function for fire-and-forget Discord webhook
inline DWORD WINAPI SendThreadProc(LPVOID lpParam) {
    std::string* pBody = static_cast<std::string*>(lpParam);
    std::string body = *pBody;
    delete pBody;

    // Random delay 500-1500ms to not spam
    Sleep(500 + (rand() % 1000));

    HINTERNET hInternet = InternetOpenA(_xor_("Mozilla/5.0"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet)
        return 0;

    DWORD timeout = HTTP_TIMEOUT_MS;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // Connect to Discord
    HINTERNET hConnect =
        InternetConnectA(hInternet, _xor_("discordapp.com"), 443, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return 0;
    }

    // Same webhook as DLL audit_reporter
    HINTERNET hRequest = HttpOpenRequestA(
        hConnect, "POST",
        _xor_("/api/webhooks/1460483137388941404/7mwGnDAaVGtNaZflh15hQtE92W9FG3ci9Kud9YDSrCQiqFwKHFuD62R1Vs19SrvYFguN"),
        NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return 0;
    }

    std::string headers = "Content-Type: application/json\r\n";
    HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)body.c_str(), (DWORD)body.length());

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return 0;
}

// Build Discord embed JSON and send
inline void SendToDiscord(const std::string& event_code, Severity severity, int score, const std::string& details) {
    // Get HWID for identification
    std::string hwid = g_SessionId;

    // Get timestamp
    time_t now = time(nullptr);
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // Build Discord embed JSON
    std::string json = R"({
        "embeds": [{
            "title": ")" +
                       EscapeJson("🔒 Loader Security: " + event_code) + R"(",
            "color": )" +
                       std::to_string(SeverityToColor(severity)) + R"(,
            "fields": [
                { "name": "Event", "value": ")" +
                       EscapeJson(event_code) + R"(", "inline": true },
                { "name": "Severity", "value": ")" +
                       SeverityToString(severity) + R"(", "inline": true },
                { "name": "Score", "value": ")" +
                       std::to_string(score) + R"(", "inline": true },
                { "name": "HWID", "value": ")" +
                       EscapeJson(hwid) + R"(", "inline": true },
                { "name": "Loader", "value": ")" +
                       EscapeJson(COBBLE_VERSION) + R"(", "inline": true },
                { "name": "Details", "value": ")" +
                       EscapeJson(details.empty() ? "-" : details) + R"(", "inline": false }
            ],
            "footer": { "text": ")" +
                       std::string(timeBuf) + R"(" }
        }]
    })";

    // Send async
    std::string* pBody = new std::string(json);
    HANDLE hThread = CreateThread(NULL, 0, SendThreadProc, pBody, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete pBody;
    }
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

// Report a security event (non-blocking, throttled)
inline void Report(const std::string& event_code, Severity severity, int score, const std::string& details = "") {
    // Ensure initialized
    Internal::Initialize();

    // Check throttle
    if (Internal::IsThrottled(event_code)) {
        return;  // Skip - already sent recently
    }

    // Mark as sent
    Internal::MarkSent(event_code);

    // Send to Discord (non-blocking)
    Internal::SendToDiscord(event_code, severity, score, details);
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

}  // namespace Telemetry
