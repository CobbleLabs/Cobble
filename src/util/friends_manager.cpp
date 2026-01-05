#include "friends_manager.h"
#include "logger.hpp"
#include <algorithm>
#include <cctype>

FriendsManager* friendsManager = nullptr;

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
	std::lock_guard<std::mutex> lock(mutex_);
	FriendData data;
	data.uuid = uuid;
	data.displayName = displayName.empty() ? uuid.substr(0, 8) : displayName;
	friendsByUUID_[uuid] = data;
	log_debug("[Friends] Added friend by UUID: %s (%s)", uuid.c_str(), data.displayName.c_str());
}

void FriendsManager::removeFriendByUUID(const std::string& uuid) {
	std::lock_guard<std::mutex> lock(mutex_);
	friendsByUUID_.erase(uuid);
	log_debug("[Friends] Removed friend by UUID: %s", uuid.c_str());
}

bool FriendsManager::isFriendByUUID(const std::string& uuid) const {
	if (uuid.empty()) return false;
	std::lock_guard<std::mutex> lock(mutex_);
	return friendsByUUID_.find(uuid) != friendsByUUID_.end();
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
}

// ============================================================
// Friends System - Name-based (LEGACY)
// ============================================================

void FriendsManager::addFriend(const std::string& name) {
	std::lock_guard<std::mutex> lock(mutex_);
	std::string cleanName = stripColorCodes(name);
	friendsByName_.insert(toLower(cleanName));
}

void FriendsManager::removeFriend(const std::string& name) {
	std::lock_guard<std::mutex> lock(mutex_);
	std::string cleanName = stripColorCodes(name);
	friendsByName_.erase(toLower(cleanName));
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
	return std::vector<std::string>(friendsByName_.begin(), friendsByName_.end());
}

// ============================================================
// Teams System
// ============================================================

bool FriendsManager::isTeammate(const std::string& playerUUID, ChatColor playerColor, ChatColor localColor) const {
	if (!teams.enabled) return false;
	
	// Check Lunar API teammates by UUID first (most reliable)
	if (teams.lunarApiEnabled && !playerUUID.empty()) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (lunarTeammates_.find(playerUUID) != lunarTeammates_.end()) {
			return true;
		}
	}
	
	// Check match own nametag
	if (teams.matchOwnNametag && localColor != ChatColor::UNKNOWN && playerColor == localColor) {
		return true;
	}
	
	// Check manual team colors
	if (teams.teamColors.find(playerColor) != teams.teamColors.end()) {
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
