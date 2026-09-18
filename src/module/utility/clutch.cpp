#include "clutch.h"

#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../sdk/player/render_data.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/inject_trace.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/network/json.hpp"

#include <cstdio>

// Redefine COBBLE_TRACE to COBBLE_TRACE_ONCE to prevent log spam in this per-frame loop module
#ifdef COBBLE_TRACE
#undef COBBLE_TRACE
#define COBBLE_TRACE(step) COBBLE_TRACE_ONCE(step)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

Clutch* scaffold = nullptr;
Clutch* clutch = nullptr;

namespace {
constexpr double kPi = 3.14159265358979323846;

// Vanilla caps a held right-click at one use every 4 ticks (rightClickDelayTimer).
// We never zero that timer; this is only the floor we schedule against.
constexpr std::uint64_t kVanillaUseIntervalMs = 200;

// How close the aim has to be before we let the raytrace decide. Loose enough that
// the noise on the last tick of the flick cannot push us out of it, tight enough
// that we are unambiguously looking at the face we asked for.
constexpr float kAimToleranceDeg = 4.0f;

// A flick that has not converged in this many ticks is not going to; give up rather
// than hold the player's camera hostage.
constexpr int kMaxAimTicks = 12;

// Floor on the trajectory lookahead, in ticks, whatever Prediction Time says. Two ticks
// is 0.4 blocks of travel: a horizon that short can only ever notice the hole after the
// ground has already run out, and by then the ledge -- the one thing left to click -- is
// leaving reach. Four ticks sees the edge coming while it is still clickable.
constexpr int kMinLookaheadTicks = 4;

// Depth a drop has to have before we will place while still standing on solid ground.
// The airborne case uses the user's Fall Depth, because once you are falling any hole
// that setting calls deadly is worth a block. A pre-emptive placement is different: it
// happens on a floor the player is walking across, so it has to ignore the two-deep
// decorative dips a low Fall Depth would otherwise fire on, and answer only to a real
// cliff or the void.
constexpr int kPreemptiveDepth = 5;

struct Direction {
    int x;
    int y;
    int z;
};

constexpr std::array<Direction, 6> kDirections = {Direction{1, 0, 0},  Direction{-1, 0, 0}, Direction{0, 1, 0},
                                                  Direction{0, -1, 0}, Direction{0, 0, 1},  Direction{0, 0, -1}};

// One tick of vanilla 1.8 player physics: advance by velocity, then apply air
// drag (0.91 horizontal) and gravity (vertical -0.08, then 0.98 damping).
inline void stepPhysics(double& x, double& y, double& z, double& mx, double& my, double& mz) {
    x += mx;
    y += my;
    z += mz;
    mx *= 0.91;
    mz *= 0.91;
    my = (my - 0.08) * 0.98;
}

inline bool finite3(double a, double b, double c) {
    return !std::isnan(a) && !std::isnan(b) && !std::isnan(c) && !std::isinf(a) && !std::isinf(b) && !std::isinf(c);
}

}  // namespace

Clutch::Clutch(PlaceMode mode) : Module(mode == PlaceMode::Clutch ? "Clutch" : "Scaffold"), placeMode_(mode) {
    renders = true;
    worldVisual = true;  // the placement preview is a world-space block outline
    usesPlayerCache = true;
    // Both variants ship locked. Scaffold because bridging is a continuous placement stream
    // and this engine is built around the occasional emergency save. Clutch because the
    // engine it is built on cannot answer the question it needs to answer: a trajectory
    // stepped through open air is not where you actually end up once a wall, a corner or your
    // own collision box is involved, so the anchor it picks is wrong exactly where the save
    // matters most -- against a corner, up against geometry, anywhere the fall is not a clean
    // vertical drop. Placing earlier made it place the wrong block earlier. What it needs is a
    // real swept collision prediction and a placement search that reasons about where the body
    // can still be caught, not a lookahead point; until it has one it stays behind the
    // developer gate.
    locked = true;
    if (isScaffold()) {
        // A bridge is placed where you are already looking, so the flick is short and
        // there is nothing to flick back to.
        settings.aimTicks = 2;
        settings.snapBack = false;
        settings.reactMinMs = 60;
        settings.reactMaxMs = 140;
    }
}

Clutch::~Clutch() = default;

std::uint64_t Clutch::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

float Clutch::wrapDegrees(float angle) {
    while (angle > 180.0f)
        angle -= 360.0f;
    while (angle < -180.0f)
        angle += 360.0f;
    return angle;
}

// 3t^2 - 2t^3: zero velocity at both ends. A flick that starts and stops abruptly is
// the giveaway; this is the shape a hand actually draws.
float Clutch::smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

int Clutch::rollReactionMs() {
    const int lo = std::clamp(settings.reactMinMs, 0, 1000);
    const int hi = std::clamp(settings.reactMaxMs, lo, 1000);
    if (hi <= lo)
        return lo;
    // Two uniform draws averaged: a rough triangular distribution, which is much
    // closer to real reaction times than a flat one and never lands on the bounds.
    const int a = lo + static_cast<int>(rng_() % static_cast<unsigned>(hi - lo + 1));
    const int b = lo + static_cast<int>(rng_() % static_cast<unsigned>(hi - lo + 1));
    return (a + b) / 2;
}

bool Clutch::rollMiss() {
    const int chance = std::clamp(settings.missChance, 0, 90);
    return chance > 0 && static_cast<int>(rng_() % 100u) < chance;
}

