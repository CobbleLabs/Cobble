#pragma once
//
// HealthPredictor — estimates a target player's real HP when the server doesn't
// sync it (enemy health is 20 client-side on most servers). It subtracts an
// estimated damage per landed hit (weapon base + sharpness, reduced by the
// target's armor points and protection), regenerates slowly, and snaps to the
// real value whenever the server actually exposes it (a non-full health field,
// e.g. a scoreboard/nametag health number).
//
// Query-driven and self-contained: ESP, NameTags and the TargetHUD all call
// query(); a per-entity per-millisecond guard makes the multiple calls per frame
// idempotent, so only the first advances the model.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "../sdk/core/minecraft.h"

class HealthPredictor {
public:
    static HealthPredictor& get() {
        static HealthPredictor inst;
        return inst;
    }

    // Returns the predicted current HP for this player. `myWeaponId` is the local
    // player's held item id (for the damage estimate). `mySharpness` is the local
    // weapon's Sharpness level (0 if unknown).
    float query(const PlayerInfo& p, int myWeaponId, int mySharpness = 0) {
        const uint64_t now = nowMs();
        State& s = states_[p.entityId];

        if (!s.init) {
            s.hp = p.health;
            s.maxHp = (p.maxHealth > 0.5f ? p.maxHealth : 20.0f);
            s.lastHurt = p.hurtTime;
            s.lastMs = now;
            s.lastHitMs = now;
            s.init = true;
            return s.hp;
        }

        // Advance the model at most once per millisecond (i.e. once per frame),
        // so ESP + NameTags + TargetHUD querying the same entity don't triple-count.
        if (now > s.lastMs) {
            s.maxHp = (p.maxHealth > 0.5f ? p.maxHealth : s.maxHp);

            // Respawn / big heal: server shows (near) full again.
            if (p.health >= s.maxHp - 0.01f && s.hp < s.maxHp - 1.0f)
                s.hp = p.health;

            // Landed-hit edge: hurtTime jumps up to (near) max right after a hit.
            if (p.hurtTime > s.lastHurt && p.hurtTime >= 9) {
                s.hp -= estimateDamage(myWeaponId, mySharpness, armorPoints(p.armor), protLevels(p.armor));
                s.lastHitMs = now;
            }
            s.lastHurt = p.hurtTime;

            // Natural regen (~1 HP / 4s) once out of combat.
            if (s.hp < s.maxHp && now - s.lastHitMs > 4000)
                s.hp += static_cast<float>(now - s.lastMs) / 4000.0f;

            // Server truth: if it exposes a non-full health, trust it (ease in).
            if (p.health < s.maxHp - 0.01f)
                s.hp = 0.6f * s.hp + 0.4f * p.health;

            s.hp = std::clamp(s.hp, 0.0f, s.maxHp);
            s.lastMs = now;
        }
        return s.hp;
    }

    void reset() { states_.clear(); }

private:
    struct State {
        bool init = false;
        float hp = 20.0f;
        float maxHp = 20.0f;
        int lastHurt = 0;
        uint64_t lastMs = 0;
        uint64_t lastHitMs = 0;
    };
    std::unordered_map<int, State> states_;

    static uint64_t nowMs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    // 1.8 melee base damage of the held item (sword/axe), fist = 1.
    static float weaponBaseDamage(int id) {
        switch (id) {
            case 268: case 283: return 4.0f;  // wood / gold sword
            case 272: return 5.0f;            // stone sword
            case 267: return 6.0f;            // iron sword
            case 276: return 7.0f;            // diamond sword
            case 271: case 286: return 3.0f;  // wood / gold axe
            case 275: return 4.0f;            // stone axe
            case 258: return 5.0f;            // iron axe
            case 279: return 6.0f;            // diamond axe
            default: return 1.0f;             // fist / other
        }
    }

    // 1.8 armor defense points per equipped piece id.
    static int armorPoints(const std::vector<PlayerInfo::ArmorItem>& armor) {
        int pts = 0;
        for (const auto& a : armor) {
            switch (a.id) {
                // Helmets
                case 298: pts += 1; break;  case 302: case 306: case 314: pts += 2; break;  case 310: pts += 3; break;
                // Chestplates
                case 299: pts += 3; break;  case 303: case 315: pts += 5; break;  case 307: pts += 6; break;  case 311: pts += 8; break;
                // Leggings
                case 300: pts += 2; break;  case 316: pts += 3; break;  case 304: pts += 4; break;  case 308: pts += 5; break;  case 312: pts += 6; break;
                // Boots
                case 301: case 305: case 317: pts += 1; break;  case 309: pts += 2; break;  case 313: pts += 3; break;
                default: break;
            }
        }
        return pts;
    }

    static int protLevels(const std::vector<PlayerInfo::ArmorItem>& armor) {
        int lvl = 0;
        for (const auto& a : armor)
            lvl += a.protection;
        return lvl;
    }

    static float estimateDamage(int weaponId, int sharpness, int armorPts, int protLevels) {
        float base = weaponBaseDamage(weaponId) + std::max(0, sharpness) * 1.25f;  // 1.8 sharpness
        float armorRed = 1.0f - std::min(20, armorPts) * 0.04f;                     // 1.8 armor formula
        float protRed = 1.0f - std::min(0.8f, protLevels * 0.04f);                  // approx EPF reduction
        return std::max(0.5f, base * armorRed * protRed);
    }
};
