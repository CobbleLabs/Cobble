#include "opportunity.h"

#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"
#include "../utility/autopot.h"
#include "../utility/clutch.h"
#include "../utility/throwpots.h"
#include "autorod.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

Opportunity* opportunity = nullptr;

namespace {
constexpr double kPi = 3.14159265358979323846;

// Vanilla caps a held right-click at one use every 4 ticks. We never touch that
// timer; this is only the floor our own scheduling adds on top of it.
constexpr std::uint64_t kVanillaUseIntervalMs = 200;

// A flick that has not converged in this many ticks never will; give the camera back
// rather than hold it hostage over one web.
constexpr int kMaxAimTicks = 12;

struct Direction {
    int x;
    int y;
    int z;
};

// A direction points FROM the block we click TO the cell the item lands in, so
// {0,1,0} means "click the top face of the block below the target cell". That one is
// first because it is the shortest flick from a normal fighting view and the placement
// most likely to survive server-side.
constexpr std::array<Direction, 6> kDirections = {
    Direction{0, 1, 0},  Direction{1, 0, 0},  Direction{-1, 0, 0},
    Direction{0, 0, 1},  Direction{0, 0, -1}, Direction{0, -1, 0}};

// Items that carry each play, in preference order. 0 = no alternative.
//   Cobweb 30, lava bucket 327, flint and steel 259 / fire charge 385, water bucket 326.
constexpr int kPlayItems[Opportunity::kPlayCount][2] = {
    {30, 0}, {327, 0}, {259, 385}, {326, 0}};

inline bool finite3(double a, double b, double c) {
    return !std::isnan(a) && !std::isnan(b) && !std::isnan(c) && !std::isinf(a) &&
           !std::isinf(b) && !std::isinf(c);
}

// Does a block of this id stop a line of sight and give a face worth clicking? The
// rejected ids are the ones that are neither: plants, rails, redstone parts, signs,
// carpets, fluids, snow layers -- you can see straight through them and you cannot
// place against them either.
inline bool blocksSight(int id) {
    if (id <= 0 || id > 255)
        return false;
    static constexpr int kSeeThrough[] = {
        6,   8,   9,   10,  11,  27,  28,  30,  31,  32,  37,  38,  39,  40,  50,
        51,  55,  59,  63,  65,  66,  68,  69,  70,  72,  75,  76,  77,  78,  83,
        104, 105, 106, 111, 115, 119, 127, 131, 132, 140, 141, 142, 143, 147, 148,
        149, 150, 157, 171, 175};
    for (int seeThrough : kSeeThrough) {
        if (seeThrough == id)
            return false;
        if (seeThrough > id)
            break;  // sorted
    }
    return true;
}

}  // namespace

Opportunity::Opportunity() : Module("Opportunity") {
    usesPlayerCache = true;

    // Per-play defaults. The windows differ because the plays differ: a web goes in
    // their feet at melee range, a bucket of lava is a thing you set up from slightly
    // further out and only when it is worth the bucket.
    settings.play[static_cast<int>(Play::Cobweb)] = {true, 1.0f, 4.0f, 20.0f, 85, 2500};
    settings.play[static_cast<int>(Play::Lava)] = {false, 1.5f, 4.0f, 12.0f, 70, 6000};
    settings.play[static_cast<int>(Play::Fire)] = {false, 1.5f, 4.0f, 14.0f, 70, 4000};
    settings.play[static_cast<int>(Play::Water)] = {false, 1.0f, 4.0f, 20.0f, 60, 5000};
}

const char* Opportunity::playName(Play play) {
    switch (play) {
        case Play::Cobweb: return "Cobweb";
        case Play::Lava: return "Lava";
        case Play::Fire: return "Fire";
        case Play::Water: return "Water";
        default: return "?";
    }
}

std::uint64_t Opportunity::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

float Opportunity::wrapDegrees(float angle) {
    while (angle > 180.0f)
        angle -= 360.0f;
    while (angle < -180.0f)
        angle += 360.0f;
    return angle;
}

// 3t^2 - 2t^3: zero velocity at both ends, which is the shape a hand draws. A linear
// ramp has a hard start and a hard stop and reads as synthetic on both.
float Opportunity::smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

int Opportunity::rollReactionMs() {
    const int lo = std::clamp(settings.reactMinMs, 0, 1000);
    const int hi = std::clamp(settings.reactMaxMs, lo, 1000);
    if (hi <= lo)
        return lo;
    // Two uniform draws averaged -- roughly triangular, like a real reaction time, and
    // it never lands on the bounds the way a single uniform draw does.
    const int a = lo + static_cast<int>(rng_() % static_cast<unsigned>(hi - lo + 1));
    const int b = lo + static_cast<int>(rng_() % static_cast<unsigned>(hi - lo + 1));
    return (a + b) / 2;
}

