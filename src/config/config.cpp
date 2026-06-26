#include "config.hpp"
#include "../module/module.h"
#include "../module/module_manager.h"
#include "../security/session_validator.hpp"
#include "../ui/hud/arraylist.h"
#include "../ui/hud/notifications.h"
#include "../ui/menu/menu.h"
#include "../ui/widgets/theme.hpp"
#include "../util/team/friends_manager.h"

#include <algorithm>
#include <cstring>
#include <cctype>

using namespace ArrayListRenderer;

namespace {
static std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::string sanitize_user_config_name(const std::string& in) {
    std::string s = trim_copy(in);
    std::string out;
    out.reserve(s.size());

    // Keep this intentionally strict to avoid path traversal and weird FS edge cases.
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
            c == '-') {
            out.push_back((char)c);
        }
    }

    out = trim_copy(out);
    if (out.size() > 48)  // keep filenames manageable
        out.resize(48);

    return out;
}

static bool ends_with_case_insensitive(const std::string& s, const char* suffix) {
    size_t n = s.size();
    size_t m = strlen(suffix);
    if (m > n)
        return false;
    for (size_t i = 0; i < m; i++) {
        char a = (char)std::tolower((unsigned char)s[n - m + i]);
        char b = (char)std::tolower((unsigned char)suffix[i]);
        if (a != b)
            return false;
    }
    return true;
}
}  // namespace

std::string ConfigManager::getUserConfigsDir() const {
    if (configPath_.empty())
        return "";

    std::string configDir = configPath_.substr(0, configPath_.find_last_of("\\/"));
    std::string userDir = configDir + "\\configs";

    CreateDirectoryA(userDir.c_str(), nullptr);  // Ensure directory exists
    return userDir;
}

std::vector<std::string> ConfigManager::listUserConfigs() const {
    std::vector<std::string> out;
    std::string dir = getUserConfigsDir();
    if (dir.empty())
        return out;

    std::string pattern = dir + "\\*.json";

    WIN32_FIND_DATAA ffd{};
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
        return out;

    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::string file = ffd.cFileName;
        if (!ends_with_case_insensitive(file, ".json"))
            continue;

        // Strip extension for display
        file.resize(file.size() - 5);
        if (file.empty())
            continue;

        // Only surface configs we can actually address with our name sanitizer.
        if (sanitize_user_config_name(file) != file)
            continue;

        out.push_back(file);
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);

    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        auto lower = [](unsigned char c) { return (unsigned char)std::tolower(c); };
        size_t n = (std::min)(a.size(), b.size());
        for (size_t i = 0; i < n; i++) {
            unsigned char ca = lower((unsigned char)a[i]);
            unsigned char cb = lower((unsigned char)b[i]);
            if (ca < cb)
                return true;
            if (ca > cb)
                return false;
        }
        return a.size() < b.size();
    });

    return out;
}

bool ConfigManager::saveUserConfig(const std::string& name, std::string* err) {
    std::string safe = sanitize_user_config_name(name);
    if (safe.empty()) {
        if (err)
            *err = "Invalid name";
        return false;
    }

    std::string dir = getUserConfigsDir();
    if (dir.empty()) {
        if (err)
            *err = "No config directory";
        return false;
    }

    std::string path = dir + "\\" + safe + ".json";

    // Snapshot current state into config_ and serialize.
    buildConfigFromModules();

    std::string content;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        content = config_.serialize();
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        if (err)
            *err = "Failed to open file for write";
        return false;
    }

    file << content;
    file.close();

    return true;
}

bool ConfigManager::loadUserConfig(const std::string& name, std::string* err) {
    std::string safe = sanitize_user_config_name(name);
    if (safe.empty()) {
        if (err)
            *err = "Invalid name";
        return false;
    }

    std::string dir = getUserConfigsDir();
    if (dir.empty()) {
        if (err)
            *err = "No config directory";
        return false;
    }

    std::string path = dir + "\\" + safe + ".json";

    std::ifstream file(path);
    if (!file.is_open()) {
        if (err)
            *err = "Config not found";
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    json::Value loaded;
    if (!json::Parser::parse(content, loaded)) {
        if (err)
            *err = "Failed to parse JSON";
        return false;
    }

    int version = loaded["version"].as_int(0);

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        config_ = loaded;
    }

    if (version < CONFIG_VERSION) {
        migrateConfig(version);
    }

    applyConfig();

    // Persist and (if enabled) sync like any other setting change.
    onSettingChanged();

    return true;
}

