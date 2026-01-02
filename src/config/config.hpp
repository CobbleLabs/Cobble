#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <Windows.h>
#include <ShlObj.h>

#include "../util/json.hpp"
#include "../util/http.hpp"
#include "../util/hwid.hpp"
#include "../util/logger.hpp"

// Server configuration
#define CONFIG_SERVER_PRIMARY L"syra.club"
#define CONFIG_SERVER_FALLBACK L"172.235.136.152"
#define CONFIG_SERVER_PORT 8080
#define CONFIG_DEBOUNCE_MS 400

// Forward declarations
struct Module;
class ModuleManager;
extern ModuleManager* moduleManager;

class ConfigManager {
public:
    static ConfigManager& get() {
        static ConfigManager instance;
        return instance;
    }
    
    // Startup: load local → apply → async fetch server
    void init() {
        // Get HWID once at startup
        hwid_ = hwid::generate();
        if (hwid_.empty()) {
            log_warn("ConfigManager: Failed to generate HWID, sync disabled");
            syncEnabled_ = false;
        }
        
        // Get config file path (%APPDATA%/Cobble/config.json)
        configPath_ = getConfigPath();
        
        // Load local config (always first)
        loadLocal();
        
        // Apply to modules immediately (done by loadLocal -> applyConfig)
        
        // Async fetch from server (non-blocking)
        if (syncEnabled_) {
            std::thread([this] { fetchServerConfig(); }).detach();
        }
        
        log_ok("ConfigManager initialized");
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
    void save() {
        saveLocal();
    }
    
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
        if (configPath_.empty()) {
            log_warn("ConfigManager: No config path, using defaults");
            applyDefaults();
            return;
        }
        
        std::ifstream file(configPath_);
        if (!file.is_open()) {
            log_raw("ConfigManager: No local config found, using defaults");
            applyDefaults();
            saveLocal();  // Create default config file
            return;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        
        if (!json::Parser::parse(content, config_)) {
            log_warn("ConfigManager: Failed to parse config, using defaults");
            applyDefaults();
            return;
        }
        
        // Check version for migrations
        int version = config_["version"].as_int(0);
        if (version < CONFIG_VERSION) {
            log_raw("ConfigManager: Migrating config from v%d to v%d", version, CONFIG_VERSION);
            migrateConfig(version);
        }
        
        applyConfig();
        log_ok("ConfigManager: Loaded local config");
    }
    
    void saveLocal() {
        if (configPath_.empty()) return;
        
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
    
    void fetchServerConfig() {
        if (!syncEnabled_ || hwid_.empty()) return;
        
        std::wstring path = L"/api/config?hwid=" + 
            std::wstring(hwid_.begin(), hwid_.end());
        
        // Try primary server first, then fallback
        auto response = http::get(CONFIG_SERVER_PRIMARY, CONFIG_SERVER_PORT, path, false);
        if (!response.success) {
            log_debug("ConfigManager: Primary failed, trying fallback...");
            response = http::get(CONFIG_SERVER_FALLBACK, CONFIG_SERVER_PORT, path, false);
        }
        
        if (!response.success) {
            log_debug("ConfigManager: Server fetch failed: %s", response.error.c_str());
            return;
        }
        
        json::Value serverResponse;
        if (!json::Parser::parse(response.body, serverResponse)) {
            log_warn("ConfigManager: Invalid server response");
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
        if (!syncEnabled_ || hwid_.empty()) return;
        
        buildConfigFromModules();
        
        std::string configJson;
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            configJson = config_.serialize();
        }
        
        std::string body = "hwid=" + http::urlencode(hwid_) + 
                          "&config=" + http::urlencode(configJson);
        
        // Try primary server first, then fallback
        auto response = http::post(CONFIG_SERVER_PRIMARY, CONFIG_SERVER_PORT, 
                                   L"/api/config/save", body, false);
        if (!response.success) {
            response = http::post(CONFIG_SERVER_FALLBACK, CONFIG_SERVER_PORT, 
                                  L"/api/config/save", body, false);
        }
        
        if (response.success) {
            log_debug("ConfigManager: Synced to server");
        }
    }
    
    void enqueueSyncDebounced() {
        if (!syncEnabled_) return;
        
        lastChangeTime_ = std::chrono::steady_clock::now();
        
        if (!pendingSync_.exchange(true)) {
            std::thread([this] {
                std::this_thread::sleep_for(std::chrono::milliseconds(CONFIG_DEBOUNCE_MS));
                
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - lastChangeTime_).count();
                
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
    bool syncEnabled_ = false;  // Disabled by default, user enables in Settings
    std::atomic<bool> pendingSync_{false};
    std::atomic<bool> syncRunning_{true};
    std::chrono::steady_clock::time_point lastChangeTime_;
};