void Clutch::onEnable() {
    phase_ = Phase::Idle;
    target_ = {};
    slot_ = -1;
    originalSlot_ = -1;
    reactDeadlineMs_ = 0;
    restoreSlotAtMs_ = 0;
    nextPlaceMs_ = 0;
    aimTick_ = 0;
    aimTotalTicks_ = 0;
    haveReturnView_ = false;
    placesThisRun_ = 0;
    owningRotation_.store(false, std::memory_order_release);
    status_.store(nullptr, std::memory_order_release);
    publishPreview({});
}

void Clutch::onDisable() {
    // No JNI from the module thread. needsClientTick() stays true while we still own
    // the rotation, so the next client tick runs abort() and hands everything back:
    // the view flick, the hotbar slot and the phase.
    status_.store(nullptr, std::memory_order_release);
    std::lock_guard<std::mutex> lock(pathMutex_);
    cachedPath_.clear();
    cachedPlacement_ = {};
}

void Clutch::onWorldChange() {
    // The anchor we were about to place against belongs to a world that no longer
    // exists; keeping it would raytrace against fresh terrain.
    target_ = {};
    phase_ = Phase::Idle;
    nextPlaceMs_ = 0;
    placesThisRun_ = 0;
    haveReturnView_ = false;
    owningRotation_.store(false, std::memory_order_release);
    status_.store(nullptr, std::memory_order_release);
    publishPreview({});
}

void Clutch::restoreSlot() {
    if (settings.restoreSlot && originalSlot_ >= 0 && originalSlot_ < 9 && player.isValid())
        player.setCurrentSlot(originalSlot_);
    originalSlot_ = -1;
}

// Unconditional hand-back. Called when the module is switched off mid-sequence, when
// the world goes away under us, or when the situation resolved itself.
void Clutch::abort(JNIEnv* env) {
    (void)env;
    restoreSlot();
    phase_ = Phase::Idle;
    target_ = {};
    slot_ = -1;
    aimTick_ = 0;
    aimTotalTicks_ = 0;
    haveReturnView_ = false;
    placesThisRun_ = 0;
    owningRotation_.store(false, std::memory_order_release);
    status_.store(nullptr, std::memory_order_release);
    publishPreview({});
}

void Clutch::publishPreview(const Placement& p) {
    std::lock_guard<std::mutex> lock(pathMutex_);
    cachedPlacement_ = p;
}

// Blocks we refuse to build with: non-solid, gravity-affected, interactive (a chest or
// door would open instead of placing), or not a full cube to stand on.
bool Clutch::isPlaceableBlock(int id) {
    if (id <= 0 || id > 255)
        return false;
    static constexpr int kRejected[] = {6,   8,   9,   10,  11,  26,  27,  28,  30,  31,  32,  37,  38,  39,  40,
                                        50,  51,  55,  59,  63,  64,  65,  66,  68,  69,  70,  71,  72,  75,  76,
                                        77,  78,  83,  90,  93,  94,  96,  104, 105, 106, 111, 115, 119, 127, 131,
                                        132, 140, 141, 142, 143, 147, 148, 149, 150, 157, 171, 175};
    for (int rejected : kRejected) {
        if (rejected == id)
            return false;
        if (rejected > id)
            break;  // sorted
    }
    return true;
}

int Clutch::findBlockSlot() const {
    // Prefer the slot already in hand: a player who is about to clutch is usually
    // holding their blocks, and taking it saves a hotbar switch nobody needed.
    const int held = player.getCurrentSlot();
    if (held >= 0 && held < 9 && isPlaceableBlock(player.getItemInSlot(held)))
        return held;
    for (int slot = 0; slot < 9; ++slot) {
        if (isPlaceableBlock(player.getItemInSlot(slot)))
            return slot;
    }
    return -1;
}

// Is the predicted landing spot a death? "Death" means the column under it has no
// floor within safeDepth blocks -- which covers both the void and a fall long enough
// to hurt. The old engine scanned all the way to y=0 and so answered "safe" for any
// column with a single block anywhere below it, which in a normal world is every
// column: the module could only ever fire over a literal bottomless pit.
bool Clutch::isDangerous(double predictedX, double predictedY, double predictedZ) const {
    if (!finite3(predictedX, predictedY, predictedZ))
        return false;

    int depth = std::clamp(settings.safeDepth, 1, 24);
    if (!settings.allowForward && player.isOnGround()) {
        // Standing on something. This used to be an unconditional refusal, which meant the
        // earliest the module could possibly decide anything was the first airborne tick --
        // it threw away the ticks where the ledge was closest and the save cheapest, and it
        // is most of why the block landed after the fall instead of before it. Acting here
        // is allowed, but only for a drop deep enough that no floor plausibly follows;
        // see kPreemptiveDepth. Nothing else changes: the samples below still require the
        // WHOLE predicted footprint to be floorless, so walking a solid surface, or standing
        // at the lip of one, never qualifies.
        depth = (std::max)(depth, kPreemptiveDepth);
    }

    const int footY = static_cast<int>(std::floor(player.getPosY())) - 1;
    if (footY < 0)
        return true;  // already below the world; nothing to check, everything to fix

    const int lowest = std::max(0, footY - depth);

    // Sample the four corners of the hitbox plus the centre: falling with one foot
    // over the edge still lands you on the ledge, and that is not a clutch.
    static constexpr double kOffsets[] = {-0.3, 0.0, 0.3};
    for (double offsetX : kOffsets) {
        for (double offsetZ : kOffsets) {
            const int x = static_cast<int>(std::floor(predictedX + offsetX));
            const int z = static_cast<int>(std::floor(predictedZ + offsetZ));
            for (int y = footY; y >= lowest; --y) {
                if (mc.getBlockId(x, y, z) != 0)
                    return false;  // there is a floor here; we live
            }
        }
    }
    return true;
}

