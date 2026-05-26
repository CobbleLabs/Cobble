#pragma once

// ============================================================================
// HONEYPOT SYSTEM - SERVER-SIDE TELEMETRY
// Decoy functions and variables that look valuable to reversers.
// When accessed, they report to the LICENSE SERVER (not Discord directly).
// The server then forwards to Discord, keeping webhooks secure.
// ============================================================================

#include <Windows.h>
#include <chrono>
#include <ctime>
#include <string>
#include <thread>

#include "../util/network/http.hpp"
#include "telemetry_host.hpp"
#include "telemetry_sender.hpp"

namespace Honeypot {

// ============================================================================
// REAL TELEMETRY - Goes through our server
// ============================================================================
inline const wchar_t* TELEMETRY_PATH = L"/api/telemetry";

// ============================================================================
// FAKE HONEYPOT WEBHOOK - INTENTIONALLY EASY TO FIND
// If someone extracts this and uses it, we'll know they cracked the binary
// DO NOT USE THIS - It's a decoy!
// ============================================================================
inline const wchar_t* FAKE_WEBHOOK_HOST = L"discordapp.com";
inline const wchar_t* FAKE_WEBHOOK_PATH =
    L"/api/webhooks/1461847086676050022/VoosC-k5C0kbXtj41qYlMlVfCs00_NssoFQh89wj4Wm29ThH-LLJns_oMT4Rl86Ui_M6";

// Prevent spam - only report once per type per session
inline std::atomic<uint32_t> g_reportedFlags{0};

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
        else
            out += c;
    }
    return out;
}

// Report to server (async, with random delay) - the server forwards to Discord
inline void reportToServer(const std::string& eventType, const std::string& details,
                           const std::string& severity = "critical") {
    // Access globals from session_validator.hpp (included via cobble.cpp)
    extern std::string g_Honeypot_HWID;
    extern std::string g_Honeypot_Username;
    std::string hwid = g_Honeypot_HWID;
    std::string user = g_Honeypot_Username;

    // Build JSON payload for server
    std::string json = "{";
    json += "\"hwid\":\"" + escapeJson(hwid) + "\",";
    json += "\"username\":\"" + escapeJson(user) + "\",";
    json += "\"event_code\":\"HONEYPOT_" + escapeJson(eventType) + "\",";
    json += "\"severity\":\"" + escapeJson(severity) + "\",";
    json += "\"score\":100,";
    json += "\"details\":\"" + escapeJson(details) + "\",";
    json += "\"source\":\"dll\"";
    json += "}";

    telemetry::send_async(json, TELEMETRY_PATH);
}

// Main report function - call this from honeypot triggers
// Uses bitflags to only report once per event type per session
inline void Report(const std::string& eventType, const std::string& details = "", uint32_t flag = 0) {
    // Check if already reported this type
    if (flag != 0) {
        uint32_t current = g_reportedFlags.load();
        if (current & flag)
            return;  // Already reported
        g_reportedFlags.fetch_or(flag);
    }

    reportToServer(eventType, details);
}

// Event type flags (bit positions)
constexpr uint32_t FLAG_LICENSE_KEY = 1 << 0;
constexpr uint32_t FLAG_DECRYPT = 1 << 1;
constexpr uint32_t FLAG_DEV_MODE = 1 << 2;
constexpr uint32_t FLAG_INTERNAL_KEYS = 1 << 3;
constexpr uint32_t FLAG_FAKE_WEBHOOK = 1 << 4;

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
// DECOY WEBHOOK FUNCTION - This looks like the real Discord sender
// If called, it reports to our server that someone found the fake webhook
// ============================================================================
inline void SendToDiscordWebhook(const std::string& message) {
    Honeypot::Report("fake_webhook_called", "Cracker tried to use extracted webhook: " + message.substr(0, 100),
                     Honeypot::FLAG_FAKE_WEBHOOK);
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
        g_DevOverride = false;
    }
}

}  // namespace internal
