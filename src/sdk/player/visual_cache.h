#pragma once

#include <mutex>
#include <vector>

#include "../core/minecraft.h"
#include "player.h"

struct VisualPlayer {
    std::string uuid;
    std::string name;
    int teamColor = -1;
    double posX = 0.0;
    double posY = 0.0;
    double posZ = 0.0;
    double lastPosX = 0.0;
    double lastPosY = 0.0;
    double lastPosZ = 0.0;
    float health = 20.0f;
    float maxHealth = 20.0f;
    std::vector<PlayerInfo::ArmorItem> armor;
    std::vector<PlayerInfo::PotionEffectInfo> potions;
};

struct VisualSnapshot {
    std::vector<VisualPlayer> players;
    RenderPos renderPos;
    float partialTicks = 0.0f;
    double localX = 0.0;
    double localY = 0.0;
    double localZ = 0.0;
    double localLastX = 0.0;
    double localLastY = 0.0;
    double localLastZ = 0.0;
    bool valid = false;
};

namespace VisualCache {
inline std::mutex mutex;
inline std::vector<VisualPlayer> cachedPlayers;
inline RenderPos cachedRenderPos;
inline float cachedPartialTicks = 0.0f;
inline double cachedLocalX = 0.0;
inline double cachedLocalY = 0.0;
inline double cachedLocalZ = 0.0;
inline double cachedLocalLastX = 0.0;
inline double cachedLocalLastY = 0.0;
inline double cachedLocalLastZ = 0.0;
inline bool cachedValid = false;

inline void Update() {
    if (!Java::Env() || !Java::Initialized)
        return;

    if (!player.isValid()) {
        std::lock_guard<std::mutex> lock(mutex);
        cachedPlayers.clear();
        cachedValid = false;
        return;
    }

    RenderPos renderPos = mc.getRenderPos();
    float partialTicks = mc.getPartialTicks();

    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();
    double localLastX = player.getLastPosX();
    double localLastY = player.getLastPosY();
    double localLastZ = player.getLastPosZ();

    auto players = mc.getPlayerList();
    std::vector<VisualPlayer> newPlayers;
    newPlayers.reserve(players.size());

    for (auto& p : players) {
        VisualPlayer cached;
        cached.uuid = p.uuid;
        cached.name = p.name;
        cached.teamColor = p.teamColor;
        cached.posX = p.posX;
        cached.posY = p.posY;
        cached.posZ = p.posZ;
        cached.lastPosX = p.lastPosX;
        cached.lastPosY = p.lastPosY;
        cached.lastPosZ = p.lastPosZ;
        cached.health = p.health;
        cached.maxHealth = p.maxHealth;
        cached.armor = std::move(p.armor);
        cached.potions = std::move(p.potions);
        newPlayers.push_back(std::move(cached));

        if (p.instance) {
            Java::Env()->DeleteLocalRef(p.instance);
            p.instance = nullptr;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        cachedPlayers = std::move(newPlayers);
        cachedRenderPos = renderPos;
        cachedPartialTicks = partialTicks;
        cachedLocalX = localX;
        cachedLocalY = localY;
        cachedLocalZ = localZ;
        cachedLocalLastX = localLastX;
        cachedLocalLastY = localLastY;
        cachedLocalLastZ = localLastZ;
        cachedValid = true;
    }
}

inline VisualSnapshot GetSnapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    VisualSnapshot snap;
    snap.players = cachedPlayers;
    snap.renderPos = cachedRenderPos;
    snap.partialTicks = cachedPartialTicks;
    snap.localX = cachedLocalX;
    snap.localY = cachedLocalY;
    snap.localZ = cachedLocalZ;
    snap.localLastX = cachedLocalLastX;
    snap.localLastY = cachedLocalLastY;
    snap.localLastZ = cachedLocalLastZ;
    snap.valid = cachedValid;
    return snap;
}
}  // namespace VisualCache