bool Clutch::isPlacementUsable(const Placement& p) const {
    if (!p.valid)
        return false;
    if (mc.getBlockId(p.targetX, p.targetY, p.targetZ) != 0)
        return false;  // somebody already filled it
    if (mc.getBlockId(p.supportX, p.supportY, p.supportZ) == 0)
        return false;  // the face we were going to click is gone

    // A solid block cannot be placed inside a living entity, so the server rejects
    // any target overlapping our own hitbox. Checking it here saves a wasted C08.
    const double px = player.getPosX();
    const double py = player.getPosY();
    const double pz = player.getPosZ();
    if (p.targetX + 1 > px - 0.3 && p.targetX < px + 0.3 && p.targetZ + 1 > pz - 0.3 && p.targetZ < pz + 0.3 &&
        p.targetY + 1 > py && p.targetY < py + 1.8) {
        return false;
    }

    const double dx = p.hitX - px;
    const double dy = p.hitY - (py + 1.62);
    const double dz = p.hitZ - pz;
    const double reach = static_cast<double>(std::clamp(settings.reach, 3.0f, 4.5f));
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

Clutch::Placement Clutch::findPlacement(double predictedX, double predictedY, double predictedZ) const {
    Placement best;
    best.score = std::numeric_limits<double>::max();

    const double eyeX = player.getPosX();
    const double eyeY = player.getPosY() + 1.62;
    const double eyeZ = player.getPosZ();
    const float curYaw = player.getYaw();
    const float curPitch = player.getPitch();

    const int centerX = static_cast<int>(std::floor(predictedX));
    const int centerY = static_cast<int>(std::floor(predictedY)) - 1;
    const int centerZ = static_cast<int>(std::floor(predictedZ));
    const int radius = std::clamp(settings.searchRadius, 1, 6);
    const double maxReach = static_cast<double>(std::clamp(settings.reach, 3.0f, 4.5f));
    const double maxReachSq = maxReach * maxReach;

    for (int ring = 0; ring <= radius; ++ring) {
        for (int x = centerX - ring; x <= centerX + ring; ++x) {
            for (int z = centerZ - ring; z <= centerZ + ring; ++z) {
                if (std::max(std::abs(x - centerX), std::abs(z - centerZ)) != ring)
                    continue;
                for (int y = centerY - 1; y <= centerY + 1; ++y) {
                    if (y < 0 || mc.getBlockId(x, y, z) != 0)
                        continue;
                    for (const Direction& dir : kDirections) {
                        const int supportX = x - dir.x;
                        const int supportY = y - dir.y;
                        const int supportZ = z - dir.z;
                        if (supportY < 0 || mc.getBlockId(supportX, supportY, supportZ) == 0)
                            continue;

                        const double hitX = supportX + 0.5 + dir.x * 0.49;
                        const double hitY = supportY + 0.5 + dir.y * 0.49;
                        const double hitZ = supportZ + 0.5 + dir.z * 0.49;
                        const double dx = hitX - eyeX;
                        const double dy = hitY - eyeY;
                        const double dz = hitZ - eyeZ;
                        const double reachSq = dx * dx + dy * dy + dz * dz;
                        if (reachSq > maxReachSq)
                            continue;

                        const double landingDx = (x + 0.5) - predictedX;
                        const double landingDz = (z + 0.5) - predictedZ;

                        // Prefer the anchor that needs the least head movement. This is
                        // not just cosmetic: a short flick is a believable flick, and it
                        // converges inside the aim budget instead of running out of it.
                        double cameraPenalty = 0.0;
                        if (settings.cameraWeight) {
                            const double horiz = std::sqrt(dx * dx + dz * dz);
                            const float wantYaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
                            const float wantPitch =
                                std::clamp(static_cast<float>(-std::atan2(dy, horiz) * 180.0 / kPi), -90.0f, 90.0f);
                            const float yawDiff = std::abs(wrapDegrees(wantYaw - curYaw));
                            const float pitchDiff = std::abs(wrapDegrees(wantPitch - curPitch));
                            cameraPenalty = (yawDiff * yawDiff + pitchDiff * pitchDiff) * 0.0006;
                        }

                        const double score = landingDx * landingDx + landingDz * landingDz + reachSq * 0.04 +
                                             std::abs(y - centerY) * 0.8 + cameraPenalty;
                        if (score < best.score) {
                            best = {x, y, z, supportX, supportY, supportZ, hitX, hitY, hitZ, score, true};
                        }
                    }
                }
            }
        }
        if (best.valid)
            break;  // nearest ring wins; do not pay for the outer ones
    }
    return best.valid ? best : Placement{};
}

// Where this variant wants a block, and whether it wants one at all.
bool Clutch::wantsPlacement(Placement& out) const {
    out = {};

    const double x = player.getPosX();
    const double y = player.getPosY();
    const double z = player.getPosZ();
    if (!finite3(x, y, z))
        return false;

    if (isScaffold()) {
        // Bridge under the spot just ahead of where we are looking, so the block always
        // lands along our real facing and the flick stays short.
        const double ahead = std::clamp(settings.scaffoldLead, 0.4f, 1.6f);
        const double yawRad = static_cast<double>(player.getYaw()) * kPi / 180.0;
        const double targetX = x - std::sin(yawRad) * ahead;
        const double targetZ = z + std::cos(yawRad) * ahead;
        const int footY = static_cast<int>(std::floor(y)) - 1;
        if (footY < 0)
            return false;
        if (mc.getBlockId(static_cast<int>(std::floor(targetX)), footY, static_cast<int>(std::floor(targetZ))) != 0) {
            return false;  // already solid ahead; nothing to bridge
        }
        out = findPlacement(targetX, y, targetZ);
        return out.valid;
    }

    double px = x, py = y, pz = z;
    double mx = player.getMotionX(), my = player.getMotionY(), mz = player.getMotionZ();
    if (!finite3(mx, my, mz))
        return false;

    const int ticks = std::clamp((settings.predictionMs + 49) / 50, kMinLookaheadTicks, 16);
    for (int tick = 0; tick < ticks; ++tick)
        stepPhysics(px, py, pz, mx, my, mz);

    // Deadly where we are going, or deadly where we already are. Only testing the horizon
    // loses the narrow-hole case the instant that horizon clears the far lip -- you fall in
    // and the module decides you are fine. Only testing here can never fire before the fall
    // has begun. Either one alone is late; the pair is what a player watching the ground has.
    if (!isDangerous(px, py, pz) && !isDangerous(x, y, z))
        return false;

    out = findPlacement(px, py, pz);
    if (!out.valid)
        out = findPlacement(x, y, z);  // nothing near the landing spot; save from here
    return out.valid;
}

// Write the rotation the server will see this tick, quantized onto the client's own
// mouse GCD grid relative to where the view is now. Every delta we produce is then one
// a physical mouse could have produced at this sensitivity, which is the property the
// rotation checks actually test.
void Clutch::applyRotation(float yaw, float pitch) {
    const float curYaw = player.getYaw();
    const float curPitch = player.getPitch();
    const float sensitivity = mc.getMouseSensitivity();
    const float gf = sensitivity * 0.6f + 0.2f;
    const float gcd = gf * gf * gf * 1.2f;

    float outYaw = yaw;
    float outPitch = std::clamp(pitch, -90.0f, 90.0f);
    if (gcd > 0.0001f) {
        outYaw = curYaw + std::round(wrapDegrees(yaw - curYaw) / gcd) * gcd;
        outPitch = std::clamp(curPitch + std::round((outPitch - curPitch) / gcd) * gcd, -90.0f, 90.0f);
    }
    player.setYaw(outYaw);
    player.setPitch(outPitch);
}

// Set up a flick from where the view is now to the placement's hit point.
void Clutch::aimAt(const Placement& p, bool freshOpportunity) {
    const double dx = p.hitX - player.getPosX();
    const double dy = p.hitY - (player.getPosY() + 1.62);
    const double dz = p.hitZ - player.getPosZ();
    const double horiz = std::sqrt(dx * dx + dz * dz);

    aimToYaw_ = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
    aimToPitch_ = std::clamp(static_cast<float>(-std::atan2(dy, horiz) * 180.0 / kPi), -90.0f, 90.0f);
    aimFromYaw_ = player.getYaw();
    aimFromPitch_ = player.getPitch();

    if (freshOpportunity && settings.snapBack && !haveReturnView_) {
        returnYaw_ = aimFromYaw_;
        returnPitch_ = aimFromPitch_;
        haveReturnView_ = true;
    }

    // Scale the flick length with the distance it has to cover. A fixed budget makes a
    // 4-degree correction crawl and a 90-degree whip teleport; both read as synthetic.
    const float travel =
        std::max(std::abs(wrapDegrees(aimToYaw_ - aimFromYaw_)), std::abs(aimToPitch_ - aimFromPitch_));
    int budget = std::clamp(settings.aimTicks, 1, 6);
    if (travel < 8.0f)
        budget = 1;
    else if (travel > 60.0f)
        budget += 1;
    aimTotalTicks_ = std::clamp(budget, 1, kMaxAimTicks);
    aimTick_ = 0;
}

// Placement is done: flick the view back where it came from and let the hotbar settle
// after a beat, because nobody switches back on the same tick they placed.
void Clutch::beginSettle() {
    phase_ = Phase::Settle;
    restoreSlotAtMs_ = nowMs() + 90 + static_cast<std::uint64_t>(rng_() % 150u);

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

void Clutch::onClientTick(JNIEnv* env) {
    if (!env)
        return;

    // Switched off mid-sequence or a screen went up: hand the rotation, the slot
    // and the phase back through one path so nothing can be left half-owned.
    if (!enabled || !player.isValid() || Menu::isOpen || mc.isInGui()) {
        if (phase_ != Phase::Idle || originalSlot_ >= 0)
            abort(env);
        return;
    }

    const std::uint64_t now = nowMs();
    const float noiseAmp = std::clamp(settings.aimNoise, 0.0f, 3.0f);
    // Symmetric noise in [-amp, amp]; drawn fresh every tick so no two rotations we
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
            const float t = smoothstep(static_cast<float>(aimTick_) / static_cast<float>(aimTotalTicks_));
            applyRotation(aimFromYaw_ + wrapDegrees(aimToYaw_ - aimFromYaw_) * t + noise(),
                          aimFromPitch_ + (aimToPitch_ - aimFromPitch_) * t + noise());
        }
        if (aimTick_ >= aimTotalTicks_ && now >= restoreSlotAtMs_) {
            restoreSlot();
            haveReturnView_ = false;
            placesThisRun_ = 0;
            target_ = {};
            slot_ = -1;
            phase_ = Phase::Idle;
            owningRotation_.store(false, std::memory_order_release);
            status_.store(nullptr, std::memory_order_release);
            publishPreview({});
        } else {
            status_.store("Settle", std::memory_order_release);
        }
        return;
    }

    Placement wanted;
    const bool want = wantsPlacement(wanted);

    if (!want) {
        // Either nothing to do, or the danger passed / the hole filled itself while we
        // were mid-sequence -- in which case settle rather than dropping the camera.
        if (phase_ == Phase::Idle) {
            status_.store(nullptr, std::memory_order_release);
            publishPreview({});
        } else {
            beginSettle();
        }
        return;
    }

    const int slot = findBlockSlot();
    if (slot < 0) {
        status_.store("No Blocks", std::memory_order_release);
        if (phase_ != Phase::Idle)
            beginSettle();
        else
            publishPreview({});
        return;
    }

    publishPreview(wanted);

    // A single tick can carry the sequence through more than one phase. Idle and React do
    // bookkeeping -- pick the anchor, take the slot -- and the old code spent a whole tick on
    // each of them before the first degree of the flick was written, which is 100 ms of the
    // fall given away for nothing when the reaction budget is already spent. A phase that
    // still has to wait for the clock returns and ends the tick; one that is merely done
    // breaks and hands the same tick to the next phase. The bound is a guard against a phase
    // cycle, not a schedule: Idle -> React -> Aim is the longest legitimate chain.
    for (int hop = 0; hop < 3; ++hop) {
        switch (phase_) {
            case Phase::Idle: {
                if (now < nextPlaceMs_) {
                    status_.store("Idle", std::memory_order_release);
                    return;
                }
                if (rollMiss()) {
                    // Did not notice it this time. Back off before re-rolling, otherwise the
                    // dice are thrown 20 times a second and the chance means nothing.
                    nextPlaceMs_ = now + 200u + (rng_() % 200u);
                    status_.store("Idle", std::memory_order_release);
                    return;
                }
                target_ = wanted;
                slot_ = slot;
                reactDeadlineMs_ = now + static_cast<std::uint64_t>(rollReactionMs());
                phase_ = Phase::React;
                owningRotation_.store(true, std::memory_order_release);
                status_.store("React", std::memory_order_release);
                break;  // React's own deadline decides whether this tick continues
            }

            case Phase::React: {
                if (now < reactDeadlineMs_) {
                    status_.store("React", std::memory_order_release);
                    return;
                }
                // Take the slot before the camera moves. The hotbar switch overlapping the
                // start of the flick is what a hand does; doing it at the instant of the
                // click is what only a program does.
                if (originalSlot_ < 0)
                    originalSlot_ = player.getCurrentSlot();
                slot_ = slot;
                if (player.getCurrentSlot() != slot_)
                    player.setCurrentSlot(slot_);

                if (!isPlacementUsable(target_))
                    target_ = wanted;
                aimAt(target_, true);
                phase_ = Phase::Aim;
                status_.store("Aim", std::memory_order_release);
                break;  // the first step of the flick belongs to this tick, not the next one
            }

            case Phase::Aim: {
                if (!isPlacementUsable(target_)) {
                    if (!isPlacementUsable(wanted)) {
                        beginSettle();
                        return;
                    }
                    target_ = wanted;
                    aimAt(target_, false);
                }

                ++aimTick_;
                const float t = smoothstep(static_cast<float>(aimTick_) / static_cast<float>(aimTotalTicks_));
                // Overshoot on the penultimate tick and correct on the last one. A flick that
                // lands dead on the target the first time is not a flick a hand produced.
                float scale = 1.0f;
                if (settings.overshoot && aimTotalTicks_ >= 3 && aimTick_ == aimTotalTicks_ - 1)
                    scale = 1.05f + (rng_() % 70u) / 1000.0f;

                applyRotation(aimFromYaw_ + wrapDegrees(aimToYaw_ - aimFromYaw_) * t * scale + noise(),
                              aimFromPitch_ + (aimToPitch_ - aimFromPitch_) * t * scale + noise());

                if (aimTick_ >= aimTotalTicks_) {
                    phase_ = Phase::Place;
                    aimTick_ = 0;  // reused as the Place attempt counter
                    status_.store("Place", std::memory_order_release);
                } else {
                    status_.store("Aim", std::memory_order_release);
                }
                return;
            }

            case Phase::Place: {
                if (!isPlacementUsable(target_)) {
                    if (!isPlacementUsable(wanted)) {
                        beginSettle();
                        return;
                    }
                    target_ = wanted;
                    aimAt(target_, false);
                    phase_ = Phase::Aim;
                    status_.store("Aim", std::memory_order_release);
                    return;
                }

                ++aimTick_;  // an attempt in Place, not a flick step
                if (aimTick_ > 24) {
                    // Something is refusing to let us place (an entity in the way, a server
                    // that keeps rejecting it). Do not hold the camera hostage over it.
                    beginSettle();
                    return;
                }

                // Try to fire on the rotation the server ALREADY holds, before touching the
                // view. This hook runs ahead of sendMotionUpdates, so a C08 sent from here
                // leaves before this tick's C03: the only rotation the server can validate
                // the placement against is the one the PREVIOUS tick shipped. Testing the
                // raytrace first is what keeps that ordering -- and it keeps it a full tick
                // cheaper than the dwell this replaced, which waited for a rotation that the
                // last tick of the flick had already put on the wire.
                if (now >= nextPlaceMs_ && mc.getRightClickDelay() == 0) {
                    mc.updateMouseOver();
                    if (mc.isTargetingBlock()) {
                        const Minecraft::Vec3 hit = mc.getTargetedBlockPos();
                        if (static_cast<int>(std::floor(hit.x)) == target_.supportX &&
                            static_cast<int>(std::floor(hit.y)) == target_.supportY &&
                            static_cast<int>(std::floor(hit.z)) == target_.supportZ) {
                            // Vanilla's own use-item path: it swings, sends C08 with the real
                            // hit vector and sets rightClickDelayTimer itself. We never touch
                            // that timer.
                            mc.rightClickMouse();
                            ++placesThisRun_;
                            nextPlaceMs_ = now + kVanillaUseIntervalMs + (rng_() % 90u);

                            // Look for the next block in the same run: React with an
                            // already-elapsed deadline re-aims without paying the reaction
                            // delay again.
                            target_ = {};
                            reactDeadlineMs_ = 0;
                            phase_ = Phase::React;
                            status_.store("React", std::memory_order_release);
                            return;
                        }
                    }
                }

                // The ray is not on the face we asked for, or the use timer is still running.
                // Track the hit point from the LIVE eye position: we are falling, so the
                // rotation that pointed at this face last tick does not point at it now.
                // Tracking a fixed world point while moving is also exactly what a hand does.
                // This ships in THIS tick's C03, and the next tick is the one allowed to fire
                // on it.
                const double dx = target_.hitX - player.getPosX();
                const double dy = target_.hitY - (player.getPosY() + 1.62);
                const double dz = target_.hitZ - player.getPosZ();
                const double horiz = std::sqrt(dx * dx + dz * dz);
                const float holdYaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
                const float holdPitch =
                    std::clamp(static_cast<float>(-std::atan2(dy, horiz) * 180.0 / kPi), -90.0f, 90.0f);
                // Only a quarter of the flick's noise while holding: a hand settles when it
                // has arrived, and too much wobble here walks the ray off the face.
                applyRotation(holdYaw + noise() * 0.25f, holdPitch + noise() * 0.25f);
                status_.store("Place", std::memory_order_release);
                return;
            }

            case Phase::Settle:
            default:
                return;
        }
    }
}