bool ConfigManager::deleteUserConfig(const std::string& name, std::string* err) {
    std::string safe = sanitize_user_config_name(name);
    if (safe.empty()) {
        if (err)
            *err = "Invalid name";
        return false;
    }

    std::string dir = getUserConfigsDir();
    if (dir.empty()) {
        if (err)
            *err = "No config directory";
        return false;
    }

    std::string path = dir + "\\" + safe + ".json";
    if (!DeleteFileA(path.c_str())) {
        if (err)
            *err = "Failed to delete file";
        return false;
    }

    return true;
}

// Serialize a portable, shareable snapshot of gameplay settings only.
// Reuses buildConfigFromModules() for a fresh snapshot, then copies the
// module nodes while stripping the three personal fields: per-module
// "keybind", AltManager's "spoofedName", and the whole top-level "friends"
// block (excluded simply by never copying it). "enabled" is kept in the
// payload but import ignores it — see importPublicConfig().
std::string ConfigManager::exportPublicConfig() {
    // Refresh config_ from live module state (locks configMutex_ internally).
    buildConfigFromModules();

    std::lock_guard<std::mutex> lock(configMutex_);

    json::Value pub;
    pub["version"] = PUBLIC_CONFIG_VERSION;

    if (config_.has("modules")) {
        const auto& modules = config_["modules"].object();
        for (const auto& kv : modules) {
            json::Value node = kv.second;  // deep copy of this module's node
            node.object().erase("keybind");
            if (kv.first == "AltManager")
                node.object().erase("spoofedName");
            pub["modules"][kv.first] = node;
        }
    }

    return pub.serialize();
}

// Apply a public config received as a JSON string. Non-destructive: applies
// each module's own settings via loadSettings() but NEVER touches enabled or
// keybind, so importing someone else's config can't switch combat modules on
// or remap the user's keys. Parsed through sehParse() because the input is
// untrusted (it comes from another user / the server).
bool ConfigManager::importPublicConfig(const std::string& jsonStr, std::string* err) {
    if (!moduleManager) {
        if (err)
            *err = "Modules not ready";
        return false;
    }

    json::Value parsed;
    if (!sehParse(jsonStr, parsed)) {
        if (err)
            *err = "Failed to parse config";
        return false;
    }

    if (!parsed.has("modules") || !parsed["modules"].is_object()) {
        if (err)
            *err = "Not a valid public config";
        return false;
    }

    const auto& modules = parsed["modules"].object();
    int applied = 0;
    for (const auto& kv : modules) {
        const std::string& name = kv.first;
        for (auto* mod : moduleManager->getModules()) {
            if (std::string(mod->name) != name)
                continue;
            // Settings only — enabled/keybind are intentionally left as-is.
            mod->loadSettings(kv.second);
            applied++;
            break;
        }
    }

    if (applied == 0) {
        if (err)
            *err = "No matching modules in config";
        return false;
    }

    // Persist the merged result and sync like any other setting change.
    onSettingChanged();
    return true;
}

// Verify a server {payload_b64, sig_b64} envelope and parse the inner payload.
// Mirrors the verification in fetchServerConfig(): Ed25519 over the raw payload
// bytes with the build-time public key. Any tamper/parse failure returns false.
bool ConfigManager::verifySignedEnvelope(const std::string& responseBody, json::Value& outPayload) {
    json::Value envelope;
    if (!json::Parser::parse(responseBody, envelope))
        return false;

    std::string payloadB64 = envelope["payload_b64"].as_string("");
    std::string sigB64 = envelope["sig_b64"].as_string("");
    if (payloadB64.empty() || sigB64.empty())
        return false;

    auto payloadBytes = b64decode(payloadB64);
    auto sigBytes = b64decode(sigB64);
    if (payloadBytes.empty() || sigBytes.size() != 64)
        return false;

    uint8_t pubkey[32];
    cobble_keys::GetPublicKey(pubkey);
    bool sigOk = tweetnacl::verify(sigBytes.data(), payloadBytes.data(), payloadBytes.size(), pubkey);
    SecureZeroMemory(pubkey, 32);
    if (!sigOk)
        return false;

    return json::Parser::parse(std::string(payloadBytes.begin(), payloadBytes.end()), outPayload);
}

