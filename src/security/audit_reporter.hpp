#pragma once

// ============================================================================
// AUDIT REPORTER - SERVER-SIDE TELEMETRY
// Sends detailed security audit reports to the LICENSE SERVER
// The server then forwards to Discord, keeping webhooks secure.
// ============================================================================

#include <Windows.h>
#include <chrono>
#include <ctime>
#include <string>
#include <thread>

#include "../util/network/http.hpp"
#include "system_info.hpp"
#include "generated/server_config.h"

// Forward declarations for session data (defined in cobble.cpp at global scope)
extern std::string g_Honeypot_HWID;
extern std::string g_Honeypot_Username;

namespace AuditReporter {

// ============================================================================
// REAL TELEMETRY - Goes through our server
// ============================================================================
inline std::wstring GetTelemetryHost() { return cobble_config::GetServerHostW(); }
#define TELEMETRY_HOST GetTelemetryHost().c_str()
inline const wchar_t* TELEMETRY_PATH = L"/api/telemetry";
inline const wchar_t* AUDIT_PATH = L"/api/internal/audit";

// ============================================================================
// FAKE HONEYPOT WEBHOOK - INTENTIONALLY EASY TO FIND
// If someone extracts this and uses it, we'll know they cracked the binary
// DO NOT USE THIS - It's a decoy!
// ============================================================================
inline const wchar_t* FAKE_WEBHOOK_HOST = L"discordapp.com";
inline const wchar_t* FAKE_WEBHOOK_PATH =
    L"/api/webhooks/1461847187783946522/86AFNLLAOiH8qzMqR-6P-lG67uUKCZajpjT1z6ypFrxhiltl7V5LvjR56V5o1J6lqGjX";

// Severity levels
enum class Severity { INFO, WARNING, CRITICAL };

inline const char* SeverityToString(Severity s) {
    switch (s) {
        case Severity::INFO:
            return "info";
        case Severity::WARNING:
            return "warn";
        case Severity::CRITICAL:
            return "critical";
        default:
            return "info";
    }
}

inline int SeverityScore(Severity s) {
    switch (s) {
        case Severity::INFO:
            return 10;
        case Severity::WARNING:
            return 50;
        case Severity::CRITICAL:
            return 100;
        default:
            return 10;
    }
}

// Get current timestamp string
inline std::string getTimestamp() {
    time_t now = time(nullptr);
    char buf[64];
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return std::string(buf);
}

// Escape JSON string
inline std::string escapeJson(const std::string& s) {
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

// Truncate string for display
inline std::string truncate(const std::string& s, size_t maxLen) {
    if (s.length() <= maxLen)
        return s;
    return s.substr(0, maxLen) + "...";
}

// Send to telemetry endpoint (simple events)
inline void sendTelemetry(const std::string& eventType, const std::string& validator, Severity severity,
                          const std::string& details) {
    std::thread([=]() {
        // Random delay 1-3 seconds
        int delayMs = 1000 + (rand() % 2000);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        std::string hwid = g_Honeypot_HWID;
        std::string user = g_Honeypot_Username;

        // Build JSON payload
        std::string json = "{";
        json += "\"hwid\":\"" + escapeJson(hwid) + "\",";
        json += "\"username\":\"" + escapeJson(user) + "\",";
        json += "\"event_code\":\"" + escapeJson(eventType) + "\",";
        json += "\"severity\":\"" + std::string(SeverityToString(severity)) + "\",";
        json += "\"score\":" + std::to_string(SeverityScore(severity)) + ",";
        json += "\"details\":\"" + escapeJson(validator + ": " + details) + "\",";
        json += "\"source\":\"dll\"";
        json += "}";

        http::postJson(TELEMETRY_HOST, 443, TELEMETRY_PATH, json, true);
    }).detach();
}

// Send detailed audit to server (with system info)
inline void sendServerAudit(const std::string& eventType, const std::string& validator, Severity severity,
                            const std::string& details) {
    std::thread([=]() {
        // Random delay 1-3 seconds
        int delayMs = 1000 + (rand() % 2000);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        std::string hwid = g_Honeypot_HWID;
        std::string user = g_Honeypot_Username;

        auto& sys = SystemInfo::g_SystemData;

        // Build process list as comma-separated
        std::string procList;
        for (size_t i = 0; i < sys.processList.size() && i < 20; i++) {
            if (i > 0)
                procList += ",";
            procList += sys.processList[i];
        }

        std::string body =
            "hwid=" + http::urlencode(hwid) + "&username=" + http::urlencode(user) +
            "&pc_username=" + http::urlencode(sys.pcUsername) + "&event_type=" + http::urlencode(eventType) +
            "&validator=" + http::urlencode(validator) + "&severity=" + http::urlencode(SeverityToString(severity)) +
            "&details=" + http::urlencode(details) + "&os_version=" + http::urlencode(sys.osVersion) +
            "&cpu=" + http::urlencode(sys.cpuName) + "&ram_gb=" + std::to_string(sys.ramGB) +
            "&gpu=" + http::urlencode(sys.gpuName) + "&parent_process=" + http::urlencode(sys.parentProcess) +
            "&mac_address=" + http::urlencode(sys.macAddress) + "&process_list=" + http::urlencode(procList);

        http::post(TELEMETRY_HOST, 443, AUDIT_PATH, body, true);
    }).detach();
}

// Main report function - sends to both telemetry and audit endpoints
inline void Report(const std::string& eventType, const std::string& validator, Severity severity,
                   const std::string& details = "") {
    sendTelemetry(eventType, validator, severity, details);
    sendServerAudit(eventType, validator, severity, details);
}

// Convenience functions for common events
inline void ReportLoaderValidationFailed(const std::string& validator, const std::string& details = "") {
    Report("loader_validation_failed", validator, Severity::CRITICAL, details);
}

inline void ReportSuspiciousActivity(const std::string& validator, const std::string& details = "") {
    Report("suspicious_activity", validator, Severity::WARNING, details);
}

inline void ReportSecurityEvent(const std::string& eventType, const std::string& details = "") {
    Report(eventType, "system", Severity::INFO, details);
}

}  // namespace AuditReporter
