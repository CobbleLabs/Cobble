#include "friends_manager.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include "../../sdk/core/minecraft.h"
#include "../../ui/hud/notifications.h"
#include "../logger.hpp"
#include "../network/http.hpp"
#include "../network/json.hpp"

FriendsManager* friendsManager = nullptr;

// ============================================================
// Mojang API UUID Resolution
// ============================================================

// Resolve a Minecraft username to UUID via Mojang API
// Returns empty string if not found or error
static std::string resolveMojangUUID(const std::string& username) {
    if (username.empty())
        return "";

    // GET https://api.mojang.com/users/profiles/minecraft/{username}
    std::wstring path = L"/users/profiles/minecraft/" + std::wstring(username.begin(), username.end());

    auto response = http::get(OBFUSCATE_WSTRING(L"api.mojang.com").c_str(), 443, path, true);  // HTTPS

    if (!response.success || response.statusCode != 200) {
        log_debug("[Friends] Mojang API failed for '%s': %s (status: %d)", username.c_str(), response.error.c_str(),
                  response.statusCode);
        return "";
    }

    // Parse JSON response: {"id": "UUID_WITHOUT_DASHES", "name": "ActualName"}
    json::Value root;
    if (!json::Parser::parse(response.body, root)) {
        log_debug("[Friends] Failed to parse Mojang response for '%s'", username.c_str());
        return "";
    }

    std::string rawUUID = root["id"].as_string();
    if (rawUUID.empty() || rawUUID.length() != 32) {
        return "";
    }

    // Convert to standard UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::string uuid = rawUUID.substr(0, 8) + "-" + rawUUID.substr(8, 4) + "-" + rawUUID.substr(12, 4) + "-" +
                       rawUUID.substr(16, 4) + "-" + rawUUID.substr(20, 12);

    log_debug("[Friends] Mojang resolved '%s' -> %s", username.c_str(), uuid.c_str());
    return uuid;
}

// Resolve a UUID to real Minecraft username via Mojang session server
// Returns empty string if not found or error
// API: GET https://sessionserver.mojang.com/session/minecraft/profile/{uuid-no-dashes}
static std::string resolveMojangUsername(const std::string& uuid) {
    if (uuid.empty())
        return "";

    // Remove dashes from UUID for API call
    std::string cleanUUID = uuid;
    cleanUUID.erase(std::remove(cleanUUID.begin(), cleanUUID.end(), '-'), cleanUUID.end());

    // GET https://sessionserver.mojang.com/session/minecraft/profile/{uuid}
    std::wstring path = L"/session/minecraft/profile/" + std::wstring(cleanUUID.begin(), cleanUUID.end());

    log_debug("[Friends] Resolving UUID %s via Mojang...", uuid.c_str());

    auto response = http::get(OBFUSCATE_WSTRING(L"sessionserver.mojang.com").c_str(), 443, path, true);  // HTTPS

    if (!response.success || response.statusCode != 200) {
        log_debug("[Friends] Mojang session API failed for UUID '%s': %s (status: %d)", uuid.c_str(),
                  response.error.c_str(), response.statusCode);
        return "";
    }

    // Parse JSON response: {"id": "UUID", "name": "RealUsername", ...}
    json::Value root;
    if (!json::Parser::parse(response.body, root)) {
        log_debug("[Friends] Failed to parse Mojang session response for '%s'", uuid.c_str());
        return "";
    }

    std::string realName = root["name"].as_string();
    if (realName.empty()) {
        log_debug("[Friends] No name in Mojang response for UUID '%s'", uuid.c_str());
        return "";
    }

    log_debug("[Friends] Mojang resolved UUID %s -> '%s'", uuid.c_str(), realName.c_str());
    return realName;
}

// ============================================================
// Utility
// ============================================================