// Publish the current gameplay settings as a named public config. Builds the
// stripped payload on the calling side, then POSTs on a detached worker so the
// UI never blocks on WinHTTP.
void ConfigManager::publishPublicConfigAsync(const std::string& name, const std::string& description) {
    // Use the live handshake session token (rotates each heartbeat), NOT the
    // cloud-sync token in sessionToken_ — that one is opt-in and often empty.
    std::string hwid = SessionValidator::snapshotToken(SessionValidator::g_HWID);
    std::string token = SessionValidator::snapshotToken(SessionValidator::g_SessionToken);
    if (hwid.empty() || token.empty()) {
        std::lock_guard<std::mutex> lock(browseMutex_);
        browseState_.status = BrowseState::Error;
        browseState_.message = "Not signed in";
        return;
    }

    std::string publicJson = exportPublicConfig();
    {
        std::lock_guard<std::mutex> lock(browseMutex_);
        browseState_.status = BrowseState::Loading;
        browseState_.message = "Publishing...";
    }

    std::string body = "{\"hwid\":\"" + http::json_escape(hwid) + "\",\"token\":\"" +
                       http::json_escape(token) + "\",\"name\":\"" + http::json_escape(name) +
                       "\",\"description\":\"" + http::json_escape(description) + "\",\"config\":" + publicJson + "}";

    std::thread([this, body] {
        auto response = http::post(cobble_config::GetServerHostW().c_str(), CONFIG_SERVER_PORT,
                                   OBFUSCATE_WSTRING(L"/api/configs/public/publish").c_str(), body, true);

        json::Value payload;
        bool verified = response.success && verifySignedEnvelope(response.body, payload);
        bool ok = verified && payload["success"].as_bool(false);

        {
            std::lock_guard<std::mutex> lock(browseMutex_);
            browseState_.status = ok ? BrowseState::Ready : BrowseState::Error;
            if (ok)
                browseState_.message = "Published";
            else if (!response.success)
                browseState_.message = "Publish failed (HTTP " + std::to_string(response.statusCode) + ")";
            else if (!verified)
                browseState_.message = "Publish failed (bad signature)";
            else
                browseState_.message = "Publish rejected: " + payload["message"].as_string("server error");
        }

        // Show the freshly-published config in the list right away.
        if (ok)
            refreshPublicConfigsAsync();
    }).detach();
}

// Fetch the public-config browse list. Result lands in browseState_.entries.
void ConfigManager::refreshPublicConfigsAsync() {
    std::string hwid = SessionValidator::snapshotToken(SessionValidator::g_HWID);
    std::string token = SessionValidator::snapshotToken(SessionValidator::g_SessionToken);
    if (hwid.empty() || token.empty()) {
        std::lock_guard<std::mutex> lock(browseMutex_);
        browseState_.status = BrowseState::Error;
        browseState_.message = "Not signed in";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(browseMutex_);
        browseState_.status = BrowseState::Loading;
        browseState_.message = "Loading...";
    }

    std::string body =
        "{\"hwid\":\"" + http::json_escape(hwid) + "\",\"token\":\"" + http::json_escape(token) + "\"}";

    std::thread([this, body] {
        auto response = http::post(cobble_config::GetServerHostW().c_str(), CONFIG_SERVER_PORT,
                                   OBFUSCATE_WSTRING(L"/api/configs/public/list").c_str(), body, true);

        json::Value payload;
        bool verified = response.success && verifySignedEnvelope(response.body, payload);
        bool ok = verified && payload["success"].as_bool(false);

        std::lock_guard<std::mutex> lock(browseMutex_);
        if (!ok) {
            browseState_.status = BrowseState::Error;
            if (!response.success)
                browseState_.message = "Load failed (HTTP " + std::to_string(response.statusCode) + ")";
            else if (!verified)
                browseState_.message = "Load failed (bad signature)";
            else
                browseState_.message = "Load rejected: " + payload["message"].as_string("server error");
            return;
        }

        browseState_.entries.clear();
        if (payload.has("configs")) {
            // Server sends an object keyed "0","1",... (client JSON has no arrays).
            for (const auto& kv : payload["configs"].object()) {
                const json::Value& e = kv.second;
                PublicConfigEntry entry;
                entry.id = e["id"].as_string("");
                entry.owner = e["owner"].as_string("");
                entry.name = e["name"].as_string("");
                entry.description = e["description"].as_string("");
                entry.downloads = e["downloads"].as_int(0);
                if (!entry.id.empty())
                    browseState_.entries.push_back(std::move(entry));
            }
        }
        // Most-downloaded first (map iteration order is unspecified).
        std::sort(browseState_.entries.begin(), browseState_.entries.end(),
                  [](const PublicConfigEntry& a, const PublicConfigEntry& b) { return a.downloads > b.downloads; });

        browseState_.status = BrowseState::Ready;
        browseState_.message = browseState_.entries.empty() ? "No configs found" : "";
    }).detach();
}

