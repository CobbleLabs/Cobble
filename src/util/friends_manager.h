#pragma once

#include <string>
#include <vector>
#include <set>
#include <mutex>

// Minecraft color codes (Â§X format)
enum class ChatColor : int {
	BLACK = 0,        // Â§0
	DARK_BLUE = 1,    // Â§1
	DARK_GREEN = 2,   // Â§2
	DARK_AQUA = 3,    // Â§3
	DARK_RED = 4,     // Â§4
	DARK_PURPLE = 5,  // Â§5
	GOLD = 6,         // Â§6
	GRAY = 7,         // Â§7
	DARK_GRAY = 8,    // Â§8
	BLUE = 9,         // Â§9
	GREEN = 10,       // Â§a
	AQUA = 11,        // Â§b
	RED = 12,         // Â§c
	LIGHT_PURPLE = 13,// Â§d
	YELLOW = 14,      // Â§e
	WHITE = 15,       // Â§f
	UNKNOWN = -1
};

// Forward declare
struct PlayerInfo;

// FriendsManager - Manages friends list and team detection
// Used by combat modules to filter friendly targets
class FriendsManager {
public:
	FriendsManager() = default;
	
	// ============================================================
	// Friends System - Manual IGN list
	// ============================================================
	
	// Add a friend by name (case insensitive matching)
	void addFriend(const std::string& name);
	
	// Remove a friend by name
	void removeFriend(const std::string& name);
	
	// Check if a name is in friends list
	bool isFriend(const std::string& name) const;
	
	// Get all friends (for UI display)
	std::vector<std::string> getFriends() const;
	
	// Clear all friends
	void clearFriends();
	
	// ============================================================
	// Teams System - Auto-detection via nametag color
	// ============================================================
	
	struct TeamSettings {
		bool enabled = true;
		bool matchOwnNametag = true;  // If true, players with same color as local are teammates
		
		// Manual team colors (ChatColor IDs that are considered friendly)
		// Default: GREEN and DARK_GREEN
		std::set<ChatColor> teamColors = { ChatColor::GREEN, ChatColor::DARK_GREEN };
		
		// Lunar API support
		bool lunarApiEnabled = true;  // Read teammates from Lunar Client API packets
	} teams;
	
	// Check if a player is a teammate based on color matching or Lunar API
	bool isTeammate(const std::string& playerName, ChatColor playerColor, ChatColor localColor) const;
	
	// Lunar API: Add player UUID to teammates set (called when packet received)
	void addLunarTeammate(const std::string& uuid);
	void clearLunarTeammates();
	bool isLunarTeammate(const std::string& uuid) const;
	
	// ============================================================
	// Application Scope
	// ============================================================
	
	bool applyToCombat = true;   // Filter AimAssist, AutoClicker
	bool applyToVisual = false; // Change ESP/Nametag colors for friends
	
	// ============================================================
	// Combined Check - Use this in modules
	// ============================================================
	
	// Returns true if this player should be ignored by combat modules
	// Checks friends list, team colors, and Lunar API teammates
	bool shouldIgnore(const std::string& name, ChatColor playerColor, ChatColor localColor) const;
	
	// ============================================================
	// Utility
	// ============================================================
	
	// Strip all Minecraft color codes (Â§X) from a string
	// Also handles UTF-8 Â§ (0xC2 0xA7) and & prefix (some mods)
	static std::string stripColorCodes(const std::string& input);
	
	// Extract ChatColor from a display name with color codes
	// e.g. "Â§aPlayerName" -> ChatColor::GREEN
	static ChatColor extractColor(const std::string& displayName);
	
	// Get color code character for a ChatColor
	static char getColorChar(ChatColor color);
	
private:
	mutable std::mutex mutex_;
	
	// Friends list (lowercase for case-insensitive matching)
	std::set<std::string> friends_;
	
	// Lunar API teammates (UUIDs from UpdateTeamMembersMessage)
	std::set<std::string> lunarTeammates_;
	
	// Helper to lowercase a string
	static std::string toLower(const std::string& s);
};

extern FriendsManager* friendsManager;