std::string FriendsManager::toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string FriendsManager::stripColorCodes(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size();) {
        unsigned char c = input[i];

        // Check for UTF-8 § (0xC2 0xA7)
        if (c == 0xC2 && i + 1 < input.size() && (unsigned char)input[i + 1] == 0xA7) {
            // Skip the § (2 bytes) and the following color code character
            i += 2;
            if (i < input.size()) {
                // Skip the color code char (0-9, a-f, k-o, r)
                i++;
            }
            continue;
        }

        // Check for corrupted encoding pattern: ┬º (0xE2 0x94 0xAC followed by 0xC2 0xBA or similar)
        // This happens when § (0xC2 0xA7) is double-encoded or misinterpreted
        // Pattern: ┬ = 0xE2 0x94 0xAC (Box Drawing Light Down And Horizontal)
        if (c == 0xE2 && i + 2 < input.size() && (unsigned char)input[i + 1] == 0x94 &&
            (unsigned char)input[i + 2] == 0xAC) {
            // Skip ┬ (3 bytes)
            i += 3;
            // Check for º (0xC2 0xBA) - masculine ordinal indicator
            if (i + 1 < input.size() && (unsigned char)input[i] == 0xC2 && (unsigned char)input[i + 1] == 0xBA) {
                // Skip º (2 bytes) and the color code
                i += 2;
                if (i < input.size()) {
                    // Skip the color code char
                    i++;
                }
            }
            continue;
        }

        // Alternative corruption: just º followed by color code
        // (partial corruption of §)
        if (c == 0xC2 && i + 1 < input.size() && (unsigned char)input[i + 1] == 0xBA) {
            // Skip º and see if next is a color code
            i += 2;
            if (i < input.size()) {
                char next = std::tolower(input[i]);
                if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') || (next >= 'k' && next <= 'o') ||
                    next == 'r') {
                    i++;  // Skip the color code
                }
            }
            continue;
        }

        // Check for & as color prefix (some mods use this)
        if (c == '&' && i + 1 < input.size()) {
            char next = std::tolower(input[i + 1]);
            if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') || (next >= 'k' && next <= 'o') ||
                next == 'r') {
                // Skip & and the code
                i += 2;
                continue;
            }
        }

        // Check for raw § byte (sometimes seen in corrupted encoding)
        // Latin-1 § = 0xA7 as single byte
        if (c == 0xA7 && i + 1 < input.size()) {
            // Skip § and the color code
            i += 2;
            continue;
        }

        // Normal character, keep it
        result += c;
        i++;
    }

    return result;
}

std::string FriendsManager::stripFormatCodes(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    // Format codes to remove: k (obfuscated), l (bold), m (strikethrough), n (underline), o (italic), r (reset)
    // Color codes to KEEP: 0-9, a-f
    auto isFormatCode = [](char c) -> bool {
        c = std::tolower(c);
        return (c == 'k' || c == 'l' || c == 'm' || c == 'n' || c == 'o' || c == 'r');
    };

    for (size_t i = 0; i < input.size();) {
        unsigned char c = input[i];

        // Check for UTF-8 § (0xC2 0xA7)
        if (c == 0xC2 && i + 2 < input.size() && (unsigned char)input[i + 1] == 0xA7) {
            char codeChar = input[i + 2];
            if (isFormatCode(codeChar)) {
                // Skip the format code (§ + code = 3 bytes in UTF-8)
                i += 3;
                continue;
            } else {
                // Keep the color code
                result += input[i];
                result += input[i + 1];
                result += input[i + 2];
                i += 3;
                continue;
            }
        }

        // Check for & as color prefix (some mods use this)
        if (c == '&' && i + 1 < input.size()) {
            char codeChar = input[i + 1];
            if (isFormatCode(codeChar)) {
                // Skip the format code (& + code = 2 bytes)
                i += 2;
                continue;
            } else {
                char next = std::tolower(codeChar);
                if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f')) {
                    // Keep the color code
                    result += input[i];
                    result += input[i + 1];
                    i += 2;
                    continue;
                }
            }
        }

        // Check for raw § byte (Latin-1)
        if (c == 0xA7 && i + 1 < input.size()) {
            char codeChar = input[i + 1];
            if (isFormatCode(codeChar)) {
                // Skip the format code
                i += 2;
                continue;
            } else {
                // Keep the color code
                result += input[i];
                result += input[i + 1];
                i += 2;
                continue;
            }
        }

        // Normal character, keep it
        result += c;
        i++;
    }

    return result;
}

