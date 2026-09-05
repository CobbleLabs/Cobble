#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "../module.h"

struct PlayerInfo;

// AntiBot — the single place that decides whether an entity is a real player.
//
// This used to be a checkbox inside AimAssist, which meant every combat module had its own
// idea of what a bot was. Now every targeting module asks the same question through Filtered(),
// so one switch governs the whole client and the heuristics can be tuned in one place.
//
// Every check runs off a PlayerInfo snapshot: no JNI, no locks, no allocation. That is a
// hard requirement — Filtered() is called from the async module loop at ~500 Hz while it
// walks the player cache.
class AntiBot : public Module {
public:
    AntiBot();

    void onRenderMenu() override;
    void onWorldChange() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;
    std::string getDisplayInfo() override;

    // True when this player should be treated as a bot and skipped.
    //
    // Null-safe and disabled-safe: returns false when the module does not exist or is off,
    // so callers need no guard of their own. Note what that means — with AntiBot disabled
    // the combat modules will happily target NPCs, which is the point of it being a module.
    static bool Filtered(const PlayerInfo& p);

    struct Settings {
        // ---- Checks ----
        // Name-based detection, i.e. the shared PlayerInfo::isNPC flag: an explicit [NPC]
        // tag, an empty or single-character name, a name that is nothing but a colour code,
        // or a name that ends in a reset code it never opened. Cheap and near-zero false
        // positives, which is why it is the default.
        bool nameTag = true;
        // Citizens — the NPC plugin almost every server runs — stamps its NPCs with a
        // version-2 UUID. Real accounts are version 4 (Mojang) or version 3 (offline mode),
        // so this never touches a human. Also catches the all-zero UUID.
        bool npcUuid = true;
        // The GameProfile name is not a name Mojang would ever issue (3-16 characters of
        // letters, digits and underscore). Skipped entirely when the profile name could not
        // be read, so a missing mapping degrades to "no check" instead of "everyone is a bot".
        bool fakeProfile = true;
        // The server never sent health attributes for this entity. Real players always have
        // a max health; fake entities spawned only to be looked at often have none.
        bool noHealth = true;
        // Off by default, and it should stay off on any server with potions: this flags
        // *every* invisible player, not just bots.
        bool invisible = false;
    } settings;

    // Per-check reject counters, shown on the settings page so the heuristics can be tuned
    // against a real server instead of guessed at. Reset on world change.
    struct Stats {
        std::uint32_t nameTag = 0;
        std::uint32_t npcUuid = 0;
        std::uint32_t fakeProfile = 0;
        std::uint32_t noHealth = 0;
        std::uint32_t invisible = 0;
    };
    Stats stats() const;
    void resetStats();

private:
    bool evaluate(const PlayerInfo& p) const;

    // mutable: counted from a const evaluate() on the async loop, and atomic stores are not
    // const-qualified.
    mutable std::atomic<std::uint32_t> hitsNameTag_{0};
    mutable std::atomic<std::uint32_t> hitsNpcUuid_{0};
    mutable std::atomic<std::uint32_t> hitsFakeProfile_{0};
    mutable std::atomic<std::uint32_t> hitsNoHealth_{0};
    mutable std::atomic<std::uint32_t> hitsInvisible_{0};
    mutable std::atomic<std::uint64_t> lastHitMs_{0};
};

extern AntiBot* antiBot;