// The module loop runs at roughly 500 Hz. Nothing here is a decision any more -- only
// the caches the render thread reads. Every timing and placement choice moved to
// onClientTick, which is a true 20 Hz tick. The previous engine searched for anchors
// and measured delays from this loop, which meant thousands of JNI block lookups a
// second and per-call timing that only ever saw ~2 ms of elapsed time.
void Clutch::onUpdate() {
    lastUpdateTick_ = GetTickCount64();
    if (!enabled)
        return;

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedPath_.clear();
        cachedPlacement_ = {};
        return;
    }

    const auto snap = VisualCache::GetSnapshot();
    if (snap.valid) {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedLocalX_ = snap.localX;
        cachedLocalY_ = snap.localY;
        cachedLocalZ_ = snap.localZ;
        cachedLocalLastX_ = snap.localLastX;
        cachedLocalLastY_ = snap.localLastY;
        cachedLocalLastZ_ = snap.localLastZ;
        cachedRenderX_ = snap.renderPos.x;
        cachedRenderY_ = snap.renderPos.y;
        cachedRenderZ_ = snap.renderPos.z;
        cachedPartialTicks_ = snap.partialTicks;
        cachedPlayerValid_ = true;
        cachedMotionX_ = player.getMotionX();
        cachedMotionY_ = player.getMotionY();
        cachedMotionZ_ = player.getMotionZ();
    }

    if (!settings.showPath) {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedPath_.clear();
        return;
    }

    // Twice a tick is far more than the eye needs and ~20x less JNI than every pass.
    const std::uint64_t now = nowMs();
    if (now < nextPathMs_)
        return;
    nextPathMs_ = now + 25u;

    const double baseX = player.getPosX();
    const double baseY = player.getPosY();
    const double baseZ = player.getPosZ();
    double mx = player.getMotionX();
    double my = player.getMotionY();
    double mz = player.getMotionZ();
    if (!finite3(baseX, baseY, baseZ) || !finite3(mx, my, mz))
        return;

    std::vector<PathPoint> path;
    path.reserve(41 * 4);
    path.push_back(PathPoint{});

    double relX = 0.0, relY = 0.0, relZ = 0.0;
    for (int step = 0; step < 40; ++step) {
        constexpr int kSubSteps = 4;
        for (int sub = 0; sub < kSubSteps; ++sub) {
            relX += mx / kSubSteps;
            relY += my / kSubSteps;
            relZ += mz / kSubSteps;
            path.push_back(PathPoint{relX, relY, relZ, false});
        }
        mx *= 0.91;
        mz *= 0.91;
        my = (my - 0.08) * 0.98;

        const int checkY = static_cast<int>(std::floor(baseY + relY));
        if (checkY < 0)
            break;
        if (mc.getBlockId(static_cast<int>(std::floor(baseX + relX)), checkY,
                          static_cast<int>(std::floor(baseZ + relZ))) != 0) {
            path.back().hitBlock = true;
            break;
        }
    }

    std::lock_guard<std::mutex> lock(pathMutex_);
    cachedPath_ = std::move(path);
}