ChatColor FriendsManager::extractColor(const std::string& displayName) {
    // Look for color code pattern: § followed by 0-9 or a-f
    // Also handle & as alternate color code char (some mods)

    for (size_t i = 0; i + 1 < displayName.size(); i++) {
        char c = displayName[i];

        // Check for § (UTF-8: 0xC2 0xA7) or &
        bool isColorPrefix = false;
        if ((unsigned char)c == 0xC2 && i + 2 < displayName.size() && (unsigned char)displayName[i + 1] == 0xA7) {
            // UTF-8 § character
            i++;  // skip to the A7
            isColorPrefix = true;
        } else if (c == '&') {
            isColorPrefix = true;
        }

        if (isColorPrefix && i + 1 < displayName.size()) {
            char code = std::tolower(displayName[i + 1]);

            if (code >= '0' && code <= '9') {
                return static_cast<ChatColor>(code - '0');
            } else if (code >= 'a' && code <= 'f') {
                return static_cast<ChatColor>(10 + (code - 'a'));
            }
        }
    }

    return ChatColor::UNKNOWN;
}

char FriendsManager::getColorChar(ChatColor color) {
    int val = static_cast<int>(color);
    if (val >= 0 && val <= 9)
        return '0' + val;
    if (val >= 10 && val <= 15)
        return 'a' + (val - 10);
    return 'f';  // default white
}

// ============================================================
// Friends System - UUID-based (PRIMARY)
// ============================================================

void FriendsManager::addFriendByUUID(const std::string& uuid, const std::string& displayName) {
    if (uuid.empty())
        return;

    std::string initialName = displayName.empty() ? uuid.substr(0, 8) : displayName;

    // Store immediately with provided name for instant UI feedback
    {
        std::lock_guard<std::mutex> lock(mutex_);
        FriendData data;
        data.uuid = uuid;
        data.displayName = initialName;
        friendsByUUID_[uuid] = data;
        std::string cleanName = stripColorCodes(data.displayName);
        std::string lowerName = toLower(cleanName);
        friendsByName_.insert(lowerName);
        pendingNames_.erase(lowerName);
    }

    log_debug("[Friends] Added friend by UUID: %s (%s) - resolving real name...", uuid.c_str(), initialName.c_str());

    // Resolve REAL name via Mojang API in background (anti-cheat bypass)
    std::thread([this, uuid, initialName]() {
        std::string realName = resolveMojangUsername(uuid);

        if (!realName.empty() && realName != initialName) {
            // Update with real name
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = friendsByUUID_.find(uuid);
            if (it != friendsByUUID_.end()) {
                std::string oldClean = toLower(stripColorCodes(it->second.displayName));
                friendsByName_.erase(oldClean);

                it->second.displayName = realName;
                friendsByName_.insert(toLower(realName));
                log_debug("[Friends] Updated friend %s: '%s' -> '%s'", uuid.c_str(), initialName.c_str(),
                          realName.c_str());

                // Show corrected notification
                NotificationManager::addSystem("Added " + realName);
            }
        }
    }).detach();
}

void FriendsManager::removeFriendByUUID(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Also remove from friendsByName_ using the displayName
    auto it = friendsByUUID_.find(uuid);
    if (it != friendsByUUID_.end()) {
        std::string cleanName = stripColorCodes(it->second.displayName);
        std::string lowerName = toLower(cleanName);
        friendsByName_.erase(lowerName);
        friendsByUUID_.erase(it);
        log_debug("[Friends] Removed friend by UUID: %s (%s)", uuid.c_str(), cleanName.c_str());
    }
}

bool FriendsManager::isFriendByUUID(const std::string& uuid) const {
    if (uuid.empty())
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    // Normalize to lowercase for case-insensitive matching
    std::string lowerUUID = toLower(uuid);

    for (const auto& pair : friendsByUUID_) {
        if (toLower(pair.first) == lowerUUID) {
            return true;
        }
    }
    return false;
}

