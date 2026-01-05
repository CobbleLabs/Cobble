#include "friends_manager.h"
#include "logger.hpp"
#include "http.hpp"
#include "json.hpp"
#include "../sdk/core/minecraft.h"
#include "../ui/hud/notifications.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

FriendsManager* friendsManager = nullptr;

// ============================================================
// Mojang API UUID Resolution
// ============================================================

// Resolve a Minecraft username to UUID via Mojang API
// Returns empty string if not found or error
static std::string resolveMojangUUID(const std::string& username) {
    if (username.empty()) return "";
    
    // GET https://api.mojang.com/users/profiles/minecraft/{username}
    std::wstring path = L"/users/profiles/minecraft/" + std::wstring(username.begin(), username.end());
    
    auto response = http::get(L"api.mojang.com", 443, path, true);  // HTTPS
    
    if (!response.success || response.statusCode != 200) {
        log_debug("[Friends] Mojang API failed for '%s': %s (status: %d)", 
                  username.c_str(), response.error.c_str(), response.statusCode);
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
    std::string uuid = rawUUID.substr(0, 8) + "-" +
                       rawUUID.substr(8, 4) + "-" +
                       rawUUID.substr(12, 4) + "-" +
                       rawUUID.substr(16, 4) + "-" +
                       rawUUID.substr(20, 12);
    
    log_debug("[Friends] Mojang resolved '%s' -> %s", username.c_str(), uuid.c_str());
    return uuid;
}

// Resolve a UUID to real Minecraft username via Mojang session server
// Returns empty string if not found or error
// API: GET https://sessionserver.mojang.com/session/minecraft/profile/{uuid-no-dashes}
static std::string resolveMojangUsername(const std::string& uuid) {
    if (uuid.empty()) return "";
    
    // Remove dashes from UUID for API call
    std::string cleanUUID = uuid;
    cleanUUID.erase(std::remove(cleanUUID.begin(), cleanUUID.end(), '-'), cleanUUID.end());
    
    // GET https://sessionserver.mojang.com/session/minecraft/profile/{uuid}
    std::wstring path = L"/session/minecraft/profile/" + std::wstring(cleanUUID.begin(), cleanUUID.end());
    
    log_debug("[Friends] Resolving UUID %s via Mojang...", uuid.c_str());
    
    auto response = http::get(L"sessionserver.mojang.com", 443, path, true);  // HTTPS
    
    if (!response.success || response.statusCode != 200) {
        log_debug("[Friends] Mojang session API failed for UUID '%s': %s (status: %d)", 
                  uuid.c_str(), response.error.c_str(), response.statusCode);
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
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return result;
}

std::string FriendsManager::stripColorCodes(const std::string& input) {
	std::string result;
	result.reserve(input.size());
	
	for (size_t i = 0; i < input.size(); ) {
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
		if (c == 0xE2 && i + 2 < input.size() && 
			(unsigned char)input[i + 1] == 0x94 && (unsigned char)input[i + 2] == 0xAC) {
			// Skip ┬ (3 bytes)
			i += 3;
			// Check for º (0xC2 0xBA) - masculine ordinal indicator
			if (i + 1 < input.size() && (unsigned char)input[i] == 0xC2 && 
				(unsigned char)input[i + 1] == 0xBA) {
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
				if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') ||
					(next >= 'k' && next <= 'o') || next == 'r') {
					i++; // Skip the color code
				}
			}
			continue;
		}
		
		// Check for & as color prefix (some mods use this)
		if (c == '&' && i + 1 < input.size()) {
			char next = std::tolower(input[i + 1]);
			if ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'f') ||
				(next >= 'k' && next <= 'o') || next == 'r') {
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

ChatColor FriendsManager::extractColor(const std::string& displayName) {
	// Look for color code pattern: § followed by 0-9 or a-f
	// Also handle & as alternate color code char (some mods)
	
	for (size_t i = 0; i + 1 < displayName.size(); i++) {
		char c = displayName[i];
		
		// Check for § (UTF-8: 0xC2 0xA7) or &
		bool isColorPrefix = false;
		if ((unsigned char)c == 0xC2 && i + 2 < displayName.size() && (unsigned char)displayName[i + 1] == 0xA7) {
			// UTF-8 § character
			i++; // skip to the A7
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
	if (val >= 0 && val <= 9) return '0' + val;
	if (val >= 10 && val <= 15) return 'a' + (val - 10);
	return 'f'; // default white
}

// ============================================================
// Friends System - UUID-based (PRIMARY)
// ============================================================

void FriendsManager::addFriendByUUID(const std::string& uuid, const std::string& displayName) {
	if (uuid.empty()) return;
	
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
				log_debug("[Friends] Updated friend %s: '%s' -> '%s'", uuid.c_str(), initialName.c_str(), realName.c_str());
				
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
	if (uuid.empty()) return false;
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
	if (!teams.enabled) return false;
	
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
	
	// Check Lunar API teammates by UUID first (most reliable)
	if (teams.lunarApiEnabled && !playerUUID.empty()) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (lunarTeammates_.find(playerUUID) != lunarTeammates_.end()) {
			return true;
		}
	}
	
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
	std::lock_guard<std::mutex> lock(mutex_);
	lunarTeammates_.insert(uuid);
}

void FriendsManager::clearLunarTeammates() {
	std::lock_guard<std::mutex> lock(mutex_);
	lunarTeammates_.clear();
}

bool FriendsManager::isLunarTeammate(const std::string& uuid) const {
	if (!teams.lunarApiEnabled) return false;
	std::lock_guard<std::mutex> lock(mutex_);
	return lunarTeammates_.find(uuid) != lunarTeammates_.end();
}

// ============================================================
// Combined Check
// ============================================================

bool FriendsManager::shouldIgnoreByUUID(const std::string& uuid, ChatColor playerColor, ChatColor localColor) const {
	// Check friends by UUID first (most reliable)
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
	if (players.empty()) return 0;
	std::lock_guard<std::mutex> lock(mutex_);
	if (pendingNames_.empty()) return 0;

	int resolved = 0;
	std::vector<std::string> seenCandidates;

	for (const auto& p : players) {
		if (p.uuid.empty()) continue;
		std::string uuidLower = toLower(p.uuid);
		std::string uuidPrefix = uuidLower.size() >= 8 ? uuidLower.substr(0, 8) : uuidLower;

		std::string candidateName = p.profileName;
		if (candidateName.empty()) {
			candidateName = p.name;
		}
		if (candidateName.empty() && !p.displayName.empty()) {
			candidateName = stripColorCodes(p.displayName);
		}
		if (candidateName.empty()) continue;

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
			if (it == pendingNames_.end()) continue;
		}

		// Resolve to UUID and store friend entry
		FriendData data;
		data.uuid = p.uuid;
		data.displayName = candidateName;
		friendsByUUID_[p.uuid] = data;
		friendsByName_.insert(normalized); // keep UI/backwards compat
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
				if (i > 0) seenSample += ", ";
				seenSample += seenCandidates[i];
			}
			log_debug("[Friends] Pending unresolved: %zu (e.g. %s) | seen candidates this tick: %s",
				pendingNames_.size(), pendingSample.c_str(), seenSample.c_str());
			lastLog = now;
		}
	}
	return resolved;
}
