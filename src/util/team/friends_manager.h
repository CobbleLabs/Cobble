#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// Minecraft color codes (§X format)
enum class ChatColor : int {
    BLACK = 0,          // §0
    DARK_BLUE = 1,      // §1
    DARK_GREEN = 2,     // §2
    DARK_AQUA = 3,      // §3
    DARK_RED = 4,       // §4
    DARK_PURPLE = 5,    // §5
    GOLD = 6,           // §6
    GRAY = 7,           // §7
    DARK_GRAY = 8,      // §8
    BLUE = 9,           // §9
    GREEN = 10,         // §a
    AQUA = 11,          // §b
    RED = 12,           // §c
    LIGHT_PURPLE = 13,  // §d
    YELLOW = 14,        // §e
    WHITE = 15,         // §f
    UNKNOWN = -1
};

// Forward declare
struct PlayerInfo;

// Friend data - UUID is the key, name is for display only
struct FriendData {
    std::string uuid;         // Primary key - NEVER changes
    std::string displayName;  // For UI only - can change
};

// FriendsManager - Manages friends list and team detection
// ============================================================
// IMPORTANT: All matching is done by UUID, never by name!
// Names are ONLY for display purposes (UI, debug logs)
// Servers can and DO modify names, but UUID is immutable.
// ============================================================
class FriendsManager {
public:
    FriendsManager() = default;

    // ============================================================
    // Friends System - UUID-based (primary)
    // ============================================================

    // Add a friend by UUID (with optional display name for UI)
    void addFriendByUUID(const std::string& uuid, const std::string& displayName = "");

    // Remove a friend by UUID
    void removeFriendByUUID(const std::string& uuid);

    // Check if a UUID is in friends list - USE THIS FOR FILTERING
    bool isFriendByUUID(const std::string& uuid) const;

    // Get friend display name for a UUID (for UI)
    std::string getFriendDisplayName(const std::string& uuid) const;

    // Update friend's display name (when we learn their real name)
    void updateFriendName(const std::string& uuid, const std::string& newName);

    // Get all friends (for UI display)
    std::vector<FriendData> getFriends() const;

    // Clear all friends
    void clearFriends();

    // ============================================================
    // Friends System - Name-based (LEGACY - for backwards compat)
    // NOTE: These still exist for config loading but internally
    // will be converted to UUID when possible
    // ============================================================

    // Add a friend by name (legacy support, stored as lowercase name)
    void addFriend(const std::string& name);

    // Remove a friend by name
    void removeFriend(const std::string& name);

    // Check if a name is in friends list (legacy fallback)
    bool isFriend(const std::string& name) const;

    // Get all friend names (for legacy config)
    std::vector<std::string> getFriendNames() const;

    // ============================================================
    // Teams System - Auto-detection via nametag color
    // ============================================================

    struct TeamSettings {
        bool enabled = true;
        bool matchOwnNametag = true;  // If true, players with same color as local are teammates

        // Manual team colors (ChatColor IDs that are considered friendly)
        bool manualColorEnabled = false;           // If true, use manualColor to filter
        ChatColor manualColor = ChatColor::GREEN;  // Selected team color

        // Lunar API support
        bool lunarApiEnabled = true;  // Read teammates from Lunar Client API packets
    } teams;

    // Check if a player is a teammate based on color matching or Lunar API
    bool isTeammate(const std::string& playerUUID, ChatColor playerColor, ChatColor localColor) const;

    // Lunar API: Teammate management (from UpdateTeamMembersMessage)
    void addLunarTeammate(const std::string& uuid);
    void removeLunarTeammate(const std::string& uuid);
    void
    syncLunarTeammates(const std::set<std::string>& currentTeammates);  // Syncs to new set, removes players who left
    void clearLunarTeammates();
    bool isLunarTeammate(const std::string& uuid) const;