std::string FriendsManager::getFriendDisplayName(const std::string& uuid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = friendsByUUID_.find(uuid);
    if (it != friendsByUUID_.end()) {
        return it->second.displayName;
    }
    return "";
}

void FriendsManager::updateFriendName(const std::string& uuid, const std::string& newName) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = friendsByUUID_.find(uuid);
    if (it != friendsByUUID_.end()) {
        it->second.displayName = newName;
    }
}

std::vector<FriendData> FriendsManager::getFriends() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FriendData> result;
    for (const auto& pair : friendsByUUID_) {
        result.push_back(pair.second);
    }
    return result;
}

void FriendsManager::clearFriends() {
    std::lock_guard<std::mutex> lock(mutex_);
    friendsByUUID_.clear();
    friendsByName_.clear();
    pendingNames_.clear();
}

// ============================================================
// Friends System - Name-based (LEGACY)
// ============================================================

void FriendsManager::addFriend(const std::string& name) {
    std::string cleanName = stripColorCodes(name);
    std::string lower = toLower(cleanName);

    // Already have this friend by name?
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (friendsByName_.find(lower) != friendsByName_.end()) {
            log_debug("[Friends] '%s' already in friends list", cleanName.c_str());
            return;
        }
        // Add to name list immediately (for UI feedback)
        friendsByName_.insert(lower);
    }

    // Resolve UUID via Mojang API in background thread
    std::thread([this, cleanName, lower]() {
        std::string uuid = resolveMojangUUID(cleanName);

        if (!uuid.empty()) {
            // Success - add as UUID-based friend
            FriendData data;
            data.uuid = uuid;
            data.displayName = cleanName;

            std::lock_guard<std::mutex> lock(mutex_);
            friendsByUUID_[uuid] = data;
            pendingNames_.erase(lower);
            log_ok("[Friends] Added '%s' (UUID: %s)", cleanName.c_str(), uuid.c_str());
        } else {
            // Failed - remove from lists (player not found)
            std::lock_guard<std::mutex> lock(mutex_);
            friendsByName_.erase(lower);
            pendingNames_.erase(lower);
            log_warn("[Friends] '%s' not found - removed from list", cleanName.c_str());
        }
    }).detach();
}

void FriendsManager::removeFriend(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string cleanName = stripColorCodes(name);
    std::string lower = toLower(cleanName);
    friendsByName_.erase(lower);
    pendingNames_.erase(lower);
}

bool FriendsManager::isFriend(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Strip color codes first, then lowercase for matching
    std::string cleanName = stripColorCodes(name);
    std::string lowerName = toLower(cleanName);
    bool found = friendsByName_.find(lowerName) != friendsByName_.end();

    return found;
}

std::vector<std::string> FriendsManager::getFriendNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> combined = friendsByName_;
    for (const auto& entry : friendsByUUID_) {
        std::string cleanName = stripColorCodes(entry.second.displayName);
        if (!cleanName.empty()) {
            combined.insert(toLower(cleanName));
        }
    }
    return std::vector<std::string>(combined.begin(), combined.end());
}

// ============================================================
// Teams System
// ============================================================