bool Opportunity::rollChance(int percent) {
    const int chance = std::clamp(percent, 0, 100);
    if (chance >= 100)
        return true;
    return static_cast<int>(rng_() % 100u) < chance;
}

void Opportunity::onEnable() {
    phase_ = Phase::Idle;
    shot_ = {};
    originalSlot_ = -1;
    reactDeadlineMs_ = 0;
    restoreSlotAtMs_ = 0;
    nextAnyPlayMs_ = 0;
    aimTick_ = 0;
    aimTotalTicks_ = 0;
    haveReturnView_ = false;
    for (int i = 0; i < kPlayCount; ++i) {
        nextPlayMs_[i] = 0;
        declinedUntilMs_[i] = 0;
    }
    owningRotation_.store(false, std::memory_order_release);
    status_.store(nullptr, std::memory_order_release);
}

void Opportunity::onDisable() {
    // No JNI from the module thread. needsClientTick() stays true while we still own the
    // rotation, so the next client tick runs abort() and hands back the view and the slot.
    status_.store(nullptr, std::memory_order_release);
}

void Opportunity::onWorldChange() {
    // The support face we were aiming at belongs to a world that no longer exists.
    shot_ = {};
    phase_ = Phase::Idle;
    aimTick_ = 0;
    aimTotalTicks_ = 0;
    haveReturnView_ = false;
    nextAnyPlayMs_ = 0;
    for (int i = 0; i < kPlayCount; ++i) {
        nextPlayMs_[i] = 0;
        declinedUntilMs_[i] = 0;
    }
    owningRotation_.store(false, std::memory_order_release);
    status_.store(nullptr, std::memory_order_release);
}

void Opportunity::restoreSlot() {
    if (settings.restoreSlot && originalSlot_ >= 0 && originalSlot_ < 9 && player.isValid())
        player.setCurrentSlot(originalSlot_);
    originalSlot_ = -1;
}

// Unconditional hand-back: switched off mid-sequence, a screen went up, or another
// module took the hotbar. One path, so nothing can be left half-owned.
void Opportunity::abort(JNIEnv* env) {
    (void)env;
    restoreSlot();
    phase_ = Phase::Idle;
    shot_ = {};
    aimTick_ = 0;
    aimTotalTicks_ = 0;
    haveReturnView_ = false;
    owningRotation_.store(false, std::memory_order_release);
    status_.store(nullptr, std::memory_order_release);
}

// Which hotbar slot carries this play, preferring the one already in hand so a play we
// are already equipped for costs no slot change at all.
int Opportunity::findItemSlot(Play play) const {
    const int idx = static_cast<int>(play);
    if (idx < 0 || idx >= kPlayCount)
        return -1;
    for (int variant = 0; variant < 2; ++variant) {
        const int wanted = kPlayItems[idx][variant];
        if (wanted == 0)
            continue;
        if (heldSlot_ >= 0 && heldSlot_ < 9 && hotbar_[heldSlot_] == wanted)
            return heldSlot_;
        for (int slot = 0; slot < 9; ++slot) {
            if (hotbar_[slot] == wanted)
                return slot;
        }
    }
    return -1;
}

// Walk the ray from the eye to the hit point and refuse the shot if anything solid is in
// the way. Stops just short of the face so the support block itself is never the reason
// its own placement is rejected. This is what keeps the module from webbing somebody
// through a wall -- which no anticheat has to catch, because a spectator would.
bool Opportunity::hasLineOfSight(double toX, double toY, double toZ) const {
    const double eyeX = player.getPosX();
    const double eyeY = player.getPosY() + 1.62;
    const double eyeZ = player.getPosZ();
    const double dx = toX - eyeX;
    const double dy = toY - eyeY;
    const double dz = toZ - eyeZ;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!finite3(dx, dy, dz) || dist < 0.2)
        return true;

    const double usable = dist - 0.12;
    const int steps = std::clamp(static_cast<int>(usable * 4.0), 2, 24);
    for (int i = 1; i <= steps; ++i) {
        const double t = usable * (static_cast<double>(i) / steps) / dist;
        const int x = static_cast<int>(std::floor(eyeX + dx * t));
        const int y = static_cast<int>(std::floor(eyeY + dy * t));
        const int z = static_cast<int>(std::floor(eyeZ + dz * t));
        if (y < 0 || y > 255)
            continue;
        if (blocksSight(mc.getBlockId(x, y, z)))
            return false;
    }
    return true;
}

