#include "autorod.h"

#include "antibot.h"
#include "autoclicker.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kDeg = 57.29577951308232;  // 180 / pi

// The hook has to arrive where we are already pointing: this module never rotates
// (the aim controller owns the view), so a wide cone would only throw hooks past people.
constexpr double kMaxCastAngle = 8.0;
// Ticks between the swap back and the follow-up swing: one for C09(weapon) to reach
// the server, one for the knockback to have actually moved them.
constexpr int kHitDelayTicks = 2;
// The rod is never held for less than this, and never for longer.
constexpr int kMinHoldTicks = 3;
constexpr int kMaxHoldTicks = 12;
// Inside this the flight is cut short: they are in sword range now, so whatever the
// hook was going to do, the swing is worth more than the wait.
constexpr double kMeleeAbortDistance = 3.2;

float wrapDeg(float degrees) {
    degrees = std::fmod(degrees + 180.0f, 360.0f);
    if (degrees < 0.0f)
        degrees += 360.0f;
    return degrees - 180.0f;
}

// Frees every GlobalRef getPlayerList() handed out. Not optional and not defensive:
// the list allocates one reference per player, nothing else reclaims them, and one
// missed pass per tick overflows the JVM global-ref table within a minute of play.
struct PlayerListRefs {
    std::vector<PlayerInfo>& list;
    ~PlayerListRefs() {
        JNIEnv* env = Java::Env();
        if (!env)
            return;
        for (auto& p : list) {
            if (p.instance) {
                env->DeleteGlobalRef(p.instance);
                p.instance = nullptr;
            }
        }
    }
};

}  // namespace

AutoRod* autoRod = nullptr;

AutoRod::AutoRod() : Module("AutoRod"), rng_(std::random_device{}()) {
    keybind = 0;
}

bool AutoRod::isWeapon(int itemId) {
    switch (itemId) {
        case 267: case 268: case 272: case 276: case 283:
        case 258: case 271: case 275: case 279: case 286:
            return true;
        default:
            return false;
    }
}

int AutoRod::findRodSlot() const {
    for (int slot = 0; slot < 9; ++slot) {
        if (player.getItemInSlot(slot) == 346)
            return slot;
    }
    return -1;
}

// EntityFishHook leaves the hand at 1.5 blocks/tick and keeps 92% of its speed every
// tick, so n ticks after the cast it has covered 18.75 * (1 - 0.92^n) blocks:
//
//     ticks:  1     2     3     4     5     6     7     8
//     blocks: 1.5   2.9   4.2   5.3   6.4   7.4   8.3   9.1
//
// Inverting that gives the ticks needed to reach a distance. Two ticks of slack cover
// the tick the hook entity spawns on and the fact that the server's copy of the target
// is always a little behind ours.
int AutoRod::hookFlightTicks(double distance) {
    const double reach = std::clamp(distance, 0.0, 16.0);
    const double remaining = 1.0 - reach / 18.75;
    int ticks = kMaxHoldTicks;
    if (remaining > 0.0) {
        const double solved = std::log(remaining) / std::log(0.92);
        if (std::isfinite(solved))
            ticks = (int)std::ceil(solved) + 2;
    }
    return std::clamp(ticks, kMinHoldTicks, kMaxHoldTicks);
}

bool AutoRod::measure(const PlayerInfo& target, double& distanceOut, double& angleOut) const {
    const double eyeY = player.getPosY() + (player.isSneaking() ? 1.54 : 1.62);
    const double dx = target.posX - player.getPosX();
    const double dz = target.posZ - player.getPosZ();

    // Aim at the upper body, not the feet: that is where the hook has to arrive, and
    // measuring to the feet reads as a far steeper angle than the crosshair really is.
    double bodyHeight = 1.8;
    const double hitboxHeight = (double)target.hitboxMaxY - (double)target.hitboxMinY;
    if (hitboxHeight > 0.1 && hitboxHeight < 4.0)
        bodyHeight = hitboxHeight;
    const double dy = (target.posY + bodyHeight * 0.78) - eyeY;

    const double flat = std::sqrt(dx * dx + dz * dz);
    distanceOut = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distanceOut) || distanceOut < 1e-4)
        return false;

    const float yawTo = (float)(std::atan2(-dx, dz) * kDeg);
    const float pitchTo = (float)(-std::atan2(dy, std::max(flat, 1e-4)) * kDeg);
    const float yawDelta = std::fabs(wrapDeg(yawTo - player.getYaw()));
    const float pitchDelta = std::fabs(pitchTo - player.getPitch());
    angleOut = std::sqrt((double)yawDelta * yawDelta + (double)pitchDelta * pitchDelta);
    return std::isfinite(angleOut);
}