bool FriendsManager::isTeammate(const std::string& playerUUID, ChatColor playerColor, ChatColor localColor) const {
    if (!teams.enabled)
        return false;

    // Debug: log parsed colors
    static auto lastDebugLog = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDebugLog).count() > 500) {
        // Debug logging disabled - uncomment to debug team detection
        // log_debug("[Teams] Check UUID=%s playerColor=%d localColor=%d (auto=%d manual=%d manualColor=%d)",
        // 	playerUUID.empty() ? "?" : playerUUID.substr(0, 8).c_str(),
        // 	static_cast<int>(playerColor), static_cast<int>(localColor),
        // 	teams.matchOwnNametag ? 1 : 0,
        // 	teams.manualColorEnabled ? 1 : 0,
        // 	static_cast<int>(teams.manualColor));
        lastDebugLog = now;
    }

    uint64_t currentSerial = worldChangeSerial_.load(std::memory_order_relaxed);

    // Check Lunar API teammates by UUID first (most reliable) - PRIORITY
    if (teams.lunarApiEnabled && !playerUUID.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lunarTeammates_.find(playerUUID);
        if (it != lunarTeammates_.end()) {
            // Validate that teammate was registered in current world
            if (it->second.registeredAtSerial == currentSerial) {
                return true;
            }
            // Stale record from previous world - ignore
        }
    }

    // Check if ANY Lunar teammates exist for current world
    // If yes, ONLY use Lunar API (ignore color matching to avoid false positives)
    if (teams.lunarApiEnabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : lunarTeammates_) {
            if (entry.second.registeredAtSerial == currentSerial) {
                // Lunar API is active - don't use color matching as fallback
                // This prevents false positives from servers that give same color to non-teammates
                return false;
            }
        }
    }

    // Only use color matching as FALLBACK when no Lunar API data exists
    // Auto color: match own nametag color
    if (teams.matchOwnNametag && localColor != ChatColor::UNKNOWN && playerColor == localColor) {
        return true;
    }

    // Manual color: filter by selected color
    if (teams.manualColorEnabled && playerColor == teams.manualColor) {
        return true;
    }

    return false;
}

void FriendsManager::addLunarTeammate(const std::string& uuid) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::lock_guard<std::mutex> lock(mutex_);

    // Create record with current world serial
    TeammateRecord record;
    record.registeredAtSerial = worldChangeSerial_.load(std::memory_order_relaxed);
    record.registeredAtMs = nowMs;

    auto result = lunarTeammates_.insert({uuid, record});
    if (result.second) {
        log_debug("[Apollo] Registered Lunar teammate: %s (serial=%llu)", uuid.c_str(),
                  (unsigned long long)record.registeredAtSerial);
    } else {
        // Update existing record with new serial
        result.first->second = record;
    }
}

void FriendsManager::clearLunarTeammates() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lunarTeammates_.empty()) {
        log_debug("[Apollo] Cleared %zu Lunar teammates", lunarTeammates_.size());
        lunarTeammates_.clear();
    }
}

bool FriendsManager::isLunarTeammate(const std::string& uuid) const {
    if (!teams.lunarApiEnabled)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lunarTeammates_.find(uuid);
    if (it == lunarTeammates_.end())
        return false;

    // Validate that teammate was registered in current world
    return it->second.registeredAtSerial == worldChangeSerial_.load(std::memory_order_relaxed);
}

bool FriendsManager::hasLunarTeammates() const {
    if (!teams.lunarApiEnabled)
        return false;
    uint64_t currentSerial = worldChangeSerial_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if any Lunar teammate is valid for current world
    for (const auto& entry : lunarTeammates_) {
        if (entry.second.registeredAtSerial == currentSerial) {
            return true;
        }
    }
    return false;
}

void FriendsManager::removeLunarTeammate(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lunarTeammates_.find(uuid);
    if (it != lunarTeammates_.end()) {
        lunarTeammates_.erase(it);
        log_debug("[Apollo] Removed Lunar teammate: %s", uuid.substr(0, 8).c_str());
    }
}

void FriendsManager::syncLunarTeammates(const std::set<std::string>& currentTeammates) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    uint64_t currentSerial = worldChangeSerial_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);

    // Find teammates that were removed (in old map but not in new set)
    std::vector<std::string> toRemove;
    for (const auto& entry : lunarTeammates_) {
        if (currentTeammates.find(entry.first) == currentTeammates.end()) {
            toRemove.push_back(entry.first);
        }
    }

    // Remove players who left the team
    for (const auto& uuid : toRemove) {
        lunarTeammates_.erase(uuid);
        log_ok("[Apollo] Teammate left: %s", uuid.substr(0, 8).c_str());
    }

    // Add new teammates with current serial
    for (const auto& uuid : currentTeammates) {
        auto it = lunarTeammates_.find(uuid);
        if (it == lunarTeammates_.end()) {
            TeammateRecord record;
            record.registeredAtSerial = currentSerial;
            record.registeredAtMs = nowMs;
            lunarTeammates_[uuid] = record;
            log_debug("[Apollo] Registered Lunar teammate: %s (serial=%llu)", uuid.substr(0, 8).c_str(),
                      (unsigned long long)currentSerial);
        } else {
            // Update existing record with current serial (refresh)
            it->second.registeredAtSerial = currentSerial;
            it->second.registeredAtMs = nowMs;
        }
    }
}