void Clutch::onRender() {
    if (!enabled || !RenderData::updated || Menu::isOpen)
        return;
    if (GetTickCount64() - lastUpdateTick_ > 300)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    std::vector<PathPoint> pathPoints;
    double interpX, interpY, interpZ;
    double motionX, motionY, motionZ;
    double renderX, renderY, renderZ;
    bool playerValid;
    Placement placement;
    {
        std::lock_guard<std::mutex> lock(pathMutex_);
        pathPoints = cachedPath_;
        interpX = cachedLocalLastX_ + (cachedLocalX_ - cachedLocalLastX_) * cachedPartialTicks_;
        interpY = cachedLocalLastY_ + (cachedLocalY_ - cachedLocalLastY_) * cachedPartialTicks_;
        interpZ = cachedLocalLastZ_ + (cachedLocalZ_ - cachedLocalLastZ_) * cachedPartialTicks_;
        renderX = cachedRenderX_;
        renderY = cachedRenderY_;
        renderZ = cachedRenderZ_;
        motionX = cachedMotionX_;
        motionY = cachedMotionY_;
        motionZ = cachedMotionZ_;
        playerValid = cachedPlayerValid_;
        placement = cachedPlacement_;
    }

    if (settings.showBps && playerValid) {
        const double horizontalSpeed = std::sqrt(motionX * motionX + motionZ * motionZ) * 20.0;
        const double verticalSpeed = motionY * 20.0;

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Speed: %.2f BPS  (Y: %.2f BPS)", horizontalSpeed, verticalSpeed);

        const ImVec2 textSize = ImGui::CalcTextSize(buffer);
        const float x = (static_cast<float>(RenderData::screenWidth) - textSize.x) / 2.0f;
        const float y = static_cast<float>(RenderData::screenHeight) / 2.0f + 60.0f;

        draw->AddRectFilled({x - 12.0f, y - 6.0f}, {x + textSize.x + 12.0f, y + textSize.y + 6.0f},
                            IM_COL32(20, 20, 20, 160), 6.0f);
        draw->AddRect({x - 12.0f, y - 6.0f}, {x + textSize.x + 12.0f, y + textSize.y + 6.0f},
                      IM_COL32(255, 255, 255, 30), 6.0f, ImDrawFlags_None, 1.0f);

        ImU32 textColor = IM_COL32(255, 255, 255, 240);
        if (verticalSpeed < -5.0)
            textColor = IM_COL32(240, 70, 70, 240);
        else if (horizontalSpeed > 8.0)
            textColor = IM_COL32(70, 240, 200, 240);
        draw->AddText({x, y}, textColor, buffer);
    }

    if (settings.showPath && pathPoints.size() >= 2) {
        std::vector<ImVec2> screenPoints;
        screenPoints.reserve(pathPoints.size());
        for (const auto& point : pathPoints) {
            const Vector3 cameraRelPos{static_cast<float>(interpX + point.relX - renderX),
                                       static_cast<float>(interpY + point.relY - renderY),
                                       static_cast<float>(interpZ + point.relZ - renderZ)};
            Vector2 screenPos;
            if (CWorldToScreen::WorldToScreen(cameraRelPos, RenderData::modelView, RenderData::projection,
                                              RenderData::screenWidth, RenderData::screenHeight, screenPos)) {
                screenPoints.push_back({screenPos.x, screenPos.y});
            }
        }
        if (screenPoints.size() >= 2) {
            draw->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), IM_COL32(0, 0, 0, 180),
                              ImDrawFlags_None, 3.5f);
            draw->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), IM_COL32(0, 191, 255, 220),
                              ImDrawFlags_None, 1.5f);
        }
    }

    if (!settings.showPlacement || !placement.valid)
        return;

    const float minX = static_cast<float>(placement.targetX - renderX);
    const float minY = static_cast<float>(placement.targetY - renderY);
    const float minZ = static_cast<float>(placement.targetZ - renderZ);
    const Vector3 corners[8] = {{minX, minY, minZ},
                                {minX + 1.0f, minY, minZ},
                                {minX + 1.0f, minY, minZ + 1.0f},
                                {minX, minY, minZ + 1.0f},
                                {minX, minY + 1.0f, minZ},
                                {minX + 1.0f, minY + 1.0f, minZ},
                                {minX + 1.0f, minY + 1.0f, minZ + 1.0f},
                                {minX, minY + 1.0f, minZ + 1.0f}};

    Vector2 screenCorners[8];
    bool visible[8];
    int visibleCount = 0;
    for (int i = 0; i < 8; ++i) {
        visible[i] = CWorldToScreen::WorldToScreen(corners[i], RenderData::modelView, RenderData::projection,
                                                   RenderData::screenWidth, RenderData::screenHeight, screenCorners[i]);
        if (visible[i])
            ++visibleCount;
    }
    if (visibleCount < 3)
        return;

    const ImU32 faceColor = IM_COL32(0, 191, 255, 35);
    const ImU32 outlineColor = IM_COL32(0, 191, 255, 220);

    static constexpr int kFaces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {2, 3, 7, 6},
                                         {0, 1, 5, 4}, {0, 3, 7, 4}, {1, 2, 6, 5}};
    for (const auto& face : kFaces) {
        if (!visible[face[0]] || !visible[face[1]] || !visible[face[2]] || !visible[face[3]])
            continue;
        const ImVec2 pts[4] = {{screenCorners[face[0]].x, screenCorners[face[0]].y},
                               {screenCorners[face[1]].x, screenCorners[face[1]].y},
                               {screenCorners[face[2]].x, screenCorners[face[2]].y},
                               {screenCorners[face[3]].x, screenCorners[face[3]].y}};
        draw->AddConvexPolyFilled(pts, 4, faceColor);
    }

    static constexpr int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : kEdges) {
        if (visible[edge[0]] && visible[edge[1]]) {
            draw->AddLine({screenCorners[edge[0]].x, screenCorners[edge[0]].y},
                          {screenCorners[edge[1]].x, screenCorners[edge[1]].y}, outlineColor, 1.5f);
        }
    }
}

