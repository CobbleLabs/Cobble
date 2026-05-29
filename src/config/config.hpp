#pragma once

#include <ShlObj.h>
#include <Windows.h>
#include <wincrypt.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "../util/hwid.hpp"
#include "../util/inject_trace.hpp"
#include "../util/logger.hpp"
#include "../util/aes_string.hpp"
#include "../util/network/http.hpp"
#include "../util/network/json.hpp"
#include "../security/tweetnacl.h"
#include "generated/keys.h"
#include "generated/server_config.h"

#pragma comment(lib, "crypt32.lib")

// Server configuration
#define CONFIG_SERVER_PORT 443
#define CONFIG_DEBOUNCE_MS 400

// Forward declarations
struct Module;
class ModuleManager;
extern ModuleManager* moduleManager;

// SEH-protected parse helper. Lives at namespace scope (and has no C++ locals
// with destructors) because MSVC forbids __try inside functions that need
// stack unwinding. Catches hard faults (AV, stack overflow) that C++
// try/catch can't see, so a corrupted config can't tear down javaw.
inline bool sehParse(const std::string& content, json::Value& out) {
    bool ok = false;
    __try {
        ok = json::Parser::parse(content, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// Reset config_ to an empty json::Value WITHOUT running the destructor on the
// previous state. A failed parse can leave the internal std::map in a state
// whose destructor faults (corrupt node pointers from a partial walk).
// Placement-new overwrites the bytes with a fresh Value, intentionally
// leaking whatever memory the broken state was holding — better than
// crashing the host process. Acceptable: this path runs once at startup.
inline void sehResetValue(json::Value& target) {
    new (&target) json::Value();
}

class ConfigManager {
public:
    static ConfigManager& get() {
        static ConfigManager instance;
        return instance;
    }

    // Startup: load local â†’ apply â†’ async fetch server
    void init() {
        COBBLE_TRACE("ConfigMgr:init enter");
        // Get HWID once at startup
        COBBLE_TRACE("ConfigMgr:before hwid::generate");
        hwid_ = hwid::generate();
        COBBLE_TRACE("ConfigMgr:after hwid::generate");
        if (hwid_.empty()) {
            log_warn("ConfigManager: Failed to generate HWID, sync disabled");
            syncEnabled_ = false;
        }

        // Get config file path (%APPDATA%/Cobble/config.json)
        COBBLE_TRACE("ConfigMgr:before getConfigPath");
        configPath_ = getConfigPath();
        COBBLE_TRACE("ConfigMgr:after getConfigPath");

        // Load session token from loader (for cloud sync auth)
        COBBLE_TRACE("ConfigMgr:before loadSessionToken");
        loadSessionToken();
        COBBLE_TRACE("ConfigMgr:after loadSessionToken");

        // Load local config (always first)
        COBBLE_TRACE("ConfigMgr:before loadLocal");
        loadLocal();
        COBBLE_TRACE("ConfigMgr:after loadLocal");

        // Apply to modules immediately (done by loadLocal -> applyConfig)

        // Async fetch from server (non-blocking)
        if (syncEnabled_ && !sessionToken_.empty()) {
            COBBLE_TRACE("ConfigMgr:spawning fetchServerConfig thread");
            std::thread([this] { fetchServerConfig(); }).detach();
        }

        log_ok("ConfigManager initialized");
        COBBLE_TRACE("ConfigMgr:init exit");
    }

    // Shutdown: save config
    void shutdown() {
        syncRunning_ = false;
        saveLocal();
        log_raw("ConfigManager shutdown");
    }

    // Called when any module setting changes (from UI)
    void onSettingChanged() {
        saveLocal();
        enqueueSyncDebounced();
    }

    // Force save (called periodically or on explicit save)
    void save() { saveLocal(); }

    // User configs (stored under %APPDATA%/Cobble/configs/*.json)
    std::string getUserConfigsDir() const;
    std::vector<std::string> listUserConfigs() const;
    bool saveUserConfig(const std::string& name, std::string* err = nullptr);
    bool loadUserConfig(const std::string& name, std::string* err = nullptr);
    bool deleteUserConfig(const std::string& name, std::string* err = nullptr);

    // Get current config as JSON (for debugging)
    std::string getConfigJson() const {
        std::lock_guard<std::mutex> lock(configMutex_);
        return config_.serialize();
    }

    // Cloud sync settings
    bool isSyncEnabled() const { return syncEnabled_; }
    void setSyncEnabled(bool enabled) {
        syncEnabled_ = enabled;
        // If just enabled, try to sync now
        if (enabled && !hwid_.empty()) {
            std::thread([this] { fetchServerConfig(); }).detach();
        }
    }

private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    std::string getConfigPath() {
        wchar_t* appData = nullptr;
        if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData) != S_OK) {
            return "";
        }

        std::wstring path(appData);
        CoTaskMemFree(appData);

        path += L"\\Cobble";
        CreateDirectoryW(path.c_str(), nullptr);  // Ensure directory exists

        path += L"\\config.json";

        // Convert to narrow string
        int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &result[0], size, nullptr, nullptr);

        return result;
    }

    void loadLocal() {
        COBBLE_TRACE("loadLocal:enter");
        if (configPath_.empty()) {
            COBBLE_TRACE("loadLocal:no path -> applyDefaults");
            log_warn("ConfigManager: No config path, using defaults");
            applyDefaults();
            COBBLE_TRACE("loadLocal:applyDefaults done (no path)");
            return;
        }

        COBBLE_TRACE("loadLocal:opening ifstream");
        std::ifstream file(configPath_);
        COBBLE_TRACE("loadLocal:after ifstream ctor");
        if (!file.is_open()) {
            COBBLE_TRACE("loadLocal:file not open -> applyDefaults");
            log_raw("ConfigManager: No local config found, using defaults");
            applyDefaults();
            COBBLE_TRACE("loadLocal:applyDefaults done (no file)");
            saveLocal();  // Create default config file
            COBBLE_TRACE("loadLocal:saveLocal done (no file)");
            return;
        }

        COBBLE_TRACE("loadLocal:reading file content");
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

#ifdef COBBLE_DEBUG
        // Dump enough about the file content that, if the parser hard-faults,
        // we can see what input triggered it.
        {
            char sizeMsg[64];
            wsprintfA(sizeMsg, "loadLocal:content size=%lu", (unsigned long)content.size());
            COBBLE_TRACE(sizeMsg);

            // First 120 chars (truncated, NUL-terminated)
            char head[160] = {0};
            size_t hn = content.size() < 120 ? content.size() : 120;
            for (size_t i = 0; i < hn; ++i) {
                char c = content[i];
                head[i] = (c >= 32 && c < 127) ? c : '?';
            }
            COBBLE_TRACE(head);

            // Last 120 chars
            if (content.size() > 120) {
                char tail[160] = {0};
                size_t off = content.size() - 120;
                for (size_t i = 0; i < 120; ++i) {
                    char c = content[off + i];
                    tail[i] = (c >= 32 && c < 127) ? c : '?';
                }
                COBBLE_TRACE(tail);
            }
        }
#endif

        COBBLE_TRACE("loadLocal:content read, parsing");

        // Belt-and-suspenders: parse() already catches C++ exceptions, but a
        // hard fault (AV from corrupted memory, stack overflow on deep
        // recursion, etc.) bypasses C++ try/catch entirely. Route through a
        // helper that wraps with SEH so a malformed config NEVER tears down
        // javaw — at worst we fall back to defaults.
        bool parseOk = sehParse(content, config_);

        if (!parseOk) {
            COBBLE_TRACE("loadLocal:parse failed -> applyDefaults");
            log_warn("ConfigManager: Failed to parse config, using defaults");
            applyDefaults();
            COBBLE_TRACE("loadLocal:applyDefaults done (parse fail)");
            return;
        }
        COBBLE_TRACE("loadLocal:parse ok");

        // Check version for migrations
        int version = config_["version"].as_int(0);
        if (version < CONFIG_VERSION) {
            COBBLE_TRACE("loadLocal:migrating");
            log_raw("ConfigManager: Migrating config from v%d to v%d", version, CONFIG_VERSION);
            migrateConfig(version);
            COBBLE_TRACE("loadLocal:migrate done");
        }

        COBBLE_TRACE("loadLocal:before applyConfig");
        applyConfig();
        COBBLE_TRACE("loadLocal:after applyConfig");
        log_ok("ConfigManager: Loaded local config");
    }

    void saveLocal() {
        if (configPath_.empty())
            return;

        // Build config from current module state
        buildConfigFromModules();

        std::lock_guard<std::mutex> lock(configMutex_);
        std::string content = config_.serialize();

        std::ofstream file(configPath_);
        if (file.is_open()) {
            file << content;
            file.close();
        }
    }

    // Base64 decode helper (WinCrypt)
    static std::vector<uint8_t> b64decode(const std::string& input) {
        DWORD outLen = 0;
        if (!CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, nullptr, &outLen, nullptr, nullptr))
            return {};
        std::vector<uint8_t> output(outLen);
        if (!CryptStringToBinaryA(input.c_str(), (DWORD)input.length(), CRYPT_STRING_BASE64, output.data(), &outLen, nullptr, nullptr))
            return {};
        output.resize(outLen);
        return output;
    }

    void fetchServerConfig() {
        if (!syncEnabled_ || hwid_.empty() || sessionToken_.empty())
            return;

        std::string body = "{\"hwid\":\"" + http::json_escape(hwid_) +
                           "\",\"token\":\"" + http::json_escape(sessionToken_) + "\"}";

        // HTTPS only - encrypted traffic
        auto response = http::post(cobble_config::GetServerHostW().c_str(), CONFIG_SERVER_PORT, OBFUSCATE_WSTRING(L"/api/config").c_str(), body, true);

        if (!response.success) {
            log_debug("ConfigManager: Server fetch failed: %s", response.error.c_str());
            return;
        }

        // Parse signed envelope
        json::Value envelope;
        if (!json::Parser::parse(response.body, envelope)) {
            log_warn("ConfigManager: Invalid server response");
            return;
        }

        std::string payloadB64 = envelope["payload_b64"].as_string("");
        std::string sigB64 = envelope["sig_b64"].as_string("");

        if (payloadB64.empty() || sigB64.empty()) {
            log_warn("ConfigManager: Missing signature - response tampered");
            return;
        }

        auto payloadBytes = b64decode(payloadB64);
        auto sigBytes = b64decode(sigB64);

        if (payloadBytes.empty() || sigBytes.size() != 64) {
            log_warn("ConfigManager: Invalid sig format");
            return;
        }

        uint8_t pubkey[32];
        cobble_keys::GetPublicKey(pubkey);
        bool sigOk = tweetnacl::verify(sigBytes.data(), payloadBytes.data(), payloadBytes.size(), pubkey);
        SecureZeroMemory(pubkey, 32);

        if (!sigOk) {
            log_warn("ConfigManager: Signature invalid - response tampered");
            return;
        }

        // Parse verified inner payload
        json::Value serverResponse;
        if (!json::Parser::parse(std::string(payloadBytes.begin(), payloadBytes.end()), serverResponse)) {
            log_warn("ConfigManager: Invalid payload JSON");
            return;
        }

        if (!serverResponse["success"].as_bool(false)) {
            log_debug("ConfigManager: Server returned failure");
            return;
        }

        std::string serverConfig = serverResponse["config"].as_string("{}");
        if (serverConfig.empty() || serverConfig == "{}") {
            log_debug("ConfigManager: No server config stored");
            return;
        }

        // Parse and merge server config
        json::Value serverData;
        if (json::Parser::parse(serverConfig, serverData)) {
            mergeServerConfig(serverData);
            applyConfig();
            saveLocal();  // Cache merged result
            log_ok("ConfigManager: Applied server config");
        }
    }

    void pushServerConfig() {
        if (!syncEnabled_ || hwid_.empty())
            return;

        buildConfigFromModules();

        std::string configJson;
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            configJson = config_.serialize();
        }

        // Server expects: { "hwid": ..., "token": ..., "config": <raw JSON> }
        // configJson is already a JSON object string, so embed it as-is.
        std::string body = "{\"hwid\":\"" + http::json_escape(hwid_) +
                           "\",\"token\":\"" + http::json_escape(sessionToken_) +
                           "\",\"config\":" + configJson + "}";

        // HTTPS only - encrypted traffic
        auto response = http::post(cobble_config::GetServerHostW().c_str(), CONFIG_SERVER_PORT, OBFUSCATE_WSTRING(L"/api/config/save").c_str(), body, true);

        if (!response.success) {
            log_debug("ConfigManager: Sync push failed");
            return;
        }

        // Verify signed envelope (fail silently on tampered response)
        json::Value envelope;
        if (!json::Parser::parse(response.body, envelope)) return;

        std::string payloadB64 = envelope["payload_b64"].as_string("");
        std::string sigB64 = envelope["sig_b64"].as_string("");
        if (payloadB64.empty() || sigB64.empty()) {
            log_warn("ConfigManager: Save response missing signature");
            return;
        }

        auto payloadBytes = b64decode(payloadB64);
        auto sigBytes = b64decode(sigB64);
        if (payloadBytes.empty() || sigBytes.size() != 64) return;

        uint8_t pubkey[32];
        cobble_keys::GetPublicKey(pubkey);
        bool sigOk = tweetnacl::verify(sigBytes.data(), payloadBytes.data(), payloadBytes.size(), pubkey);
        SecureZeroMemory(pubkey, 32);

        if (sigOk) {
            log_debug("ConfigManager: Synced to server");
        } else {
            log_warn("ConfigManager: Save response signature invalid");
        }
    }

    void enqueueSyncDebounced() {
        if (!syncEnabled_)
            return;

        lastChangeTime_ = std::chrono::steady_clock::now();

        if (!pendingSync_.exchange(true)) {
            std::thread([this] {
                std::this_thread::sleep_for(std::chrono::milliseconds(CONFIG_DEBOUNCE_MS));

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                     lastChangeTime_)
                                   .count();

                // Only sync if no changes in last DEBOUNCE_MS
                if (elapsed >= CONFIG_DEBOUNCE_MS) {
                    pushServerConfig();
                }

                pendingSync_ = false;
            }).detach();
        }
    }

    void mergeServerConfig(const json::Value& serverData) {
        std::lock_guard<std::mutex> lock(configMutex_);

        // Server data overwrites local for modules
        if (serverData.has("modules")) {
            const auto& serverModules = serverData["modules"].object();
            for (const auto& kv : serverModules) {
                config_["modules"][kv.first] = kv.second;
            }
        }

        // Flags always come from server
        if (serverData.has("flags")) {
            config_["flags"] = serverData["flags"];
        }

        // HUD settings from server
        if (serverData.has("hud")) {
            config_["hud"] = serverData["hud"];
        }
    }

    void applyDefaults();
    void applyConfig();
    void buildConfigFromModules();
    void migrateConfig(int fromVersion);

    static constexpr int CONFIG_VERSION = 1;

    mutable std::mutex configMutex_;
    json::Value config_;
    std::string configPath_;
    std::string hwid_;
    std::string sessionToken_;
    bool syncEnabled_ = false;  // Disabled by default, user enables in Settings
    std::atomic<bool> pendingSync_{false};
    std::atomic<bool> syncRunning_{true};
    std::chrono::steady_clock::time_point lastChangeTime_;

    void loadSessionToken() {
        if (configPath_.empty())
            return;

        // Session file is in same directory as config
        std::string configDir = configPath_.substr(0, configPath_.find_last_of("\\/"));
        std::string sessionPath = configDir + "\\.session";

        std::ifstream sessionFile(sessionPath);
        if (sessionFile.is_open()) {
            std::getline(sessionFile, sessionToken_);
            sessionFile.close();

            // Delete file after reading (one-time use, stays in memory)
            DeleteFileA(sessionPath.c_str());

            if (!sessionToken_.empty()) {
                log_debug("ConfigManager: Loaded session token");
            }
        }
    }
};