void FriendsManager::setApolloNametagColor(const std::string& uuid, const std::string& color, bool hasExtra) {
    if (uuid.empty())
        return;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    uint64_t currentSerial = worldChangeSerial_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    apolloNametagLastSeenMs_[uuid] = nowMs;
    (void)hasExtra;  // Parameter preserved for potential debug logging

    bool isGreen = (color == "GREEN" || color == "DARK_GREEN");
    // Only treat as teammate signal if the color is actually GREEN
    // hasExtra alone (cosmetic effects) should NOT trigger teammate detection
    bool isTeammateSignal = isGreen;
    int& greenHits = apolloNametagGreenHits_[uuid];
    int64_t& lastGreenMs = apolloNametagLastGreenMs_[uuid];

    if (isTeammateSignal) {
        if (lastGreenMs > 0 && nowMs - lastGreenMs > kApolloGreenConfirmWindowMs) {
            greenHits = 0;
        }
        // Always increment by 1, never auto-confirm (even with hasExtra)
        // This prevents cosmetic color cycling from false-positive triggering
        greenHits++;
        lastGreenMs = nowMs;
        if (greenHits >= kApolloGreenConfirmCount) {
            auto it = apolloNametagTeammates_.find(uuid);
            if (it == apolloNametagTeammates_.end()) {
                TeammateRecord record;
                record.registeredAtSerial = currentSerial;
                record.registeredAtMs = nowMs;
                apolloNametagTeammates_[uuid] = record;
                log_debug("[Apollo] Nametag TEAMMATE registered: %s -> %s (serial=%llu)", uuid.substr(0, 8).c_str(),
                          color.c_str(), (unsigned long long)currentSerial);
            } else {
                // Refresh serial for existing teammate
                it->second.registeredAtSerial = currentSerial;
                it->second.registeredAtMs = nowMs;
            }
        }
    } else {
        // Only reset greenHits if NOT already a confirmed teammate
        // This preserves confirmation state during temporary color changes (e.g., archer tag)
        auto itTeam = apolloNametagTeammates_.find(uuid);
        if (itTeam == apolloNametagTeammates_.end()) {
            // Not a teammate - reset hits
            greenHits = 0;
        } else {
            // Already a teammate - check grace period before removing
            // Keep greenHits intact so they don't need to re-confirm after archer tag
            if (lastGreenMs == 0 || nowMs - lastGreenMs > kApolloTeammateGraceMs) {
                apolloNametagTeammates_.erase(itTeam);
                greenHits = 0;  // Reset hits only when actually removed
                log_debug("[Apollo] Nametag TEAMMATE removed: %s -> %s (grace expired)", uuid.substr(0, 8).c_str(), color.c_str());
            }
            // else: within grace period, keep teammate status and greenHits
        }
    }

    apolloNametags_[uuid] = color;
}

bool FriendsManager::isApolloTeammate(const std::string& uuid) const {
    if (uuid.empty())
        return false;
    uint64_t currentSerial = worldChangeSerial_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);

    // Check Lunar teammates first (with serial validation) - PRIORITY
    auto itLunar = lunarTeammates_.find(uuid);
    if (itLunar != lunarTeammates_.end()) {
        if (itLunar->second.registeredAtSerial == currentSerial) {
            return true;
        }
    }

    // Check if ANY Lunar teammates exist for current world
    // If yes, ONLY use Lunar API (ignore nametag colors to avoid false positives)
    bool hasAnyLunarTeammate = false;
    for (const auto& entry : lunarTeammates_) {
        if (entry.second.registeredAtSerial == currentSerial) {
            hasAnyLunarTeammate = true;
            break;
        }
    }

    // Only use Apollo nametag teammates as FALLBACK when no Lunar API data exists
    // This prevents false positives from servers that give green nametags to non-teammates
    if (!hasAnyLunarTeammate) {
        auto itApollo = apolloNametagTeammates_.find(uuid);
        if (itApollo != apolloNametagTeammates_.end()) {
            if (itApollo->second.registeredAtSerial == currentSerial) {
                return true;
            }
        }
    }

    return false;
}