// Download one public config by id and apply it via importPublicConfig (which
// never toggles modules). Result/message lands in browseState_.
void ConfigManager::downloadAndApplyPublicConfigAsync(const std::string& id) {
    std::string hwid = SessionValidator::snapshotToken(SessionValidator::g_HWID);
    std::string token = SessionValidator::snapshotToken(SessionValidator::g_SessionToken);
    if (hwid.empty() || token.empty() || id.empty()) {
        std::lock_guard<std::mutex> lock(browseMutex_);
        browseState_.status = BrowseState::Error;
        browseState_.message = "Not signed in";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(browseMutex_);
        browseState_.status = BrowseState::Loading;
        browseState_.message = "Downloading...";
    }

    std::string body = "{\"hwid\":\"" + http::json_escape(hwid) + "\",\"token\":\"" +
                       http::json_escape(token) + "\",\"id\":\"" + http::json_escape(id) + "\"}";

    std::thread([this, body] {
        auto response = http::post(cobble_config::GetServerHostW().c_str(), CONFIG_SERVER_PORT,
                                   OBFUSCATE_WSTRING(L"/api/configs/public/get").c_str(), body, true);

        json::Value payload;
        bool verified = response.success && verifySignedEnvelope(response.body, payload);
        bool ok = verified && payload["success"].as_bool(false);

        std::string resultMsg;
        if (ok) {
            std::string cfg = payload["config"].as_string("");
            std::string err;
            if (importPublicConfig(cfg, &err))
                resultMsg = "Applied: " + payload["name"].as_string("config");
            else
                resultMsg = "Apply failed: " + err;
        } else if (!response.success) {
            resultMsg = "Download failed (HTTP " + std::to_string(response.statusCode) + ")";
        } else if (!verified) {
            resultMsg = "Download failed (bad signature)";
        } else {
            resultMsg = "Download rejected: " + payload["message"].as_string("server error");
        }

        std::lock_guard<std::mutex> lock(browseMutex_);
        browseState_.status = ok ? BrowseState::Ready : BrowseState::Error;
        browseState_.message = resultMsg;
    }).detach();
}


void ConfigManager::applyDefaults() {
    COBBLE_TRACE("applyDefaults:enter");
    std::lock_guard<std::mutex> lock(configMutex_);
    COBBLE_TRACE("applyDefaults:got mutex");

    // Reset to a clean object. A failed parse can leave config_ with partial
    // state whose destructor faults; sehResetValue catches that and
    // placement-new's a fresh Value over the corrupt bytes (intentional
    // small leak rather than tearing down javaw).
    sehResetValue(config_);
    COBBLE_TRACE("applyDefaults:config_ reset");

    config_["version"] = CONFIG_VERSION;
    COBBLE_TRACE("applyDefaults:version set");

    // Settings
    config_["settings"]["cloudSync"] = false;
    COBBLE_TRACE("applyDefaults:cloudSync set");

    // HUD
    config_["hud"]["arrayList"] = true;
    config_["hud"]["notifications"] = true;
    COBBLE_TRACE("applyDefaults:hud set");

    // Default module configs (all disabled with default keybinds)
    COBBLE_TRACE("applyDefaults:exit");
}