// Find the face to right-click so the item lands in `cell`. Prefers the face that needs
// the least head movement, and never returns one it cannot see.
bool Opportunity::anchorFor(int cellX, int cellY, int cellZ, Shot& out) const {
    if (cellY < 1 || cellY > 254)
        return false;
    if (mc.getBlockId(cellX, cellY, cellZ) != 0)
        return false;  // the cell is already occupied; nothing to put there

    const double eyeX = player.getPosX();
    const double eyeY = player.getPosY() + 1.62;
    const double eyeZ = player.getPosZ();
    const float curYaw = player.getYaw();
    const float curPitch = player.getPitch();
    const double maxReach = static_cast<double>(std::clamp(settings.reach, 3.0f, 4.5f));
    const double maxReachSq = maxReach * maxReach;

    // Fire is removed again on the next block update unless the block under it has a
    // solid top surface, so it only ever clicks that one face.
    const int dirCount =
        (out.play == Play::Fire) ? 1 : static_cast<int>(kDirections.size());

    struct Candidate {
        double hitX, hitY, hitZ;
        int supportX, supportY, supportZ;
        double score;
    };
    Candidate cands[6];
    int count = 0;

    for (int d = 0; d < dirCount; ++d) {
        const Direction& dir = kDirections[d];
        const int supportX = cellX - dir.x;
        const int supportY = cellY - dir.y;
        const int supportZ = cellZ - dir.z;
        if (supportY < 0 || supportY > 255)
            continue;
        if (!blocksSight(mc.getBlockId(supportX, supportY, supportZ)))
            continue;  // no full face there to click

        const double hitX = supportX + 0.5 + dir.x * 0.49;
        const double hitY = supportY + 0.5 + dir.y * 0.49;
        const double hitZ = supportZ + 0.5 + dir.z * 0.49;
        const double dx = hitX - eyeX;
        const double dy = hitY - eyeY;
        const double dz = hitZ - eyeZ;
        const double reachSq = dx * dx + dy * dy + dz * dz;
        if (reachSq > maxReachSq)
            continue;

        // A short flick is a believable flick, and it converges inside the aim budget
        // instead of running out of it.
        const double horiz = std::sqrt(dx * dx + dz * dz);
        const float wantYaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
        const float wantPitch = std::clamp(
            static_cast<float>(-std::atan2(dy, horiz) * 180.0 / kPi), -90.0f, 90.0f);
        const float yawDiff = std::abs(wrapDegrees(wantYaw - curYaw));
        const float pitchDiff = std::abs(wrapDegrees(wantPitch - curPitch));
        const double score =
            reachSq * 0.05 + (yawDiff * yawDiff + pitchDiff * pitchDiff) * 0.0008;

        cands[count++] = {hitX, hitY, hitZ, supportX, supportY, supportZ, score};
    }

    // Best first, and only pay for a line-of-sight trace when a face is actually in
    // the running -- usually that means exactly one trace.
    bool used[6] = {};
    for (int picked = 0; picked < count; ++picked) {
        int best = -1;
        for (int i = 0; i < count; ++i) {
            if (!used[i] && (best < 0 || cands[i].score < cands[best].score))
                best = i;
        }
        if (best < 0)
            break;
        used[best] = true;
        const Candidate& c = cands[best];
        if (settings.requireLineOfSight && !hasLineOfSight(c.hitX, c.hitY, c.hitZ))
            continue;

        out.cellX = cellX;
        out.cellY = cellY;
        out.cellZ = cellZ;
        out.supportX = c.supportX;
        out.supportY = c.supportY;
        out.supportZ = c.supportZ;
        out.hitX = c.hitX;
        out.hitY = c.hitY;
        out.hitZ = c.hitZ;
        out.valid = true;
        return true;
    }
    return false;
}

