#pragma once

#include <string>
#include <vector>

#include "../core/minecraft.h"

namespace EffectTracker {

// Clears all tracked effects (e.g., world change).
void Clear();

// Advances durations using wall-clock time (20 ticks/sec).
// Call this periodically from the main loop.
void Tick();

// Update or insert an effect for a given entity id.
void OnEffectAdd(int entityId, int effectId, int amplifier, int durationTicks);

// Remove a single effect id for a given entity id.
void OnEffectRemove(int entityId, int effectId);

// Associate an entity id with a UUID once known. This lets the tracker keep effects stable
// across respawns or server-side entity id changes.
void AssociateEntity(int entityId, const std::string& uuid);

// Get currently tracked effects for an entity. Returns empty if none.
std::vector<PlayerInfo::PotionEffectInfo> GetEffects(int entityId);

// Debug toggle (controls console logging inside the tracker).
void SetDebug(bool enabled);

}  // namespace EffectTracker