    // Apollo Nametags: Store player nametag colors from OverrideNametagMessage
    // hasExtra: true if JSON had "extra" array (match/complex nametag), false for simple (lobby)
    // When hasExtra=false, trust the color directly (no sticky teammate behavior)
    void setApolloNametagColor(const std::string& uuid, const std::string& color, bool hasExtra = true);
    bool isApolloTeammate(const std::string& uuid) const;  // GREEN or DARK_GREEN
    std::string getApolloNametagColor(const std::string& uuid) const;
    void clearApolloNametags();
    size_t getApolloNametagCount() const;   // Debug: total nametags cached
    size_t getApolloTeammateCount() const;  // Debug: count of GREEN/DARK_GREEN

    // ============================================================
    // World Change Detection
    // ============================================================
    
    // Call from main loop to detect server/dimension changes
    // Automatically clears lunarTeammates_ and apolloNametags_ when world changes
    void checkWorldChange();

    // ============================================================
    // Visual Friends - Auto-detected teammates (cleared on world change)
    // ============================================================

    // Add/remove player from visual friends (auto-detected via nametag color)
    void addVisualFriend(const std::string& uuid);
    void removeVisualFriend(const std::string& uuid);
    bool isVisualFriend(const std::string& uuid) const;
    void clearVisualFriends();

    // ============================================================
    // Application Scope
    // ============================================================

    bool applyToCombat = true;   // Filter AimAssist, AutoClicker
    bool applyToVisual = false;  // Change ESP/Nametag colors for friends

    // Quick-add friend feature (press key while looking at player)
    bool quickAddEnabled = true;
    int quickAddKey = 0x04;  // Default: middle mouse button (VK_MBUTTON)

    // ============================================================
    // Combined Check - Use this in modules
    // ============================================================

    // Returns true if this player should be ignored by combat modules
    // Checks friends list (by UUID), team colors, and Lunar API teammates
    // NOTE: Pass UUID for reliable matching, name is only for legacy fallback
    bool shouldIgnoreByUUID(const std::string& uuid, ChatColor playerColor, ChatColor localColor) const;

    // Legacy name-based check (fallback only)
    bool shouldIgnore(const std::string& name, ChatColor playerColor, ChatColor localColor) const;

    // ============================================================
    // Utility
    // ============================================================

    // Resolve pending name entries to UUIDs using live player data
    // Returns number of pending entries resolved
    int resolvePending(const std::vector<PlayerInfo>& players);

    // Strip all Minecraft color codes (§X) from a string
    // Also handles UTF-8 § (0xC2 0xA7) and & prefix (some mods)
    static std::string stripColorCodes(const std::string& input);

    // Strip only format codes (§k, §l, §m, §n, §o, §r) but KEEP color codes (§0-9, §a-f)
    // Useful when you want to preserve color information
    static std::string stripFormatCodes(const std::string& input);

    // Extract ChatColor from a display name with color codes
    // e.g. "§aPlayerName" -> ChatColor::GREEN
    static ChatColor extractColor(const std::string& displayName);

    // Get color code character for a ChatColor
    static char getColorChar(ChatColor color);

private:
    mutable std::mutex mutex_;

    // Friends by UUID (PRIMARY) - Map<UUID, FriendData>
    std::map<std::string, FriendData> friendsByUUID_;

    // Friends by name (LEGACY) - Set<lowercase name>
    // Used for backwards compatibility with old configs
    std::set<std::string> friendsByName_;

    // Pending names awaiting UUID resolution (lowercase, colorless)
    std::set<std::string> pendingNames_;

    // Lunar API teammates (UUIDs from UpdateTeamMembersMessage)
    std::set<std::string> lunarTeammates_;

    // Apollo nametag colors (UUIDs from OverrideNametagMessage -> color string)
    std::map<std::string, std::string> apolloNametags_;

    // Visual friends (auto-detected teammates, cleared on world change)
    std::set<std::string> visualFriends_;

    // World change detection (jobject stored as void* to avoid JNI include)
    void* lastWorldRef_ = nullptr;

    // Helper to lowercase a string
    static std::string toLower(const std::string& s);
};

extern FriendsManager* friendsManager;