// Is any play on right now, and if so, where. Returns the shot to take.
bool Opportunity::evaluate(Shot& out) {
    out = {};
    const std::uint64_t now = nowMs();
    if (now < nextAnyPlayMs_)
        return false;

    const VisualSnapshot snap = VisualCache::GetSnapshot();
    if (!snap.valid || snap.players.empty())
        return false;

    const double px = player.getPosX();
    const double py = player.getPosY();
    const double pz = player.getPosZ();
    if (!finite3(px, py, pz))
        return false;

    // Nearest enemy only. Reaching past a closer player to set up a play on a further
    // one is not something a person does with somebody swinging at them.
    const VisualPlayer* target = nullptr;
    double bestDistSq = std::numeric_limits<double>::max();
    for (const VisualPlayer& candidate : snap.players) {
        if (candidate.health <= 0.0f)
            continue;
        if (settings.ignoreInvisible && candidate.isInvisible)
            continue;
        if (settings.ignoreTeam) {
            if (candidate.isOnLocalTeam)
                continue;
            if (friendsManager) {
                if (!candidate.uuid.empty() && friendsManager->isFriendByUUID(candidate.uuid))
                    continue;
                if (friendsManager->isFriend(FriendsManager::stripColorCodes(candidate.name)))
                    continue;
            }
        }
        const double dx = candidate.posX - px;
        const double dy = candidate.posY - py;
        const double dz = candidate.posZ - pz;
        if (!finite3(dx, dy, dz))
            continue;
        const double distSq = dx * dx + dy * dy + dz * dz;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            target = &candidate;
        }
    }
    if (!target)
        return false;

    const double dist = std::sqrt(bestDistSq);
    const int cellX = static_cast<int>(std::floor(target->posX));
    const int cellY = static_cast<int>(std::floor(target->posY));
    const int cellZ = static_cast<int>(std::floor(target->posZ));

    // Collect every play whose window we are inside, then try them in a shuffled order:
    // when two are on at once, always taking the same one is a tell.
    Play ready[kPlayCount];
    int readyCount = 0;
    for (int i = 0; i < kPlayCount; ++i) {
        const PlaySettings& ps = settings.play[i];
        if (!ps.enabled)
            continue;
        if (now < nextPlayMs_[i] || now < declinedUntilMs_[i])
            continue;
        if (dist < ps.minDist || dist > ps.maxDist)
            continue;
        if (target->health > ps.maxTargetHealth)
            continue;
        const Play play = static_cast<Play>(i);
        if (findItemSlot(play) < 0)
            continue;

        if (play == Play::Lava || play == Play::Fire) {
            // Self-preservation. Also the reason a person would not take this shot: at
            // this range the lava is as much ours as theirs.
            const double safe = static_cast<double>(std::clamp(settings.selfSafeDist, 0.0f, 6.0f));
            const double sdx = (cellX + 0.5) - px;
            const double sdy = (cellY + 0.5) - (py + 0.9);
            const double sdz = (cellZ + 0.5) - pz;
            if (sdx * sdx + sdy * sdy + sdz * sdz < safe * safe)
                continue;
        }
        ready[readyCount++] = play;
    }
    if (readyCount == 0)
        return false;

    for (int i = readyCount - 1; i > 0; --i)
        std::swap(ready[i], ready[static_cast<int>(rng_() % static_cast<unsigned>(i + 1))]);

    for (int i = 0; i < readyCount; ++i) {
        Shot candidate;
        candidate.play = ready[i];
        candidate.entityId = target->entityId;
        if (!anchorFor(cellX, cellY, cellZ, candidate))
            continue;
        candidate.slot = findItemSlot(candidate.play);
        if (candidate.slot < 0)
            continue;

        // The chance roll goes last: it is only spent on a play that was genuinely
        // available, and a declined play backs off instead of re-rolling 20 times a
        // second until it passes, which would make the setting mean nothing.
        const int idx = static_cast<int>(candidate.play);
        if (!rollChance(settings.play[idx].chance)) {
            declinedUntilMs_[idx] = now + 250u + (rng_() % 400u);
            continue;
        }
        out = candidate;
        return true;
    }
    return false;
}

// Re-point a shot at where its target is now. Anchors and windows are re-checked; no
// chance roll is spent and no cooldown is touched, because this is the same opportunity.
bool Opportunity::refreshShot(Shot& shot) const {
    if (!shot.valid || shot.entityId == 0)
        return false;
    const int idx = static_cast<int>(shot.play);
    if (idx < 0 || idx >= kPlayCount)
        return false;

    const VisualSnapshot snap = VisualCache::GetSnapshot();
    if (!snap.valid)
        return false;

    const double px = player.getPosX();
    const double py = player.getPosY();
    const double pz = player.getPosZ();
    if (!finite3(px, py, pz))
        return false;

    for (const VisualPlayer& p : snap.players) {
        if (p.entityId != shot.entityId)
            continue;
        if (p.health <= 0.0f)
            return false;

        const PlaySettings& ps = settings.play[idx];
        if (p.health > ps.maxTargetHealth)
            return false;
        const double dx = p.posX - px;
        const double dy = p.posY - py;
        const double dz = p.posZ - pz;
        if (!finite3(dx, dy, dz))
            return false;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < ps.minDist || dist > ps.maxDist)
            return false;

        Shot fresh;
        fresh.play = shot.play;
        fresh.entityId = shot.entityId;
        fresh.slot = shot.slot;
        if (!anchorFor(static_cast<int>(std::floor(p.posX)),
                       static_cast<int>(std::floor(p.posY)),
                       static_cast<int>(std::floor(p.posZ)), fresh)) {
            return false;
        }
        shot = fresh;
        return stillValid(shot);
    }
    return false;  // they left the world
}