// Apply loaded config to modules
void ConfigManager::applyConfig() {
    if (!moduleManager)
        return;

    // Apply settings
    if (config_.has("settings")) {
        syncEnabled_ = config_["settings"]["cloudSync"].as_bool(false);
        Menu::menuKey = config_["settings"]["menuKey"].as_int(0x2D);  // VK_INSERT
        // Load accent color
        if (config_["settings"].has("accentColor")) {
            ui::theme::accentColor[0] = config_["settings"]["accentColor"]["r"].as_float(0.9412f);
            ui::theme::accentColor[1] = config_["settings"]["accentColor"]["g"].as_float(0.4667f);
            ui::theme::accentColor[2] = config_["settings"]["accentColor"]["b"].as_float(0.0549f);
            ui::theme::accentColor[3] = config_["settings"]["accentColor"]["a"].as_float(1.0f);
        }
    }

    // Apply HUD settings
    if (config_.has("hud")) {
        enabled = config_["hud"]["arrayList"].as_bool(true);
        rainbow = config_["hud"]["arrayListRainbow"].as_bool(true);
        colorBar = config_["hud"]["arrayListColorBar"].as_bool(true);
        showBackground = config_["hud"]["arrayListBackground"].as_bool(true);
        showInfo = config_["hud"]["arrayListShowInfo"].as_bool(true);
        sortByLength = config_["hud"]["arrayListSortByLength"].as_bool(true);
        scale = config_["hud"]["arrayListScale"].as_float(1.0f);
        rainbowSaturation = config_["hud"]["arrayListSaturation"].as_float(0.7f);
        anchor = config_["hud"]["arrayListAnchor"].as_int(0);
        NotificationManager::enabled = config_["hud"]["notifications"].as_bool(true);
    }

    // Apply friends settings
    if (config_.has("friends") && friendsManager) {
        // Load dynamic friend records. This preserves every entry and its UUID.
        if (config_["friends"].has("entries")) {
            friendsManager->clearFriends();
            const auto& entries = config_["friends"]["entries"].object();
            for (const auto& entry : entries) {
                const json::Value& data = entry.second;
                friendsManager->restoreFriend(data["uuid"].as_string(""), data["name"].as_string(""));
            }
        } else if (config_["friends"].has("list")) {
            // Backwards compatibility with older comma-separated configs.
            friendsManager->clearFriends();
            std::string friendsList = config_["friends"]["list"].as_string("");
            if (!friendsList.empty()) {
                size_t start = 0, end;
                while ((end = friendsList.find(',', start)) != std::string::npos) {
                    std::string name = friendsList.substr(start, end - start);
                    if (!name.empty())
                        friendsManager->addFriend(name);
                    start = end + 1;
                }
                std::string last = friendsList.substr(start);
                if (!last.empty())
                    friendsManager->addFriend(last);
            }
        }
        // Load team settings
        friendsManager->teams.enabled = config_["friends"]["teamsEnabled"].as_bool(true);
        friendsManager->teams.matchOwnNametag = config_["friends"]["matchOwnNametag"].as_bool(true);
        friendsManager->applyToCombat = config_["friends"]["applyToCombat"].as_bool(true);
        friendsManager->applyToVisual = config_["friends"]["applyToVisual"].as_bool(false);
        // Quick-add settings
        friendsManager->quickAddEnabled = config_["friends"]["quickAddEnabled"].as_bool(true);
        friendsManager->quickAddKey = config_["friends"]["quickAddKey"].as_int(0x04);  // VK_MBUTTON
    }

    // Apply module configs
    if (!config_.has("modules"))
        return;

    if (!config_["modules"].has("AntiDebuff") && config_["modules"].has("NoVisualEffects"))
        config_["modules"]["AntiDebuff"] = config_["modules"]["NoVisualEffects"];

    const auto& modules = config_["modules"].object();

    for (const auto& kv : modules) {
        const std::string& name = kv.first;
        const json::Value& data = kv.second;

        // Find module by name
        for (auto* mod : moduleManager->getModules()) {
            if (std::string(mod->name) != name)
                continue;

            // Apply base settings
            mod->keybind = data["keybind"].as_int(0);
            bool shouldEnable = data["enabled"].as_bool(false);
            if (shouldEnable != mod->enabled && mod->isAccessible()) {
                mod->setEnabled(shouldEnable);
            }

            // Each module deserializes its own keys into its settings.
            // Base "enabled"/"keybind" are already handled above; saveSettings()
            // owns the rest and is the single source of truth per module.
            mod->loadSettings(data);

            break;
        }
    }
}