int AutoRod::pickTarget(double& distanceOut) const {
    distanceOut = 0.0;
    std::vector<PlayerInfo> players = mc.getPlayerList();
    PlayerListRefs release{players};
    if (players.empty())
        return 0;

    const double minDistance = std::clamp((double)settings.minDistance, 1.0, 20.0);
    const double maxDistance =
        std::max(minDistance + 0.5, std::clamp((double)settings.maxDistance, 1.0, 20.0));

    int bestId = 0;
    double bestAngle = 1e30;
    for (const auto& p : players) {
        if (!p.instance || p.isNPC || p.isInvisible || p.isOnLocalTeam)
            continue;
        if (p.health <= 0.0f)
            continue;
        if (AntiBot::Filtered(p))
            continue;
        if (friendsManager && !p.uuid.empty() &&
            (friendsManager->isFriendByUUID(p.uuid) || friendsManager->isApolloTeammate(p.uuid) ||
             friendsManager->isLunarTeammate(p.uuid)))
            continue;

        double distance = 0.0;
        double angle = 0.0;
        if (!measure(p, distance, angle))
            continue;
        // Below the band a swing is strictly better and the rod would be costing us
        // one; above it the hook's own gravity drops it out of the hitbox before it
        // arrives. Either way the cast is worse than not casting.
        if (distance < minDistance || distance > maxDistance)
            continue;
        if (angle > kMaxCastAngle || angle >= bestAngle)
            continue;
        // The hook stops at the first block it hits, so a target behind cover is a
        // wasted cast, a wasted cooldown, and a sword left in the wrong hand.
        if (!mc.canEntityBeSeen(p.instance))
            continue;

        bestAngle = angle;
        distanceOut = distance;
        bestId = p.entityId;
    }
    return bestId;
}

double AutoRod::distanceToId(int entityId) const {
    if (entityId == 0)
        return -1.0;
    std::vector<PlayerInfo> players = mc.getPlayerList();
    PlayerListRefs release{players};
    for (const auto& p : players) {
        if (p.entityId != entityId)
            continue;
        if (p.health <= 0.0f)
            return -1.0;
        const double dx = p.posX - player.getPosX();
        const double dy = p.posY - player.getPosY();
        const double dz = p.posZ - player.getPosZ();
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        return std::isfinite(distance) ? distance : -1.0;
    }
    return -1.0;
}

// Deterministic delays are the cheapest thing in the module to fingerprint: a
// histogram of the inter-packet gaps collapses to one spike, identical on every
// sequence. Spread every delay that is a choice rather than a physical constant --
// which is why the rod-hold length below is NOT jittered: shortening it kills the
// hook in flight and lengthening it only wastes melee time.
int AutoRod::jitterTicks(int baseTicks) {
    const int base = std::max(1, baseTicks);
    const int spread = std::clamp(settings.jitterPercent, 0, 90);
    if (spread == 0)
        return base;
    // An integer percentage of a 2-3 tick delay rounds to zero, so guarantee at
    // least +-1 tick of play.
    const int amp = std::max(1, (base * spread) / 100);
    std::uniform_int_distribution<int> dist(-amp, amp);
    return std::max(1, base + dist(rng_));
}

int AutoRod::rollCooldownMs() {
    const int base = std::clamp(settings.cooldownMs, 400, 6000);
    const int spread = std::clamp(settings.jitterPercent, 0, 90);
    if (spread == 0)
        return base;
    const int amp = std::max(1, (base * spread) / 100);
    std::uniform_int_distribution<int> dist(-amp, amp);
    return std::max(300, base + dist(rng_));
}

// Tears the sequence down and starts the cooldown. EVERY exit goes through here,
// completions and bail-outs alike, because a bail-out that left the cooldown alone
// would let the very next click retry immediately -- and a trigger that retries on
// every click is the slot-churn this module is not allowed to produce.
void AutoRod::abortSequence(bool restoreSlot) {
    if (restoreSlot && rodSlot_ >= 0 && weaponSlot_ >= 0 && player.getCurrentSlot() == rodSlot_)
        player.setCurrentSlot(weaponSlot_);
    if (phase_ != Phase::Idle) {
        lastSequence_ = std::chrono::steady_clock::now();
        cooldownMsThisRound_ = 0;  // rerolled on the next attempt
    }
    phase_ = Phase::Idle;
    waitTicks_ = 0;
    rodSlot_ = weaponSlot_ = -1;
    targetId_ = 0;
}