// Everything the shot assumed, re-checked against the world as it is now.
bool Opportunity::stillValid(const Shot& shot) const {
    if (!shot.valid || shot.slot < 0 || shot.slot > 8)
        return false;
    if (mc.getBlockId(shot.cellX, shot.cellY, shot.cellZ) != 0)
        return false;
    if (!blocksSight(mc.getBlockId(shot.supportX, shot.supportY, shot.supportZ)))
        return false;

    const int idx = static_cast<int>(shot.play);
    if (idx < 0 || idx >= kPlayCount)
        return false;
    const int held = hotbar_[shot.slot];
    if (held != kPlayItems[idx][0] &&
        (kPlayItems[idx][1] == 0 || held != kPlayItems[idx][1])) {
        return false;  // the stack ran out or moved
    }

    const double dx = shot.hitX - player.getPosX();
    const double dy = shot.hitY - (player.getPosY() + 1.62);
    const double dz = shot.hitZ - player.getPosZ();
    const double reach = static_cast<double>(std::clamp(settings.reach, 3.0f, 4.5f));
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

// Write the rotation the server will see this tick, quantized onto the client's own
// mouse GCD grid relative to where the view is now. Every delta we produce is then one a
// physical mouse could have produced at this sensitivity, which is the property the
// rotation checks actually test.
void Opportunity::applyRotation(float yaw, float pitch) {
    const float curYaw = player.getYaw();
    const float curPitch = player.getPitch();
    const float sensitivity = mc.getMouseSensitivity();
    const float gf = sensitivity * 0.6f + 0.2f;
    const float gcd = gf * gf * gf * 1.2f;

    float outYaw = yaw;
    float outPitch = std::clamp(pitch, -90.0f, 90.0f);
    if (gcd > 0.0001f) {
        outYaw = curYaw + std::round(wrapDegrees(yaw - curYaw) / gcd) * gcd;
        outPitch =
            std::clamp(curPitch + std::round((outPitch - curPitch) / gcd) * gcd, -90.0f, 90.0f);
    }
    player.setYaw(outYaw);
    player.setPitch(outPitch);
}

// Set up the flick from where the view is now to the shot's hit point.
void Opportunity::aimAt(const Shot& shot, bool freshOpportunity) {
    const double dx = shot.hitX - player.getPosX();
    const double dy = shot.hitY - (player.getPosY() + 1.62);
    const double dz = shot.hitZ - player.getPosZ();
    const double horiz = std::sqrt(dx * dx + dz * dz);

    aimToYaw_ = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
    aimToPitch_ =
        std::clamp(static_cast<float>(-std::atan2(dy, horiz) * 180.0 / kPi), -90.0f, 90.0f);
    aimFromYaw_ = player.getYaw();
    aimFromPitch_ = player.getPitch();

    if (freshOpportunity && settings.snapBack && !haveReturnView_) {
        returnYaw_ = aimFromYaw_;
        returnPitch_ = aimFromPitch_;
        haveReturnView_ = true;
    }

    // Scale the flick with the distance it covers. A fixed budget makes a 4-degree
    // correction crawl and a 90-degree whip teleport; both read as synthetic.
    const float travel = std::max(std::abs(wrapDegrees(aimToYaw_ - aimFromYaw_)),
                                  std::abs(aimToPitch_ - aimFromPitch_));
    int budget = std::clamp(settings.aimTicks, 1, 6);
    if (travel < 8.0f)
        budget = 1;
    else if (travel > 60.0f)
        budget += 1;
    aimTotalTicks_ = std::clamp(budget, 1, kMaxAimTicks);
    aimTick_ = 0;
}

// The play is over: flick the view back where it came from and let the hotbar settle
// after a beat, because nobody switches back on the same tick they used the item.
void Opportunity::beginSettle() {
    phase_ = Phase::Settle;
    restoreSlotAtMs_ = nowMs() + 110 + static_cast<std::uint64_t>(rng_() % 180u);

    if (settings.snapBack && haveReturnView_) {
        aimFromYaw_ = player.getYaw();
        aimFromPitch_ = player.getPitch();
        aimToYaw_ = returnYaw_;
        aimToPitch_ = returnPitch_;
        aimTick_ = 0;
        aimTotalTicks_ = std::clamp(settings.aimTicks, 1, 6);
    } else {
        aimTick_ = 0;
        aimTotalTicks_ = 0;
    }
}

void Opportunity::onClientTick(JNIEnv* env) {
    if (!env)
        return;

    // Anything else that owns the hotbar or the use-item channel outranks an
    // opportunity: AutoRod mid-sequence, a clutch save (when you are about to die,
    // webbing somebody is not the play), a potion on its way out. Same path for a screen
    // going up or the module being switched off mid-sequence, so nothing is ever left
    // half-owned.
    const bool placementBusy = (clutch && clutch->isRewritingRotation()) ||
                               (scaffold && scaffold->isRewritingRotation());
    const bool potionBusy = (throwPots && throwPots->isRewritingRotation()) ||
                            (autoPot && autoPot->isRewritingRotation());
    if (!enabled || !player.isValid() || Menu::isOpen || mc.isInGui() ||
        (autoRod && autoRod->isSequencing()) || placementBusy || potionBusy) {
        if (phase_ != Phase::Idle || originalSlot_ >= 0)
            abort(env);
        return;
    }

    const std::uint64_t now = nowMs();
    heldSlot_ = player.getCurrentSlot();
    for (int slot = 0; slot < 9; ++slot)
        hotbar_[slot] = player.getItemInSlot(slot);

    const float noiseAmp = std::clamp(settings.aimNoise, 0.0f, 3.0f);
    // Symmetric noise in [-amp, amp], drawn fresh every tick, so no two rotations we
    // ship are ever the same value twice in a row.
    const auto noise = [this, noiseAmp]() -> float {
        if (noiseAmp <= 0.0f)
            return 0.0f;
        return (static_cast<int>(rng_() % 2001u) - 1000) / 1000.0f * noiseAmp;
    };

    // Settle is the hand-back and runs to completion regardless of the world state.
    if (phase_ == Phase::Settle) {
        if (aimTick_ < aimTotalTicks_) {
            ++aimTick_;
            const float t =
                smoothstep(static_cast<float>(aimTick_) / static_cast<float>(aimTotalTicks_));
            applyRotation(aimFromYaw_ + wrapDegrees(aimToYaw_ - aimFromYaw_) * t + noise(),
                          aimFromPitch_ + (aimToPitch_ - aimFromPitch_) * t + noise());
        }
        if (aimTick_ >= aimTotalTicks_ && now >= restoreSlotAtMs_) {
            restoreSlot();
            haveReturnView_ = false;
            shot_ = {};
            phase_ = Phase::Idle;
            owningRotation_.store(false, std::memory_order_release);
            status_.store(nullptr, std::memory_order_release);
        } else {
            status_.store("Settle", std::memory_order_release);
        }
        return;
    }

    switch (phase_) {
        case Phase::Idle: {
            Shot found;
            if (!evaluate(found)) {
                status_.store(nullptr, std::memory_order_release);
                return;
            }
            // Noticed it. The head does not move yet -- the reaction delay is exactly
            // the window where a person has seen the opening and not acted on it, and
            // leaving the rotation alone through it means the aim modules keep the head
            // for those ticks instead of freezing on our behalf.
            shot_ = found;
            reactDeadlineMs_ = now + static_cast<std::uint64_t>(rollReactionMs());
            phase_ = Phase::React;
            status_.store(playName(shot_.play), std::memory_order_release);
            return;
        }

        case Phase::React: {
            if (now < reactDeadlineMs_) {
                status_.store(playName(shot_.play), std::memory_order_release);
                return;
            }
            // They have had a few hundred milliseconds to move. Re-point at where they
            // are now; if the opening closed in the meantime, the play is simply off --
            // nothing has been claimed yet, so there is nothing to unwind.
            if (!refreshShot(shot_)) {
                shot_ = {};
                phase_ = Phase::Idle;
                status_.store(nullptr, std::memory_order_release);
                return;
            }

            // Take the slot before the camera moves. A hotbar switch that overlaps the
            // start of the flick is what a hand does; one that lands on the same tick as
            // the click is what only a program does.
            if (originalSlot_ < 0)
                originalSlot_ = heldSlot_;
            if (heldSlot_ != shot_.slot)
                player.setCurrentSlot(shot_.slot);

            aimAt(shot_, true);
            phase_ = Phase::Aim;
            owningRotation_.store(true, std::memory_order_release);
            status_.store(playName(shot_.play), std::memory_order_release);
            return;
        }

        case Phase::Aim: {
            if (!stillValid(shot_) && !refreshShot(shot_)) {
                beginSettle();
                return;
            }

            ++aimTick_;
            const float t =
                smoothstep(static_cast<float>(aimTick_) / static_cast<float>(aimTotalTicks_));
            // Overshoot on the penultimate tick, correct on the last one. A flick that
            // lands dead on target the first time is not a flick a hand produced.
            float scale = 1.0f;
            if (settings.overshoot && aimTotalTicks_ >= 3 && aimTick_ == aimTotalTicks_ - 1)
                scale = 1.05f + (rng_() % 70u) / 1000.0f;

            applyRotation(
                aimFromYaw_ + wrapDegrees(aimToYaw_ - aimFromYaw_) * t * scale + noise(),
                aimFromPitch_ + (aimToPitch_ - aimFromPitch_) * t * scale + noise());

            if (aimTick_ >= aimTotalTicks_) {
                phase_ = Phase::Place;
                aimTick_ = 0;  // reused as the Place dwell counter
            }
            status_.store(playName(shot_.play), std::memory_order_release);
            return;
        }

        case Phase::Place: {
            if (!stillValid(shot_)) {
                if (!refreshShot(shot_)) {
                    beginSettle();
                    return;
                }
                aimAt(shot_, false);
                phase_ = Phase::Aim;
                status_.store(playName(shot_.play), std::memory_order_release);
                return;
            }

            ++aimTick_;  // dwell in Place, not a flick step
            if (aimTick_ > 20) {
                // Something keeps refusing the placement. Do not hold the camera hostage.
                beginSettle();
                return;
            }

            // Track the hit point from the live eye position: both of us are moving, so
            // the rotation that pointed at this face last tick does not point at it now.
            const double dx = shot_.hitX - player.getPosX();
            const double dy = shot_.hitY - (player.getPosY() + 1.62);
            const double dz = shot_.hitZ - player.getPosZ();
            const double horiz = std::sqrt(dx * dx + dz * dz);
            const float holdYaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
            const float holdPitch = std::clamp(
                static_cast<float>(-std::atan2(dy, horiz) * 180.0 / kPi), -90.0f, 90.0f);
            // A quarter of the flick's noise while holding: a hand settles once it has
            // arrived, and more wobble than that walks the ray off the face.
            applyRotation(holdYaw + noise() * 0.25f, holdPitch + noise() * 0.25f);

            // Never on the first Place tick. That tick exists so the C03 for it ships
            // this rotation before the C08 that depends on it -- our hook runs ahead of
            // sendMotionUpdates(), so a same-tick placement would reach the server
            // before the rotation it was aimed with.
            if (aimTick_ < 2 || mc.getRightClickDelay() > 0) {
                status_.store(playName(shot_.play), std::memory_order_release);
                return;
            }

            mc.updateMouseOver();
            if (!mc.isTargetingBlock()) {
                status_.store(playName(shot_.play), std::memory_order_release);
                return;
            }
            const Minecraft::Vec3 hit = mc.getTargetedBlockPos();
            if (static_cast<int>(std::floor(hit.x)) != shot_.supportX ||
                static_cast<int>(std::floor(hit.y)) != shot_.supportY ||
                static_cast<int>(std::floor(hit.z)) != shot_.supportZ) {
                // The ray is not on the face we asked for. Wait for it; never guess.
                status_.store(playName(shot_.play), std::memory_order_release);
                return;
            }

            // Vanilla's own use-item path: it swings, sends the C08 with the real hit
            // vector, and sets rightClickDelayTimer itself -- we never touch that timer.
            // For the buckets the server re-derives the cell from its own raytrace
            // against the rotation we just sent, which is the whole reason the rotation
            // has to be the real one.
            mc.rightClickMouse();

            const int idx = static_cast<int>(shot_.play);
            nextPlayMs_[idx] =
                now +
                static_cast<std::uint64_t>(std::clamp(settings.play[idx].cooldownMs, 0, 60000)) +
                (rng_() % 250u);
            const std::uint64_t globalFloor =
                static_cast<std::uint64_t>(std::clamp(settings.globalCooldownMs, 0, 10000));
            nextAnyPlayMs_ = now + std::max(kVanillaUseIntervalMs, globalFloor) + (rng_() % 150u);

            // One placement per opportunity. Standing there re-webbing the same feet is
            // both pointless and the most obvious thing this module could do.
            beginSettle();
            return;
        }

        case Phase::Settle:
        default:
            return;
    }
}

std::string Opportunity::getDisplayInfo() {
    if (const char* status = status_.load(std::memory_order_acquire))
        return status;
    return "Watching";
}

void Opportunity::onRenderMenu() {
    static const char* const kPlayLabels[] = {"Cobweb", "Lava", "Fire", "Water"};

    bool enabledValue = enabled;
    ui::two_column(
        "Opportunity",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ImGui::Spacing();

            ui::SliderFloat("Reach", &settings.reach, 3.0f, 4.5f, "%.1f blocks");
            ui::SliderFloat("Self Safe Distance", &settings.selfSafeDist, 0.0f, 6.0f,
                            "%.1f blocks");
            ui::SliderInt("Global Cooldown", &settings.globalCooldownMs, 0, 10000, "%d ms");
            ui::Checkbox("Require Line Of Sight", &settings.requireLineOfSight);
            ui::Checkbox("Ignore Teammates", &settings.ignoreTeam);
            ui::Checkbox("Ignore Invisibles", &settings.ignoreInvisible);
        },
        "Humanization",
        [this]() {
            ui::SliderInt("React Min", &settings.reactMinMs, 0, 1000, "%d ms");
            ui::SliderInt("React Max", &settings.reactMaxMs, 0, 1000, "%d ms");
            if (settings.reactMaxMs < settings.reactMinMs)
                settings.reactMaxMs = settings.reactMinMs;
            ui::SliderInt("Flick Ticks", &settings.aimTicks, 1, 6, "%d ticks");
            ui::SliderFloat("Aim Noise", &settings.aimNoise, 0.0f, 3.0f, "%.2f deg");
            ui::Checkbox("Overshoot", &settings.overshoot);
            ui::Checkbox("Snap Back", &settings.snapBack);
            ui::Checkbox("Switch Back", &settings.restoreSlot);
        });

    // Each play carries its own window, health gate, chance and cooldown; the selector
    // decides which one the rows below are editing.
    editPlay_ = std::clamp(editPlay_, 0, kPlayCount - 1);
    ui::two_column(
        "Play",
        [this]() {
            ui::Combo("Play", &editPlay_, kPlayLabels, kPlayCount);
            ImGui::Spacing();

            PlaySettings& ps = settings.play[editPlay_];
            ui::Checkbox("Use This Play", &ps.enabled);
            ui::SliderFloat("Min Distance", &ps.minDist, 0.0f, 6.0f, "%.1f blocks");
            ui::SliderFloat("Max Distance", &ps.maxDist, 0.0f, 6.0f, "%.1f blocks");
            if (ps.maxDist < ps.minDist)
                ps.maxDist = ps.minDist;
        },
        "Trigger",
        [this]() {
            PlaySettings& ps = settings.play[editPlay_];
            ui::SliderFloat("Max Health", &ps.maxTargetHealth, 0.5f, 20.0f, "%.1f hp");
            ui::SliderInt("Chance", &ps.chance, 0, 100, "%d%%");
            ui::SliderInt("Cooldown", &ps.cooldownMs, 0, 20000, "%d ms");
        });
}

