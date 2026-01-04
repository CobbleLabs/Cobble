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
		
		// Check for UTF-8 Â§ (0xC2 0xA7)
		if (c == 0xC2 && i + 1 < input.size() && (unsigned char)input[i + 1] == 0xA7) {
			// Skip the Â§ (2 bytes) and the following color code character
			i += 2;
			if (i < input.size()) {
				// Skip the color code char (0-9, a-f, k-o, r)
				i++;
			}
			continue;
		}
		
		// Check for corrupted encoding pattern: â”¬Âº (0xE2 0x94 0xAC followed by 0xC2 0xBA or similar)
		// This happens when Â§ (0xC2 0xA7) is double-encoded or misinterpreted
		// Pattern: â”¬ = 0xE2 0x94 0xAC (Box Drawing Light Down And Horizontal)
		if (c == 0xE2 && i + 2 < input.size() && 
			(unsigned char)input[i + 1] == 0x94 && (unsigned char)input[i + 2] == 0xAC) {
			// Skip â”¬ (3 bytes)
			i += 3;
			// Check for Âº (0xC2 0xBA) - masculine ordinal indicator
			if (i + 1 < input.size() && (unsigned char)input[i] == 0xC2 && 
				(unsigned char)input[i + 1] == 0xBA) {
				// Skip Âº (2 bytes) and the color code
				i += 2;
				if (i < input.size()) {
					// Skip the color code char
					i++;
				}
			}
			continue;
		}
		
		// Alternative corruption: just Âº followed by color code 
		// (partial corruption of Â§)
		if (c == 0xC2 && i + 1 < input.size() && (unsigned char)input[i + 1] == 0xBA) {
			// Skip Âº and see if next is a color code
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
		
		// Check for raw Â§ byte (sometimes seen in corrupted encoding)
		// Latin-1 Â§ = 0xA7 as single byte
		if (c == 0xA7 && i + 1 < input.size()) {
			// Skip Â§ and the color code
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
	// Look for color code pattern: Â§ followed by 0-9 or a-f
	// Also handle & as alternate color code char (some mods)
	
	for (size_t i = 0; i + 1 < displayName.size(); i++) {
		char c = displayName[i];
		
		// Check for Â§ (UTF-8: 0xC2 0xA7) or &
		bool isColorPrefix = false;
		if ((unsigned char)c == 0xC2 && i + 2 < displayName.size() && (unsigned char)displayName[i + 1] == 0xA7) {
			// UTF-8 Â§ character
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
// Friends System
// ============================================================

void FriendsManager::addFriend(const std::string& name) {
	std::lock_guard<std::mutex> lock(mutex_);
	std::string cleanName = stripColorCodes(name);
	friends_.insert(toLower(cleanName));
}

void FriendsManager::removeFriend(const std::string& name) {
	std::lock_guard<std::mutex> lock(mutex_);
	std::string cleanName = stripColorCodes(name);
	friends_.erase(toLower(cleanName));
}

bool FriendsManager::isFriend(const std::string& name) const {
	std::lock_guard<std::mutex> lock(mutex_);
	
	// Strip color codes first, then lowercase for matching
	std::string cleanName = stripColorCodes(name);
	std::string lowerName = toLower(cleanName);
	bool found = friends_.find(lowerName) != friends_.end();
	
	// Debug logging
	static int debugCounter = 0;
	if (++debugCounter % 50 == 1 && !friends_.empty()) {
		std::string firstFriend = *friends_.begin();
		log_debug("[Friends] isFriend('%s') -> clean='%s', lower='%s', found=%d, list has %zu, first='%s'",
			name.c_str(), cleanName.c_str(), lowerName.c_str(), found, friends_.size(), firstFriend.c_str());
	}
	
	return found;
}

std::vector<std::string> FriendsManager::getFriends() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return std::vector<std::string>(friends_.begin(), friends_.end());
}

void FriendsManager::clearFriends() {
	std::lock_guard<std::mutex> lock(mutex_);
	friends_.clear();
}

// ============================================================
// Teams System
// ============================================================

bool FriendsManager::isTeammate(const std::string& playerName, ChatColor playerColor, ChatColor localColor) const {
	if (!teams.enabled) return false;
	
	// Check match own nametag first
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

bool FriendsManager::shouldIgnore(const std::string& name, ChatColor playerColor, ChatColor localColor) const {
	// Check friends first
	if (isFriend(name)) {
		return true;
	}
	
	// Check teams
	if (isTeammate(name, playerColor, localColor)) {
		return true;
	}
	
	// Lunar API check would need UUID which we don't have here
	// That check is done separately when UUID is available
	
	return false;
}
