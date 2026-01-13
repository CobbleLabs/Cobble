#pragma once

// ============================================================================
// AUDIT REPORTER
// Sends detailed security audit reports to Discord and admin panel
// ============================================================================

#include <Windows.h>
#include <string>
#include <thread>
#include <chrono>
#include <ctime>

#include "system_info.hpp"
#include "../util/network/http.hpp"

// Forward declarations for session data (defined in cobble.cpp at global scope)
extern std::string g_Honeypot_HWID;
extern std::string g_Honeypot_Username;

namespace AuditReporter {

// Discord webhook for security alerts
inline const wchar_t* WEBHOOK_HOST = L"discordapp.com";
inline const wchar_t* WEBHOOK_PATH = L"/api/webhooks/1460483137388941404/7mwGnDAaVGtNaZflh15hQtE92W9FG3ci9Kud9YDSrCQiqFwKHFuD62R1Vs19SrvYFguN";

// Admin panel endpoint for detailed audit
inline const wchar_t* AUDIT_HOST = L"api.cobble.vip";
inline const wchar_t* AUDIT_PATH = L"/api/internal/audit";

// Severity levels
enum class Severity {
    INFO,
    WARNING,
    CRITICAL
};

inline const char* SeverityToString(Severity s) {
    switch (s) {
        case Severity::INFO: return "INFO";
        case Severity::WARNING: return "WARNING";
        case Severity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

inline int SeverityToColor(Severity s) {
    switch (s) {
        case Severity::INFO: return 3447003;      // Blue
        case Severity::WARNING: return 16776960;   // Yellow
        case Severity::CRITICAL: return 16711680;  // Red
        default: return 8421504;  // Gray
    }
}

// Get current timestamp string
inline std::string getTimestamp() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

// Escape JSON string
inline std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') continue;
        else out += c;
    }
    return out;
}

// Truncate string for display
inline std::string truncate(const std::string& s, size_t maxLen) {
    if (s.length() <= maxLen) return s;
    return s.substr(0, maxLen) + "...";
}

// Send detailed audit report to Discord
inline void sendDiscordAlert(
    const std::string& eventType,
    const std::string& validator,
    Severity severity,
    const std::string& details
) {
    std::thread([=]() {
        // Random delay 1-3 seconds
        int delayMs = 1000 + (rand() % 2000);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        
        std::string hwid = g_Honeypot_HWID;
        std::string user = g_Honeypot_Username;
        std::string ts = getTimestamp();
        
        auto& sys = SystemInfo::g_SystemData;
        
        // Build Discord embed JSON with all system info
        std::string json = R"({
            "embeds": [{
                "title": "🚨 Security Alert: )" + escapeJson(eventType) + R"(",
                "color": )" + std::to_string(SeverityToColor(severity)) + R"(,
                "fields": [
                    { "name": "Validator", "value": ")" + escapeJson(validator) + R"(", "inline": true },
                    { "name": "Severity", "value": ")" + SeverityToString(severity) + R"(", "inline": true },
                    { "name": "HWID", "value": ")" + escapeJson(truncate(hwid, 16)) + R"(", "inline": true },
                    { "name": "User", "value": ")" + escapeJson(user) + R"(", "inline": true },
                    { "name": "PC Username", "value": ")" + escapeJson(sys.pcUsername) + R"(", "inline": true },
                    { "name": "OS", "value": ")" + escapeJson(truncate(sys.osVersion, 32)) + R"(", "inline": true },
                    { "name": "CPU", "value": ")" + escapeJson(truncate(sys.cpuName, 32)) + R"(", "inline": true },
                    { "name": "RAM", "value": ")" + std::to_string(sys.ramGB) + R"( GB", "inline": true },
                    { "name": "GPU", "value": ")" + escapeJson(truncate(sys.gpuName, 32)) + R"(", "inline": true },
                    { "name": "Parent Process", "value": ")" + escapeJson(sys.parentProcess) + R"(", "inline": true },
                    { "name": "Details", "value": ")" + escapeJson(truncate(details, 100)) + R"(", "inline": false }
                ],
                "footer": { "text": ")" + escapeJson(ts) + R"(" }
            }]
        })";
        
        http::postJson(WEBHOOK_HOST, 443, WEBHOOK_PATH, json, true);
    }).detach();
}

// Send detailed audit to server
inline void sendServerAudit(
    const std::string& eventType,
    const std::string& validator,
    Severity severity,
    const std::string& details
) {
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
            if (i > 0) procList += ",";
            procList += sys.processList[i];
        }
        
        std::string body = 
            "hwid=" + http::urlencode(hwid) +
            "&username=" + http::urlencode(user) +
            "&pc_username=" + http::urlencode(sys.pcUsername) +
            "&event_type=" + http::urlencode(eventType) +
            "&validator=" + http::urlencode(validator) +
            "&severity=" + http::urlencode(SeverityToString(severity)) +
            "&details=" + http::urlencode(details) +
            "&os_version=" + http::urlencode(sys.osVersion) +
            "&cpu=" + http::urlencode(sys.cpuName) +
            "&ram_gb=" + std::to_string(sys.ramGB) +
            "&gpu=" + http::urlencode(sys.gpuName) +
            "&parent_process=" + http::urlencode(sys.parentProcess) +
            "&mac_address=" + http::urlencode(sys.macAddress) +
            "&process_list=" + http::urlencode(procList);
        
        http::post(AUDIT_HOST, 443, AUDIT_PATH, body, true);
    }).detach();
}

// Main report function - sends to both Discord and server
inline void Report(
    const std::string& eventType,
    const std::string& validator,
    Severity severity,
    const std::string& details = ""
) {
    sendDiscordAlert(eventType, validator, severity, details);
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