// Build config from current module state
void ConfigManager::buildConfigFromModules() {
    if (!moduleManager)
        return;

    std::lock_guard<std::mutex> lock(configMutex_);

    config_["version"] = CONFIG_VERSION;

    // Settings
    config_["settings"]["cloudSync"] = syncEnabled_;
    config_["settings"]["menuKey"] = Menu::menuKey;
    // Save accent color
    config_["settings"]["accentColor"]["r"] = ui::theme::accentColor[0];
    config_["settings"]["accentColor"]["g"] = ui::theme::accentColor[1];
    config_["settings"]["accentColor"]["b"] = ui::theme::accentColor[2];
    config_["settings"]["accentColor"]["a"] = ui::theme::accentColor[3];

    // HUD
    config_["hud"]["arrayList"] = enabled;
    config_["hud"]["arrayListRainbow"] = rainbow;
    config_["hud"]["arrayListColorBar"] = colorBar;
    config_["hud"]["arrayListBackground"] = showBackground;
    config_["hud"]["arrayListShowInfo"] = showInfo;
    config_["hud"]["arrayListSortByLength"] = sortByLength;
    config_["hud"]["arrayListScale"] = scale;
    config_["hud"]["arrayListSaturation"] = rainbowSaturation;
    config_["hud"]["arrayListAnchor"] = anchor;
    config_["hud"]["notifications"] = NotificationManager::enabled;

    // Friends
    if (friendsManager) {
        auto friendRecords = friendsManager->getFriends();
        config_["friends"]["entries"] = json::Value();
        for (size_t i = 0; i < friendRecords.size(); ++i) {
            const std::string key = std::to_string(i);
            config_["friends"]["entries"][key]["uuid"] = friendRecords[i].uuid;
            config_["friends"]["entries"][key]["name"] = friendRecords[i].displayName;
        }

        auto friends = friendsManager->getFriendNames();
        std::string friendsList;
        for (size_t i = 0; i < friends.size(); ++i) {
            if (i > 0)
                friendsList += ",";
            friendsList += friends[i];
        }
        config_["friends"]["list"] = friendsList;
        config_["friends"]["teamsEnabled"] = friendsManager->teams.enabled;
        config_["friends"]["matchOwnNametag"] = friendsManager->teams.matchOwnNametag;
        config_["friends"]["applyToCombat"] = friendsManager->applyToCombat;
        config_["friends"]["applyToVisual"] = friendsManager->applyToVisual;
        // Quick-add settings
        config_["friends"]["quickAddEnabled"] = friendsManager->quickAddEnabled;
        config_["friends"]["quickAddKey"] = friendsManager->quickAddKey;
    }

    // Build modules config
    for (auto* mod : moduleManager->getModules()) {
        std::string name = mod->name;

        config_["modules"][name]["enabled"] = mod->enabled;
        config_["modules"][name]["keybind"] = mod->keybind;

        // Each module serializes its own keys into its node. Base
        // "enabled"/"keybind" were written above; saveSettings() owns the
        // rest and is the single source of truth per module.
        mod->saveSettings(config_["modules"][name]);
    }
}

// Migrate config from older version
void ConfigManager::migrateConfig(int fromVersion) {
    std::lock_guard<std::mutex> lock(configMutex_);

    // Add migrations here as needed
    // if (fromVersion < 2) { ... }

    config_["version"] = CONFIG_VERSION;
}