std::string FriendsManager::getApolloNametagColor(const std::string& uuid) const {
    if (uuid.empty())
        return "";
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = apolloNametags_.find(uuid);
    return (it != apolloNametags_.end()) ? it->second : "";
}

void FriendsManager::clearApolloNametags() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!apolloNametags_.empty()) {
        log_debug("[Apollo] Cleared %zu nametag colors (world change)", apolloNametags_.size());
        apolloNametags_.clear();
    }
    apolloNametagTeammates_.clear();
    apolloNametagGreenHits_.clear();
    apolloNametagLastGreenMs_.clear();
    apolloNametagLastSeenMs_.clear();
}

size_t FriendsManager::getApolloNametagCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apolloNametags_.size();
}

size_t FriendsManager::getApolloTeammateCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apolloNametagTeammates_.size();
}

// ============================================================
// Visual Friends - Auto-detected teammates
// ============================================================

void FriendsManager::addVisualFriend(const std::string& uuid) {
    if (uuid.empty())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (visualFriends_.find(uuid) == visualFriends_.end()) {
        visualFriends_.insert(uuid);
        log_debug("[Friends] Added visual friend: %s", uuid.substr(0, 8).c_str());
    }
}

void FriendsManager::removeVisualFriend(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = visualFriends_.find(uuid);
    if (it != visualFriends_.end()) {
        visualFriends_.erase(it);
        log_debug("[Friends] Removed visual friend: %s", uuid.substr(0, 8).c_str());
    }
}

bool FriendsManager::isVisualFriend(const std::string& uuid) const {
    if (uuid.empty())
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return visualFriends_.find(uuid) != visualFriends_.end();
}

void FriendsManager::clearVisualFriends() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!visualFriends_.empty()) {
        log_debug("[Friends] Cleared %zu visual friends (world change)", visualFriends_.size());
        visualFriends_.clear();
    }
}

void FriendsManager::notifyWorldChangePacket() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    int64_t last = lastWorldChangePacketMs_.load(std::memory_order_relaxed);
    if (nowMs - last < 500) {
        return;
    }
    lastWorldChangePacketMs_.store(nowMs, std::memory_order_relaxed);

    log_ok("[FriendsManager] World change detected (packet) - clearing teammate data");
    clearLunarTeammates();
    clearApolloNametags();
    clearVisualFriends();
    worldChangeSerial_.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================
// World Change Detection
// ============================================================

bool FriendsManager::checkWorldChange() {
    if (!Java::Env() || !Java::Initialized)
        return false;

    jobject currentWorld = mc.getWorld();
    bool worldChanged = false;

    jobject lastWorld = static_cast<jobject>(lastWorldRef_);

    if (lastWorld == nullptr && currentWorld != nullptr) {
        // First world detection (e.g., injection mid-match)
        // Don't clear teammates - treat as initialization, not change
        // Just store the reference and return
        lastWorldRef_ = Java::Env()->NewGlobalRef(currentWorld);
        Java::Env()->DeleteLocalRef(currentWorld);
        log_debug("[FriendsManager] First world detection - skipping teammate clear");
        return false;
    } else if (lastWorld != nullptr && currentWorld == nullptr) {
        // Left server
        worldChanged = true;
    } else if (lastWorld != nullptr && currentWorld != nullptr) {
        // Check if different world object
        if (!Java::Env()->IsSameObject(lastWorld, currentWorld)) {
            worldChanged = true;
        }
    }

    if (worldChanged) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        int64_t lastPacketMs = lastWorldChangePacketMs_.load(std::memory_order_relaxed);
        if (nowMs - lastPacketMs > 3000) {
            log_ok("[FriendsManager] World change detected - clearing teammate data");

            // Clear all session-based teammate data
            clearLunarTeammates();
            clearApolloNametags();
            clearVisualFriends();
            worldChangeSerial_.fetch_add(1, std::memory_order_relaxed);
        }

        // Update world reference
        if (lastWorld) {
            Java::Env()->DeleteGlobalRef(lastWorld);
            lastWorldRef_ = nullptr;
        }

        if (currentWorld) {
            lastWorldRef_ = Java::Env()->NewGlobalRef(currentWorld);
        }
    }

    // Clean up local ref
    if (currentWorld) {
        Java::Env()->DeleteLocalRef(currentWorld);
    }
    return worldChanged;
}