// Config keys are flat and prefixed per play, so adding a fifth play later cannot
// renumber the four that already exist in somebody's config.
static const char* const kPlayKeys[] = {"cobweb", "lava", "fire", "water"};

void Opportunity::saveSettings(json::Value& node) const {
    for (int i = 0; i < kPlayCount; ++i) {
        const std::string key = kPlayKeys[i];
        const PlaySettings& ps = settings.play[i];
        node[key + "Enabled"] = ps.enabled;
        node[key + "MinDist"] = ps.minDist;
        node[key + "MaxDist"] = ps.maxDist;
        node[key + "MaxHealth"] = ps.maxTargetHealth;
        node[key + "Chance"] = ps.chance;
        node[key + "Cooldown"] = ps.cooldownMs;
    }

    node["reach"] = settings.reach;
    node["selfSafeDist"] = settings.selfSafeDist;
    node["requireLineOfSight"] = settings.requireLineOfSight;
    node["ignoreTeam"] = settings.ignoreTeam;
    node["ignoreInvisible"] = settings.ignoreInvisible;
    node["globalCooldownMs"] = settings.globalCooldownMs;
    node["reactMinMs"] = settings.reactMinMs;
    node["reactMaxMs"] = settings.reactMaxMs;
    node["aimTicks"] = settings.aimTicks;
    node["aimNoise"] = settings.aimNoise;
    node["overshoot"] = settings.overshoot;
    node["snapBack"] = settings.snapBack;
    node["restoreSlot"] = settings.restoreSlot;
}

