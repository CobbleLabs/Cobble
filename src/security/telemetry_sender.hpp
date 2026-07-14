#pragma once

// ============================================================================
// TELEMETRY SENDER - shared async post to the license server
// Both Honeypot and AuditReporter build their own JSON payloads and ship
// them through here. Fire-and-forget, jittered, never throws.
// ============================================================================

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "../util/network/http.hpp"
#include "../util/aes_string.hpp"
#include "telemetry_host.hpp"

namespace telemetry {

// Sends a JSON payload to the server asynchronously with a small random
// delay (100-500ms) to avoid timing correlation. Defaults to /api/telemetry;
// callers needing the detailed audit endpoint can pass /api/internal/audit.
inline void send_async(const std::string& json_body, const std::wstring& path) {
    std::thread([json_body, path]() {
        try {
            auto delay = 100 + (std::rand() % 400);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            http::post_cobble(path, json_body, 443);
        } catch (...) {
            // Telemetry must never crash the host process.
        }
    }).detach();
}
inline void send_async(const std::string& json_body) {
    send_async(json_body, OBFUSCATE_WSTRING(L"/api/telemetry"));
}

}  // namespace telemetry
