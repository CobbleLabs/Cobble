#pragma once

#include <string>
#include "../module.h"

// Teams — decides who counts as a teammate, and from which evidence.
//
// The detection itself lives in FriendsManager because everything that needs it
// (AimAssist, Piercing, Reach, ESP, Pings) already reads from
// there off the async loop. This module is the editor for that one policy struct,
// not a second copy of it: flipping a switch here changes what every consumer sees
// on its next scan, with no per-module plumbing and nothing to push each tick.
//
// Four independent sources of evidence, strongest first:
//   Lunar API      UpdateTeamMembersMessage — the server states the roster outright.
//   Apollo nametag OverrideNametagMessage — a green tag, confirmed over several
//                  signals so cosmetic colour cycling cannot fake one.
//   Scoreboard     S3EPacketTeams membership, matched by name.
//   Nametag colour The player's prefix colour matches yours, or a colour you picked.
//
// Plus the vanilla same-team flag the game computes itself (Entity.isOnSameTeam), which
// gets its own switch because it arrives from the game rather than from a packet we parse.
//
// Turning the module off means "no team awareness at all" — including that vanilla flag,
// which every combat module would otherwise honour regardless of what Cobble thinks.
class Teams : public Module {
public:
    Teams();

    void onEnable() override;
    void onDisable() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;
    std::string getDisplayInfo() override;

private:
    // Mirrors `enabled` into the policy struct. Called from both toggle paths and
    // after a config load, because ConfigManager only calls setEnabled() when the
    // value actually changes — a config that agrees with the default would otherwise
    // leave the policy at whatever the legacy `friends` block put there.
    void pushEnabled() const;
};

extern Teams* teamsModule;