std::string Clutch::getDisplayInfo() {
    if (const char* status = status_.load(std::memory_order_acquire))
        return status;
    return "Armed";
}

void Clutch::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        isClutch() ? "Clutch" : "Scaffold",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ImGui::Spacing();

            if (isScaffold()) {
                ui::SliderFloat("Bridge Reach", &settings.scaffoldLead, 0.4f, 1.6f, "%.1f blocks");
            } else {
                ui::SliderInt("Prediction Time", &settings.predictionMs, 100, 800, "%d ms");
                ui::SliderInt("Fall Depth", &settings.safeDepth, 1, 24, "%d blocks");
                ui::Checkbox("Allow Forward", &settings.allowForward);
            }
            ui::SliderInt("Search Radius", &settings.searchRadius, 1, 6, "%d blocks");
            ui::SliderFloat("Reach", &settings.reach, 3.0f, 4.5f, "%.1f blocks");
            ui::Checkbox("Camera-Weighted Search", &settings.cameraWeight);
            ui::Checkbox("Switch Back", &settings.restoreSlot);
        },
        "Humanization & HUD",
        [this]() {
            ui::SliderInt("React Min", &settings.reactMinMs, 0, 500, "%d ms");
            ui::SliderInt("React Max", &settings.reactMaxMs, 0, 500, "%d ms");
            if (settings.reactMaxMs < settings.reactMinMs)
                settings.reactMaxMs = settings.reactMinMs;
            ui::SliderInt("Flick Ticks", &settings.aimTicks, 1, 6, "%d ticks");
            ui::SliderFloat("Aim Noise", &settings.aimNoise, 0.0f, 3.0f, "%.2f deg");
            ui::Checkbox("Overshoot", &settings.overshoot);
            ui::Checkbox("Snap Back", &settings.snapBack);
            ui::SliderInt("Miss Chance", &settings.missChance, 0, 90, "%d%%");
            ImGui::Spacing();
            ui::Checkbox("Show Speed HUD", &settings.showBps);
            ui::Checkbox("Show Visual Path", &settings.showPath);
            ui::Checkbox("Show Block Preview", &settings.showPlacement);
        });
}

