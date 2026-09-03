#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

// Teammate record - tracks when a teammate was registered
// Used to validate that teammate data is from the current world
struct TeammateRecord {
    uint64_t registeredAtSerial;  // World serial when teammate was registered
    int64_t registeredAtMs;       // Timestamp for debugging
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
    ~FriendsManager();

    // ============================================================
    // Friends System - UUID-based (primary)
    // ============================================================

    // Add a friend by UUID (with optional display name for UI)
    void addFriendByUUID(const std::string& uuid, const std::string& displayName = "");

    // Restore a persisted friend without starting a network lookup.
    // Empty UUID entries remain valid name-based friends until resolved in-world.
    void restoreFriend(const std::string& uuid, const std::string& displayName);

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

    // Live team-detection policy. The Teams module is the editor for this struct;
    // every detector below consults it, so a switch flipped in the menu takes effect
    // for combat and visuals at once without any per-module plumbing.
    struct TeamSettings {
        bool enabled = true;
        bool matchOwnNametag = true;  // If true, players with same color as local are teammates

        // Manual team colors (ChatColor IDs that are considered friendly)
        bool manualColorEnabled = false;           // If true, use manualColor to filter
        ChatColor manualColor = ChatColor::GREEN;  // Selected team color

        // Lunar API support
        bool lunarApiEnabled = true;  // Read teammates from Lunar Client API packets

        // Apollo OverrideNametagMessage: a green nametag is a strong teammate hint,
        // but cosmetic colour cycling can fake one, hence the confirmation counter.
        bool apolloNametags = true;

        // Vanilla scoreboard membership: S3EPacketTeams plus Entity.isOnSameTeam().
        bool scoreboard = true;

        // How many green nametag signals inside the confirm window are needed before
        // a player is trusted as a teammate. 1 reacts instantly and false-positives
        // on cosmetics; higher is slower but certain.
        int confirmHits = 3;

        // How long a confirmed teammate keeps the status after losing the colour.
        // Covers temporary recolours such as the HCTRanked archer tag.
        int graceMs = 10000;

        // Window inside which repeated green signals count toward the same confirmation.
        // A gap longer than this restarts the count at zero, so a player who happens to
        // flash green once every few seconds never accumulates his way to teammate.
        int confirmWindowMs = 1500;

        // A live Lunar roster suppresses the Apollo-nametag fallback outright: a server
        // that states its teams is more trustworthy than a colour. Turn this off where the
        // Lunar roster is only your party and green tags mark the rest of the team.
        bool lunarExclusive = true;

        // Count a green nametag only when it arrived with a team-tag payload. Tightens
        // servers that hand plain green tags out in lobbies and to party members.
        bool requireTeamTag = false;

        // Honour the vanilla team flag (Entity.isOnSameTeam), which the game computes
        // itself. Separate from `scoreboard`, which is Cobble's own S3EPacketTeams
        // tracking — combat modules obey this flag even when Cobble has no roster.
        bool vanillaTeamFlag = true;
    } teams;

    // Should combat modules obey PlayerInfo::isOnLocalTeam? The vanilla flag comes from the
    // game rather than from us, so it needs its own switch — and "Teams off" has to mean no
    // team awareness at all, this flag included.
    bool honorsVanillaTeams() const { return teams.enabled && teams.vanillaTeamFlag; }

    // Snapshot of what is actually detected right now, for the module's status line.
    // Only records registered in the current world are counted.
    struct TeamStats {
        size_t friends = 0;
        size_t lunar = 0;
        size_t apolloNametags = 0;
        size_t scoreboardTeams = 0;
    };
    TeamStats getTeamStats() const;

    // Drop every auto-detected record so the next packets rebuild it from scratch.
    // Manual friends are untouched.
    void clearDetected();

    // Check if a player is a teammate based on color matching or Lunar API
    bool isTeammate(const std::string& playerUUID, ChatColor playerColor, ChatColor localColor) const;

    // Scoreboard team membership from S3EPacketTeams.
    void updateScoreboardTeam(const std::string& teamName, int action, const std::vector<std::string>& players);
    void clearScoreboardTeams();
    bool isScoreboardTeammate(const std::string& localName, const std::string& playerName) const;

    // Lunar API: Teammate management (from UpdateTeamMembersMessage)
    void addLunarTeammate(const std::string& uuid);
    void removeLunarTeammate(const std::string& uuid);
    void
    syncLunarTeammates(const std::set<std::string>& currentTeammates);  // Syncs to new set, removes players who left
    void clearLunarTeammates();
    bool isLunarTeammate(const std::string& uuid) const;
    bool hasLunarTeammates() const;  // Returns true if any Lunar API teammates are active

    // Apollo Nametags: Store player nametag colors from OverrideNametagMessage
    // hasExtra: the colour came from the nested team tag (HCTRanked) rather than the outer
    // name, which a plain lobby/party tag uses. Only consulted when teams.requireTeamTag.
    void setApolloNametagColor(const std::string& uuid, const std::string& color, bool hasExtra = true);
    bool isApolloTeammate(const std::string& uuid) const;  // confirmed via Apollo nametag or Lunar team payload
    std::string getApolloNametagColor(const std::string& uuid) const;
    void clearApolloNametags();
    size_t getApolloNametagCount() const;   // Debug: total nametags cached
    size_t getApolloTeammateCount() const;  // Debug: count of GREEN/DARK_GREEN

    // ============================================================
    // World Change Detection
    // ============================================================

    // Call from main loop to detect server/dimension changes
    // Automatically clears lunarTeammates_ and apolloNametags_ when world changes
    bool checkWorldChange();
    uint64_t getWorldChangeSerial() const;

    // ============================================================
    // Visual Friends - Auto-detected teammates (cleared on world change)
    // ============================================================

    // Add/remove player from visual friends (auto-detected via nametag color)
    void addVisualFriend(const std::string& uuid);
    void removeVisualFriend(const std::string& uuid);
    bool isVisualFriend(const std::string& uuid) const;
    void clearVisualFriends();
    void notifyWorldChangePacket();

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
    // Map stores TeammateRecord to validate data is from current world
    std::map<std::string, TeammateRecord> lunarTeammates_;

    // Apollo nametag colors (UUIDs from OverrideNametagMessage -> color string)
    std::map<std::string, std::string> apolloNametags_;
    // Map stores TeammateRecord to validate data is from current world
    std::unordered_map<std::string, TeammateRecord> apolloNametagTeammates_;
    std::unordered_map<std::string, int> apolloNametagGreenHits_;
    std::unordered_map<std::string, int64_t> apolloNametagLastGreenMs_;
    std::unordered_map<std::string, int64_t> apolloNametagLastSeenMs_;

    // Visual friends (auto-detected teammates, cleared on world change)
    std::set<std::string> visualFriends_;

    // Vanilla scoreboard membership. Team names are server-defined; player names are normalized.
    std::unordered_map<std::string, std::unordered_set<std::string>> scoreboardTeams_;

    // World change detection (jobject stored as void* to avoid JNI include)
    void* lastWorldRef_ = nullptr;
    std::atomic<uint64_t> worldChangeSerial_{0};
    std::atomic<int64_t> lastWorldChangePacketMs_{0};

    // Helper to lowercase a string
    static std::string toLower(const std::string& s);
};

extern FriendsManager* friendsManager;
