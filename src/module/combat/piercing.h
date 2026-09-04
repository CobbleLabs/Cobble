#pragma once

#include "../../jni/java.h"
#include "../../util/team/friends_manager.h"
#include "../module.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct PlayerInfo;

class Piercing : public Module {
public:
    Piercing();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;
    bool handleAttackInput();
    bool shouldSendOutgoingPacket(JNIEnv* env, jobject packet);

    struct Settings {
        float distance = 3.0f;
        bool weaponsOnly = false;
        bool targetInvisibles = false;
        bool ignoreFriends = true;
        bool ignoreTeams = true;
        bool throughWalls = true;
        float throughWallsRange = 3.0f;
        bool backtrackCheck = true;
        float expand = 0.0f;
        // Keep the ray going once it has passed through a friend/teammate so the
        // enemy standing behind them becomes the target instead of nothing.
        bool pierceTeams = true;
        // Reach granted to a target that sits *behind* a friend/teammate. A body
        // in the way costs ~0.6-1.0 blocks of depth plus separation, so the enemy
        // behind is usually past the plain "Visible Distance" — without its own
        // budget the pierce would never resolve at the default 3.0.
        float pierceRange = 4.5f;
        // Blank vanilla's crosshair hit when it lands on a friend/teammate, so a
        // click can't damage them even when we found nobody behind to hit.
        bool protectTeams = true;
    } settings;

private:
    // Outcome of one ray pass over the player list.
    struct TargetPick {
        jobject entity = nullptr;  // Local ref; the caller owns it.
        bool visible = false;
        // The winning target is farther along the ray than the nearest
        // friend/teammate, i.e. we pierced through somebody.
        bool behindFriendly = false;
        // Entity IDs of every friend/teammate currently in the player list.
        // Used to recognise vanilla's crosshair pick without a second JNI sweep.
        std::vector<int> friendlyIds;
    };

    static std::uint64_t nowMs();
    TargetPick selectTarget(bool hiddenOnly);

    // True when the candidate is a friend or a teammate under the current
    // settings. Single source of truth for both the "skip as a target" pass and
    // the "protect from the crosshair" pass, so the two can never disagree.
    bool isFriendly(const PlayerInfo& candidate, const std::string& localName, ChatColor localTeamColor) const;

    // Point vanilla's objectMouseOver at the pierced target, or blank it when it
    // holds a friend/teammate we refuse to hit. Returns true if it wrote MISS.
    bool applyCrosshairOverride(JNIEnv* env, const TargetPick& pick);

    static bool rayIntersectsBox(double originX, double originY, double originZ,
                                 double directionX, double directionY, double directionZ,
                                 double minX, double minY, double minZ,
                                 double maxX, double maxY, double maxZ,
                                 double maxDistance, double& hitDistance);

    std::atomic<bool> manualAttackActive_{false};
    std::atomic<std::uint64_t> suppressDuplicatePacketsUntilMs_{0};
    std::atomic<std::uint64_t> lastAttackMs_{0};
};

extern Piercing* piercing;