void Clutch::saveSettings(json::Value& node) const {
    node["scaffoldLead"] = settings.scaffoldLead;
    node["predictionMs"] = settings.predictionMs;
    node["searchRadius"] = settings.searchRadius;
    node["safeDepth"] = settings.safeDepth;
    node["allowForward"] = settings.allowForward;
    node["reach"] = settings.reach;
    node["reactMinMs"] = settings.reactMinMs;
    node["reactMaxMs"] = settings.reactMaxMs;
    node["aimTicks"] = settings.aimTicks;
    node["aimNoise"] = settings.aimNoise;
    node["overshoot"] = settings.overshoot;
    node["snapBack"] = settings.snapBack;
    node["restoreSlot"] = settings.restoreSlot;
    node["missChance"] = settings.missChance;
    node["cameraWeight"] = settings.cameraWeight;
    node["showBps"] = settings.showBps;
    node["showPath"] = settings.showPath;
    node["showPlacement"] = settings.showPlacement;
}

void Clutch::loadSettings(const json::Value& node) {
    settings.scaffoldLead = (std::clamp)(node["scaffoldLead"].as_float(settings.scaffoldLead), 0.4f, 1.6f);
    settings.predictionMs = (std::clamp)(node["predictionMs"].as_int(settings.predictionMs), 100, 800);
    settings.searchRadius = (std::clamp)(node["searchRadius"].as_int(settings.searchRadius), 1, 6);
    settings.safeDepth = (std::clamp)(node["safeDepth"].as_int(settings.safeDepth), 1, 24);
    settings.allowForward = node["allowForward"].as_bool(settings.allowForward);
    settings.reach = (std::clamp)(node["reach"].as_float(settings.reach), 3.0f, 4.5f);
    settings.reactMinMs = (std::clamp)(node["reactMinMs"].as_int(settings.reactMinMs), 0, 500);
    settings.reactMaxMs = (std::clamp)(node["reactMaxMs"].as_int(settings.reactMaxMs), settings.reactMinMs, 500);
    settings.aimTicks = (std::clamp)(node["aimTicks"].as_int(settings.aimTicks), 1, 6);
    settings.aimNoise = (std::clamp)(node["aimNoise"].as_float(settings.aimNoise), 0.0f, 3.0f);
    settings.overshoot = node["overshoot"].as_bool(settings.overshoot);
    settings.snapBack = node["snapBack"].as_bool(settings.snapBack);
    settings.restoreSlot = node["restoreSlot"].as_bool(settings.restoreSlot);
    settings.missChance = (std::clamp)(node["missChance"].as_int(settings.missChance), 0, 90);
    settings.cameraWeight = node["cameraWeight"].as_bool(settings.cameraWeight);
    settings.showBps = node["showBps"].as_bool(settings.showBps);
    settings.showPath = node["showPath"].as_bool(settings.showPath);
    settings.showPlacement = node["showPlacement"].as_bool(settings.showPlacement);
}