void AutoRod::onEnable() {
    phase_ = Phase::Idle;
    waitTicks_ = 0;
    rodSlot_ = weaponSlot_ = -1;
    targetId_ = 0;
    prevAttackDown_ = false;
    requestPending_.store(false, std::memory_order_relaxed);
    observedAutoClick_ = autoClicker ? autoClicker->lastAutoClick : std::chrono::steady_clock::time_point{};
    cooldownMsThisRound_ = 0;
    lastSequence_ = std::chrono::steady_clock::now() - std::chrono::hours(1);
}

void AutoRod::onDisable() {
    // A bare slot restore is safe off the client thread: it is a single write to
    // inventory.currentItem, exactly what a hotbar keypress does, and there is no
    // pending use-item for it to desync from.
    abortSequence(true);
    requestPending_.store(false, std::memory_order_relaxed);
}

// Module thread (~500 Hz). This must not touch the game: hotbar changes, use-item
// and attacks all belong to onClientTick() so they land tick-aligned and in vanilla
// packet order. All that happens here is latching the attack intent.
void AutoRod::onUpdate() {
    if (!enabled || Menu::isOpen) {
        prevAttackDown_ = false;
        requestPending_.store(false, std::memory_order_relaxed);
        return;
    }

    const bool attackDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool attackPressed = attackDown && !prevAttackDown_;
    prevAttackDown_ = attackDown;

    bool autoClick = false;
    if (autoClicker && autoClicker->lastAutoClick != observedAutoClick_) {
        observedAutoClick_ = autoClicker->lastAutoClick;
        autoClick = true;
    }

    if (attackPressed || autoClick || !settings.onlyWhileAttacking)
        requestPending_.store(true, std::memory_order_release);
}

void AutoRod::onClientTick(JNIEnv* env) {
    (void)env;
    if (!enabled || !player.isValid() || mc.isInGui()) {
        abortSequence(true);
        requestPending_.store(false, std::memory_order_relaxed);
        return;
    }

    // One transition per tick, and each phase owns its own wait counter rather than a
    // shared decrement at the top: HookFlying has to be able to look at the world on
    // every tick it is waiting, not just on the tick it expires.
    switch (phase_) {
        case Phase::RodSelected: {
            if (waitTicks_ > 0) {
                --waitTicks_;
                return;
            }
            // syncCurrentPlayItem() ran at the top of this tick, so C09(rod) is on the
            // wire and the rod really is the held item server-side.
            if (player.getCurrentSlot() != rodSlot_ || mc.getRightClickDelay() > 0) {
                abortSequence(true);
                return;
            }
            // Re-measure one tick later than the pick: the slot change cost a tick and
            // the flight length is derived from where they are NOW, not from where they
            // were when the trigger fired.
            const double distance = distanceToId(targetId_);
            if (distance < 0.0 || distance <= kMeleeAbortDistance) {
                // They died, left, or closed the gap while we were switching. Casting
                // now would hand them a free hit against a rod.
                abortSequence(true);
                return;
            }
            // Minecraft.rightClickMouse() sets rightClickDelayTimer = 4 itself, and
            // that is what keeps the use-item cadence vanilla. The old code zeroed
            // the timer first and never restored it, so every right-click for the
            // rest of the session bypassed the 4-tick cooldown.
            mc.rightClickMouse();
            phase_ = Phase::HookFlying;
            waitTicks_ = hookFlightTicks(distance) - 1;
            return;
        }

        case Phase::HookFlying: {
            // Nothing happens in this phase, and that is the whole point: the hook only
            // exists for as long as the rod is in hand. EntityFishHook.onUpdate kills
            // the bobber on the first tick the angler is holding anything else, so
            // swapping back early -- which is what this module used to do -- threw the
            // hook away in flight and landed nothing on the other player at all.
            if (player.getCurrentSlot() != rodSlot_) {
                // The player took the hotbar back. The bobber went with it; do not
                // fight them for the slot.
                abortSequence(false);
                return;
            }
            const double distance = distanceToId(targetId_);
            const bool targetGone = distance < 0.0;
            const bool inMelee = distance >= 0.0 && distance <= kMeleeAbortDistance;
            if (waitTicks_ > 0 && !targetGone && !inMelee) {
                --waitTicks_;
                return;
            }
            if (weaponSlot_ >= 0)
                player.setCurrentSlot(weaponSlot_);
            phase_ = Phase::WeaponSelected;
            waitTicks_ = jitterTicks(kHitDelayTicks) - 1;
            return;
        }

        case Phase::WeaponSelected:
            if (waitTicks_ > 0) {
                --waitTicks_;
                return;
            }
            if (settings.autoHit && weaponSlot_ >= 0 && player.getCurrentSlot() == weaponSlot_ &&
                mc.isTargetingEntity()) {
                mc.clickMouse();
            }
            abortSequence(false);
            return;

        case Phase::Idle:
            break;
    }

    // ---- Idle: consider starting a sequence. ----
    if (!requestPending_.exchange(false, std::memory_order_acq_rel))
        return;

    if (cooldownMsThisRound_ == 0)
        cooldownMsThisRound_ = rollCooldownMs();
    if (std::chrono::steady_clock::now() - lastSequence_ < std::chrono::milliseconds(cooldownMsThisRound_))
        return;

    // A human could not have right-clicked during vanilla's own cooldown either.
    if (mc.getRightClickDelay() > 0)
        return;

    const int currentSlot = player.getCurrentSlot();
    if (settings.requireWeapon && !isWeapon(player.getHeldItemId()))
        return;
    const int rodSlot = findRodSlot();
    if (rodSlot < 0 || rodSlot == currentSlot)
        return;

    // Last, because it is the only expensive test in the ladder: one player-list walk,
    // and only once everything cheap has already agreed a cast is possible.
    double distance = 0.0;
    const int target = pickTarget(distance);
    if (target == 0)
        return;

    weaponSlot_ = currentSlot;
    rodSlot_ = rodSlot;
    targetId_ = target;
    player.setCurrentSlot(rodSlot_);
    phase_ = Phase::RodSelected;
    // Cast on the next tick, once C09(rod) has gone out. The cooldown is NOT stamped
    // here: it is stamped when the sequence ends, so its spacing measures the gap
    // between sequences instead of being eaten by the length of one.
    waitTicks_ = 0;
}