void Opportunity::loadSettings(const json::Value& node) {
    for (int i = 0; i < kPlayCount; ++i) {
        const std::string key = kPlayKeys[i];
        PlaySettings& ps = settings.play[i];
        ps.enabled = node[key + "Enabled"].as_bool(ps.enabled);
        ps.minDist = (std::clamp)(node[key + "MinDist"].as_float(ps.minDist), 0.0f, 6.0f);
        ps.maxDist = (std::clamp)(node[key + "MaxDist"].as_float(ps.maxDist), ps.minDist, 6.0f);
        ps.maxTargetHealth =
            (std::clamp)(node[key + "MaxHealth"].as_float(ps.maxTargetHealth), 0.5f, 20.0f);
        ps.chance = (std::clamp)(node[key + "Chance"].as_int(ps.chance), 0, 100);
        ps.cooldownMs = (std::clamp)(node[key + "Cooldown"].as_int(ps.cooldownMs), 0, 20000);
    }

    settings.reach = (std::clamp)(node["reach"].as_float(settings.reach), 3.0f, 4.5f);
    settings.selfSafeDist =
        (std::clamp)(node["selfSafeDist"].as_float(settings.selfSafeDist), 0.0f, 6.0f);
    settings.requireLineOfSight = node["requireLineOfSight"].as_bool(settings.requireLineOfSight);
    settings.ignoreTeam = node["ignoreTeam"].as_bool(settings.ignoreTeam);
    settings.ignoreInvisible = node["ignoreInvisible"].as_bool(settings.ignoreInvisible);
    settings.globalCooldownMs =
        (std::clamp)(node["globalCooldownMs"].as_int(settings.globalCooldownMs), 0, 10000);
    settings.reactMinMs = (std::clamp)(node["reactMinMs"].as_int(settings.reactMinMs), 0, 1000);
    settings.reactMaxMs = (std::clamp)(node["reactMaxMs"].as_int(settings.reactMaxMs),
                                       settings.reactMinMs, 1000);
    settings.aimTicks = (std::clamp)(node["aimTicks"].as_int(settings.aimTicks), 1, 6);
    settings.aimNoise = (std::clamp)(node["aimNoise"].as_float(settings.aimNoise), 0.0f, 3.0f);
    settings.overshoot = node["overshoot"].as_bool(settings.overshoot);
    settings.snapBack = node["snapBack"].as_bool(settings.snapBack);
    settings.restoreSlot = node["restoreSlot"].as_bool(settings.restoreSlot);
}