uint64_t FriendsManager::getWorldChangeSerial() const {
    return worldChangeSerial_.load(std::memory_order_relaxed);
}

// ============================================================
// Combined Check
// ============================================================

bool FriendsManager::shouldIgnoreByUUID(const std::string& uuid, ChatColor playerColor, ChatColor localColor) const {
    // Check visual friends first (fastest - auto-detected teammates)
    if (isVisualFriend(uuid)) {
        return true;
    }

    // Check friends by UUID (manual friends)
    if (isFriendByUUID(uuid)) {
        return true;
    }

    // Check teams (also uses UUID for Lunar API)
    if (isTeammate(uuid, playerColor, localColor)) {
        return true;
    }

    return false;
}

bool FriendsManager::shouldIgnore(const std::string& name, ChatColor playerColor, ChatColor localColor) const {
    // Legacy name-based check
    if (isFriend(name)) {
        return true;
    }

    // Teams still need UUID for Lunar API, but color matching works without it
    if (isTeammate("", playerColor, localColor)) {
        return true;
    }

    return false;
}

// ============================================================
// Pending resolution: convert name inputs to UUID when seen in-world
// ============================================================

int FriendsManager::resolvePending(const std::vector<PlayerInfo>& players) {
    if (players.empty())
        return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingNames_.empty())
        return 0;

    int resolved = 0;
    std::vector<std::string> seenCandidates;

    for (const auto& p : players) {
        if (p.uuid.empty())
            continue;
        std::string uuidLower = toLower(p.uuid);
        std::string uuidPrefix = uuidLower.size() >= 8 ? uuidLower.substr(0, 8) : uuidLower;

        std::string candidateName = p.profileName;
        if (candidateName.empty()) {
            candidateName = p.name;
        }
        if (candidateName.empty() && !p.displayName.empty()) {
            candidateName = stripColorCodes(p.displayName);
        }
        if (candidateName.empty())
            continue;

        std::string normalized = toLower(stripColorCodes(candidateName));
        if (seenCandidates.size() < 5) {
            seenCandidates.push_back(normalized);
        }

        // Direct match on normalized name
        auto it = pendingNames_.find(normalized);
        if (it == pendingNames_.end()) {
            // Also match if user entered UUID or prefix of UUID as "name"
            it = pendingNames_.find(uuidLower);
            if (it == pendingNames_.end()) {
                it = pendingNames_.find(uuidPrefix);
            }
            if (it == pendingNames_.end())
                continue;
        }

        // Resolve to UUID and store friend entry
        FriendData data;
        data.uuid = p.uuid;
        data.displayName = candidateName;
        friendsByUUID_[p.uuid] = data;
        friendsByName_.insert(normalized);  // keep UI/backwards compat
        pendingNames_.erase(it);
        log_debug("[Friends] Resolved pending friend '%s' -> %s", normalized.c_str(), p.uuid.c_str());
        ++resolved;
    }

    // If nothing resolved, log pending state periodically for debugging
    if (resolved == 0 && !pendingNames_.empty()) {
        static auto lastLog = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLog).count() > 2000) {
            std::string pendingSample = *pendingNames_.begin();
            std::string seenSample;
            for (size_t i = 0; i < seenCandidates.size(); ++i) {
                if (i > 0)
                    seenSample += ", ";
                seenSample += seenCandidates[i];
            }
            log_debug("[Friends] Pending unresolved: %zu (e.g. %s) | seen candidates this tick: %s",
                      pendingNames_.size(), pendingSample.c_str(), seenSample.c_str());
            lastLog = now;
        }
    }
    return resolved;
}