std::string AutoRod::getDisplayInfo() {
    switch (phase_) {
        case Phase::RodSelected:
            return "Cast";
        case Phase::HookFlying:
            return "Hook";
        case Phase::WeaponSelected:
            return "Hit";
        case Phase::Idle:
            break;
    }
    return "Ready";
}

void AutoRod::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::Checkbox("Only While Attacking", &settings.onlyWhileAttacking);
            ui::Checkbox("Require Weapon", &settings.requireWeapon);
            ui::Checkbox("Auto Hit", &settings.autoHit);
        },
        "Range",
        [this]() {
            ui::SliderFloat("Min Distance", &settings.minDistance, 3.0f, 6.0f, "%.1f");
            ui::SliderFloat("Max Distance", &settings.maxDistance, 5.0f, 12.0f, "%.1f");
            ui::SliderInt("Cooldown (ms)", &settings.cooldownMs, 400, 6000, "%d");
            ui::SliderInt("Timing Jitter (%)", &settings.jitterPercent, 0, 90, "%d");
        });
}

void AutoRod::saveSettings(json::Value& node) const {
    node["onlyWhileAttacking"] = settings.onlyWhileAttacking;
    node["requireWeapon"] = settings.requireWeapon;
    node["autoHit"] = settings.autoHit;
    node["minDistance"] = settings.minDistance;
    node["maxDistance"] = settings.maxDistance;
    node["cooldownMs"] = settings.cooldownMs;
    node["jitterPercent"] = settings.jitterPercent;
}

void AutoRod::loadSettings(const json::Value& node) {
    settings.onlyWhileAttacking = node["onlyWhileAttacking"].as_bool(settings.onlyWhileAttacking);
    settings.requireWeapon = node["requireWeapon"].as_bool(settings.requireWeapon);
    settings.autoHit = node["autoHit"].as_bool(settings.autoHit);
    settings.minDistance = std::clamp(node["minDistance"].as_float(settings.minDistance), 3.0f, 6.0f);
    settings.maxDistance = std::clamp(node["maxDistance"].as_float(settings.maxDistance), 5.0f, 12.0f);
    settings.cooldownMs = node["cooldownMs"].as_int(settings.cooldownMs);
    settings.jitterPercent = node["jitterPercent"].as_int(settings.jitterPercent);
}
