#pragma once

// ============================================================================
// HONEYPOT SYSTEM
// Decoy functions and variables that look valuable to reversers.
// When accessed, they report to Discord webhook and admin panel.
// ============================================================================

#include <Windows.h>
#include <string>
#include <ctime>
#include <thread>
#include <chrono>

#include "../util/network/http.hpp"

namespace Honeypot {

// Discord webhook for instant notifications
inline const wchar_t* WEBHOOK_HOST = L"discordapp.com";
inline const wchar_t* WEBHOOK_PATH = L"/api/webhooks/1460483137388941404/7mwGnDAaVGtNaZflh15hQtE92W9FG3ci9Kud9YDSrCQiqFwKHFuD62R1Vs19SrvYFguN";

// Admin panel endpoint
inline const wchar_t* TELEMETRY_HOST = L"api.cobble.vip";
inline const wchar_t* TELEMETRY_PATH = L"/api/internal/telemetry";

// Prevent spam - only report once per type per session
inline std::atomic<uint32_t> g_reportedFlags{0};

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
        else out += c;
    }
    return out;
}

// Forward declarations for session data (defined in session_validator.hpp)
// We include it directly to access the globals

// Report to Discord webhook (async, with random delay)
inline void reportToDiscord(const std::string& eventType, const std::string& details) {
    std::thread([eventType, details]() {
        // Random delay 2-5 seconds to avoid detection
        int delayMs = 2000 + (rand() % 3000);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        
        // Access globals from session_validator.hpp (included via cobble.cpp)
        extern std::string g_Honeypot_HWID;
        extern std::string g_Honeypot_Username;
        std::string hwid = g_Honeypot_HWID;
        std::string user = g_Honeypot_Username;
        std::string ts = getTimestamp();
        
        // Build Discord embed JSON
        std::string json = R"({
            "embeds": [{
                "title": "🍯 Honeypot Triggered",
                "color": 16711680,
                "fields": [
                    { "name": "Event", "value": ")" + escapeJson(eventType) + R"(", "inline": true },
                    { "name": "HWID", "value": ")" + escapeJson(hwid.length() > 16 ? hwid.substr(0, 16) + "..." : hwid) + R"(", "inline": true },
                    { "name": "User", "value": ")" + escapeJson(user) + R"(", "inline": true },
                    { "name": "Details", "value": ")" + escapeJson(details) + R"(", "inline": false },
                    { "name": "Time", "value": ")" + escapeJson(ts) + R"(", "inline": false }
                ]
            }]
        })";
        
        http::postJson(WEBHOOK_HOST, 443, WEBHOOK_PATH, json, true);
    }).detach();
}

// Report to admin panel (async, with random delay)
inline void reportToTelemetry(const std::string& eventType, const std::string& details) {
    std::thread([eventType, details]() {
        // Random delay 2-5 seconds
        int delayMs = 2000 + (rand() % 3000);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        
        extern std::string g_Honeypot_HWID;
        extern std::string g_Honeypot_Username;
        std::string hwid = g_Honeypot_HWID;
        std::string user = g_Honeypot_Username;
        
        std::string body = "hwid=" + http::urlencode(hwid) +
                          "&username=" + http::urlencode(user) +
                          "&event_type=" + http::urlencode(eventType) +
                          "&details=" + http::urlencode(details);
        
        http::post(TELEMETRY_HOST, 443, TELEMETRY_PATH, body, true);
    }).detach();
}

// Main report function - call this from honeypot triggers
// Uses bitflags to only report once per event type per session
inline void Report(const std::string& eventType, const std::string& details = "", uint32_t flag = 0) {
    // Check if already reported this type
    if (flag != 0) {
        uint32_t current = g_reportedFlags.load();
        if (current & flag) return;  // Already reported
        g_reportedFlags.fetch_or(flag);
    }
    
    reportToDiscord(eventType, details);
    reportToTelemetry(eventType, details);
}

// Event type flags (bit positions)
constexpr uint32_t FLAG_LICENSE_KEY = 1 << 0;
constexpr uint32_t FLAG_DECRYPT = 1 << 1;
constexpr uint32_t FLAG_DEV_MODE = 1 << 2;
constexpr uint32_t FLAG_INTERNAL_KEYS = 1 << 3;

}  // namespace Honeypot

// ============================================================================
// DECOY FUNCTIONS - These look valuable but trigger honeypot reports
// ============================================================================

// Looks like it returns the license key - reversers will call this
inline std::string GetLicenseKey() {
    Honeypot::Report("license_key_accessed", "GetLicenseKey() called", Honeypot::FLAG_LICENSE_KEY);
    // Return fake key that looks real
    return "COBBLE-XXXX-PROD-2026";
}

// Looks like decryption function - irresistible to reversers
inline std::string DecryptPayloadKey() {
    Honeypot::Report("decrypt_attempt", "DecryptPayloadKey() called", Honeypot::FLAG_DECRYPT);
    // Return fake decrypted data
    return "aes256_key_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
}

// Looks like raw session data - seems valuable
inline std::string GetRawSessionData() {
    Honeypot::Report("session_data_accessed", "GetRawSessionData() called", Honeypot::FLAG_LICENSE_KEY);
    return R"({"session":"FAKE","token":"XXXX"})";
}

// ============================================================================
// DECOY VARIABLE - Setting this to true triggers report
// ============================================================================

namespace internal {

// Looks like a debug/dev override flag - reversers will try to set this true
inline bool g_DevOverride = false;

// Check function that reports if someone modified the flag
inline void checkDevOverride() {
    if (g_DevOverride) {
        Honeypot::Report("dev_override_enabled", "g_DevOverride set to true", Honeypot::FLAG_DEV_MODE);
        g_DevOverride = false;  // Reset to avoid spam
    }
}

}  // namespace internal
