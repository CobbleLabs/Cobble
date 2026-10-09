#include "aimassist.h"
#include "antibot.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"
#include "../../util/inject_trace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

AimAssist* aimAssist = nullptr;

// How long a rotation write keeps the claim alive. Five client ticks: long enough that the
// 20 Hz consumers never sample a gap between two 500 Hz writes, short enough that letting go
// of the mouse hands the head back within a fifth of a second.
static constexpr unsigned long long kRotClaimTailMs = 250ull;

// Where inside the target's hitbox the aim sits, as a fraction of its height.
// 0.88 of a 1.8-block player box is ~1.58 above the feet: the head, just under the
// crown so vertical tracking error still lands on the box instead of skimming past
// the top face. Expressing it as a ratio keeps it on the head for a sneaking, riding
// or otherwise shorter box too.
static constexpr double kHeadRatio = 0.88;

// Aim height for every version except 1.7.10, which runs the jump machinery further down and
// has its own point. 0.72 of a 1.8-block box is ~1.30 above the feet: the upper chest.
//
// This is deliberately not either extreme. The hitbox centre is what the module used to aim
// at, and it holds the camera still, but 0.9 above the feet is the abdomen. The head at 0.88
// tracks the part of a player that moves most, which is what made the camera nod on every
// jump. The upper chest is high on the body while still being close enough to the box centre
// that a jump barely moves it.
static constexpr double kAimRatio1_8_9 = 0.72;

// Upper-torso height, as a fraction of the hitbox, used while the target is airborne.
// 0.8 of a 1.8-block box is ~1.44 above the reference feet height.
static constexpr double kChestRatio = 0.80;

// How much of a jump's vertical displacement the aim point follows.
//
// The two ends are both worse than the middle. At 0 the point is measured from the
// ground the target took off from: the crosshair barely moves, but by the apex the aim
// has slid down to the target's shins. At 1 it is measured from the live box: the aim
// stays on the torso, but the crosshair travels the full ~1.25 blocks of the jump,
// which is the flick this was written to remove. Half tracks the body closely enough
// to keep the point on the waist while cutting the travel to roughly a third.
static constexpr double kJumpFollow = 0.5;

// Extra aim height on 1.7.10 only, in blocks, applied after the hitbox clamp so it always
// takes full effect. 1.0 is one block; a player is 1.8 tall and the eye line sits at 1.62.
//
// Was 1.6 while the aim was being biased to paper over a target box that sat a whole
// yOffset too low. getAimPoint now rebases that box on posY, so the aim lands on the head
// on its own and this is back to being a pure trim knob. Positive aims higher.
static constexpr double kAimHeightBias1_7_10 = 0.0;

// Time constant of the head <-> chest ease, in 1/seconds. A vanilla jump takes about
// half a second to reach its apex, so the offset has to be fully in place well before
// then while still being slow enough that no single update steps the camera.
static constexpr float kAimHeightEaseRate = 8.0f;

// Separation from the ground, in blocks, at which the aim is fully on the chest. A vanilla
// jump peaks near 1.25, so completing the shift around half a block up puts it in place well
// before the apex -- which is exactly where head tracking would reverse direction and start
// the nod. Driving this off measured height rather than the on/off flag is what makes the
// takeoff progressive instead of a step.
static constexpr double kFullAirSeparation = 0.55;

// What leaving the ground is worth on its own, before any height is gained. Without it the
// shift would not begin until the target had already climbed, which is too late.
static constexpr float kTakeoffAirFloor = 0.35f;

// Response multiplier while either player is off the ground, applied to both axes. Softening
// the response turns the residual motion into a drift instead of a chase, which is what stops
// the camera nodding through a trade where both players are jumping and taking knockback.
static constexpr float kMotionDamping = 0.45f;

// How much of the chest shift a *local* jump is worth.
//
// Kept small on purpose. When the target jumps, its head is the thing moving and dropping to
// the chest is the fix. When the player jumps, the target is standing still and only the
// camera moves -- the aim point is fine where it is, so pulling it down the body would just
// give up the head for nothing. The stability there comes from the pitch damping instead,
// which a local jump still earns in full.
static constexpr float kLocalAirHeightShare = 0.25f;

// How long the target's feet must hold one height before it counts as landed. Remote
// positions arrive at ~20 Hz, so the window has to absorb a whole packet gap and the brief
// pause at the apex of a jump without absorbing an actual landing. 150 ms was inside the
// jitter of a 160 ms connection and mid-air stalls were being read as landings.
static constexpr float kFeetSettleMs = 260.0f;

// Vertical movement below this is packet noise, not a jump.
static constexpr double kFeetMoveEpsilon = 0.003;

// How far the ground anchor may lag below the live box. A jump lifts a player about
// 1.25 blocks, so a larger gap is a stale anchor rather than a jump -- a target that
// never reported landing. Letting it lag without bound pushed the aim point under the
// hitbox, where the clamp pinned it to the feet. Tracking the box once the gap is
// reached keeps the aim steady instead of snapping it back.
static constexpr double kMaxAnchorLag = 1.4;

// Extra reach a target keeps once it is acquired. A strafing player crosses the
// configured distance boundary several times a second, and handing the aim back on
// each crossing is the "loses the target for no reason" behaviour.
static constexpr double kLockDistanceSlack = 1.5;

// Lock In trades give-up distance for stickiness: the target is meant to survive a
// disengage-and-chase, not just a strafe, so it is held well past attack range.
static constexpr double kLockInDistanceSlack = 4.0;

void AimAssist::markRotationWrite() {
    lastRotWriteMs_.store(GetTickCount64(), std::memory_order_release);
}

bool AimAssist::isRewritingRotation() const {
    if (!enabled)
        return false;
    const unsigned long long last = lastRotWriteMs_.load(std::memory_order_acquire);
    if (last == 0ull)
        return false;
    const unsigned long long now = GetTickCount64();
    return now >= last && (now - last) <= kRotClaimTailMs;
}

// Entity.onGround for the target, resolved lazily off the already-cached Entity
// class. A runtime-obfuscated client with no alias for it leaves the field null, and
// the aim then simply stays on the head as it did before.
static jfieldID g_aim_fid_onGround = nullptr;
static bool g_aimResolvedOnGround = false;

static void resolveAimOnGround(JNIEnv* env) {
    if (g_aimResolvedOnGround || !JniCache::cls_Entity)
        return;
    g_aimResolvedOnGround = true;

    for (const char* name : {"onGround", "field_70122_E", "C"}) {
        g_aim_fid_onGround = Java::GetFieldID(env, JniCache::cls_Entity, name, "Z");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (g_aim_fid_onGround)
            return;
    }
}

// Vertical eye offset of a player, measured from its feet.
static constexpr double kEyeHeight = 1.62;

// Ground truth for where the crosshair actually lands: Minecraft's own raytrace result,
// Minecraft.objectMouseOver.hitVec.
//
// Every other number in the vertical trace is derived from this module's own assumptions
// about eye height and coordinate spaces, so it agrees with itself whether or not those
// assumptions hold. This one does not: the game computed it from the real camera and the
// rotation we wrote, so comparing its Y against the target's feet settles where the aim
// really is without trusting any of our arithmetic.
static jfieldID g_aim_fid_hitVec = nullptr;
static jfieldID g_aim_fid_vecY = nullptr;
static bool g_aimResolvedHitVec = false;

static void resolveAimHitVec(JNIEnv* env) {
    if (g_aimResolvedHitVec || !JniCache::cls_MovingObjectPosition || !JniCache::cls_Vec3)
        return;
    g_aimResolvedHitVec = true;

    for (const char* name : {"hitVec", "field_72307_f"}) {
        g_aim_fid_hitVec = Java::GetFieldID(env, JniCache::cls_MovingObjectPosition, name, "Lnet/minecraft/util/Vec3;");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (g_aim_fid_hitVec)
            break;
    }
    for (const char* name : {"yCoord", "field_72448_b"}) {
        g_aim_fid_vecY = Java::GetFieldID(env, JniCache::cls_Vec3, name, "D");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (g_aim_fid_vecY)
            break;
    }
}

// Y of the crosshair's raytrace hit, or NaN when the game is not hitting anything.
// hitType: 0 = nothing, 1 = block, 2 = the entity we are tracking, 3 = another entity.
static double readCrosshairHitY(JNIEnv* env, jobject trackedTarget, int& hitType) {
    hitType = 0;
    const double kNone = std::numeric_limits<double>::quiet_NaN();
    resolveAimHitVec(env);
    if (!g_aim_fid_hitVec || !g_aim_fid_vecY || !JniCache::fid_objectMouseOver || !mc.isValid())
        return kNone;

    jobject mop = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
    if (!mop) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return kNone;
    }

    double y = kNone;
    jobject entityHit = JniCache::fid_entityHit ? env->GetObjectField(mop, JniCache::fid_entityHit) : nullptr;
    hitType = entityHit ? (trackedTarget && env->IsSameObject(entityHit, trackedTarget) ? 2 : 3) : 1;
    if (entityHit)
        env->DeleteLocalRef(entityHit);

    jobject vec = env->GetObjectField(mop, g_aim_fid_hitVec);
    if (vec) {
        y = env->GetDoubleField(vec, g_aim_fid_vecY);
        env->DeleteLocalRef(vec);
    }
    env->DeleteLocalRef(mop);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return y;
}

// The module loop runs at ~500 Hz, but an early-out (menu open, no target) can leave
// a timer running far longer. Anything outside that cadence collapses to one period,
// because a large dt makes an exponential step cover most of its remaining distance
// in a single update -- the visible snap.
static float stepSeconds(Timer& timer) {
    float dt = timer.Elapsed<std::chrono::duration<float, std::milli>>() / 1000.0f;
    timer.Start();
    constexpr float kLoopPeriodSec = 0.002f;  // ModuleManager cadence
    if (!(dt > 0.0001f) || dt > 0.02f)
        dt = kLoopPeriodSec;
    return dt;
}

// ── C03PacketPlayer resolution for packet-rotation (mirrors clutch) ──────────
// Resolved lazily on the network thread inside onOutgoingPacket, so no cross-
// thread races on these statics.
static jclass g_aimC03 = nullptr;
static jfieldID g_aim_fid_yaw = nullptr;
static jfieldID g_aim_fid_pitch = nullptr;
static jfieldID g_aim_fid_rotating = nullptr;
static bool g_aimResolvedC03 = false;

static void resolveAimC03(JNIEnv* env) {
    if (g_aimResolvedC03)
        return;
    g_aimResolvedC03 = true;

    jclass localClass = nullptr;
    if (Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", localClass)) {
        g_aimC03 = (jclass)env->NewGlobalRef(localClass);

        g_aim_fid_yaw = Java::GetFieldID(env, g_aimC03, "yaw", "F");
        if (!g_aim_fid_yaw)
            g_aim_fid_yaw = Java::GetFieldID(env, g_aimC03, "field_149476_e", "F");

        g_aim_fid_pitch = Java::GetFieldID(env, g_aimC03, "pitch", "F");
        if (!g_aim_fid_pitch)
            g_aim_fid_pitch = Java::GetFieldID(env, g_aimC03, "field_149473_f", "F");

        g_aim_fid_rotating = Java::GetFieldID(env, g_aimC03, "rotating", "Z");
        if (!g_aim_fid_rotating)
            g_aim_fid_rotating = Java::GetFieldID(env, g_aimC03, "field_149481_i", "Z");

        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
}

// Weapon item IDs
namespace ItemIds {
// Swords
constexpr int WOOD_SWORD = 268;
constexpr int STONE_SWORD = 272;
constexpr int IRON_SWORD = 267;
constexpr int DIAMOND_SWORD = 276;
constexpr int GOLD_SWORD = 283;

// Axes
constexpr int WOOD_AXE = 271;
constexpr int STONE_AXE = 275;
constexpr int IRON_AXE = 258;
constexpr int DIAMOND_AXE = 279;
constexpr int GOLD_AXE = 286;
}  // namespace ItemIds

// GCD calculation based on Minecraft's mouse sensitivity formula
static float calculateGCD(float sensitivity) {
    float f = sensitivity * 0.6f + 0.2f;
    return f * f * f * 1.2f;
}

// Strict GCD application - real mice ARE quantized, AC detects when we skip GCD
static float applyGCDStrict(float delta, float gcd) {
    if (gcd < 0.0001f)
        return delta;
    return std::round(delta / gcd) * gcd;
}

// Keep an absolute pitch inside the vertical limit without leaving the mouse grid.
// Clamping to exactly +-90 truncates the delta between consecutive rotations into
// something no mouse could report, and parks the pitch on a value no human holds --
// both of them vertical-aim tells. Stepping back toward zero by whole GCD steps stays
// on the grid by construction, whatever the incoming value was.
static float limitPitchOnGrid(float pitch, float gcd) {
    constexpr float kPitchLimit = 89.9f;
    if (std::abs(pitch) <= kPitchLimit)
        return pitch;
    if (gcd < 0.0001f)
        return pitch > 0.0f ? kPitchLimit : -kPitchLimit;
    const float steps = std::ceil((std::abs(pitch) - kPitchLimit) / gcd);
    return pitch > 0.0f ? pitch - steps * gcd : pitch + steps * gcd;
}

// Vary the integer step counts of the two axes.
//
// A real mouse reports whole counts, so a real rotation delta is always an exact
// multiple of the GCD and its floor divisor is ~0. Being "perfectly aligned" is
// therefore normal, not suspicious, and nudging a delta by a FRACTION of a step to
// disguise it is precisely what a gcd check detects — on both axes at once, and
// hardest on pitch, whose tracking deltas are small enough that a fraction of a
// step is a large relative error.
//
// What can still read as mechanical is the two axes' step counts moving in
// lockstep. So vary the counts themselves and stay on the grid: borrow or lend a
// whole step, occasionally, on one axis. The caller settles the rotation residual
// after this runs, so a step taken here is paid back on the next frame rather than
// accumulating into aim error.
static void varyStepCounts(float& yawDelta, float& pitchDelta, float gcd, std::mt19937& rng) {
    if (gcd < 0.0001f)
        return;

    const long yawSteps = std::lround(yawDelta / gcd);
    const long pitchSteps = std::lround(pitchDelta / gcd);
    if (yawSteps == 0 && pitchSteps == 0)
        return;

    std::uniform_int_distribution<int> roll(0, 99);
    std::uniform_int_distribution<int> pick(0, 1);

    // A locked step ratio across frames is the tell worth breaking. Only touch the
    // axis that is actually carrying several steps, so a 1-step correction is never
    // doubled or cancelled outright.
    const bool yawRoomy = std::labs(yawSteps) >= 2;
    const bool pitchRoomy = std::labs(pitchSteps) >= 2;
    if (!yawRoomy && !pitchRoomy)
        return;
    if (roll(rng) >= 14)
        return;

    const bool touchYaw = yawRoomy && (!pitchRoomy || pick(rng) == 0);
    float& target = touchYaw ? yawDelta : pitchDelta;
    const long steps = touchYaw ? yawSteps : pitchSteps;
    const long adjusted = steps + (pick(rng) == 0 ? 1 : -1);
    target = static_cast<float>(adjusted) * gcd;
}

static float g_cachedGCD = 0.0f;

// AimAssist Implementation
AimAssist::AimAssist() : Module("AimAssist") {
    keybind = 0;
    std::random_device rd;
    rng.seed(rd());
}

void AimAssist::releaseCachedTarget(JNIEnv* env) {
    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    if (!cachedTarget || !env)
        return;

    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteGlobalRef(cachedTarget);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    cachedTarget = nullptr;
    cachedTargetMissingScans_ = 0;
}

void AimAssist::resetTargetState(JNIEnv* env) {
    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    releaseCachedTarget(env);
    targetHasHitbox = false;
    targetChangedThisFrame = false;
    cachedTargetMissingScans_ = 0;
    aimHeightBlend_ = 0.0f;
    aimHeightTimer_.Start();
    localAirBlend_ = 0.0f;
    targetAirBlend_ = 0.0f;
    motionDamping_ = 1.0f;
    hasGroundAnchor_ = false;
    controllerTimer.Start();
    simulatedJitterYaw = simulatedJitterPitch = 0.0f;
    simulatedJitterTargetYaw = simulatedJitterTargetPitch = 0.0f;
    appliedJitterYaw_ = appliedJitterPitch_ = 0.0f;
    yawRotationResidual = pitchRotationResidual = 0.0;
    // Drop the velocity estimate with the target it was measured from.
    predVelX = predVelY = predVelZ = 0.0;
    predTimer.Start();
}

void AimAssist::onWorldChange() {
    resetTargetState(Java::Env());
    targetResetRequested_.store(true, std::memory_order_release);
}

void AimAssist::onEnable() {
    log_ok("AimAssist enabled");
    resetTargetState(Java::Env());
    targetResetRequested_.store(false, std::memory_order_release);
}

void AimAssist::onDisable() {
    log_ok("AimAssist disabled");
    resetTargetState(Java::Env());
    targetResetRequested_.store(false, std::memory_order_release);
}

double AimAssist::retentionSlack() const {
    return settings.lockIn ? kLockInDistanceSlack : kLockDistanceSlack;
}

float AimAssist::wrapTo180(float value) {
    float res = std::fmod(value + 180.f, 360.f);
    if (res < 0)
        res += 360.f;
    return res - 180.f;
}

std::pair<float, float> AimAssist::getAngles(double x1, double y1, double z1, double x2, double y2, double z2) {
    float d_x = (float)(x2 - x1);
    float d_y = (float)(y2 - y1);
    float d_z = (float)(z2 - z1);

    constexpr float PI = 3.14159265358979323846f;
    float hypothenuse = std::sqrt(d_x * d_x + d_z * d_z);
    float yaw = std::atan2(d_z, d_x) * 180.f / PI - 90.f;
    float pitch = -std::atan2(d_y, hypothenuse) * 180.f / PI;

    return std::make_pair(yaw, pitch);
}

float AimAssist::randFloat(float min, float max) {
    std::uniform_real_distribution<float> dis(min, max);
    return dis(rng);
}

int AimAssist::randInt(int min, int max) {
    std::uniform_int_distribution<int> dis(min, max);
    return dis(rng);
}

void AimAssist::onUpdate() {
    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    // Default off each tick; applyAimController re-arms it only while actively
    // aiming with packetRotation, so idle/no-target ticks never rewrite packets.
    hasServerRotation_.store(false, std::memory_order_relaxed);
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    if (targetResetRequested_.exchange(false, std::memory_order_acq_rel))
        resetTargetState(env);

    if (!player.isValid()) {
        markAimIdle();
        return;
    }
    if (Menu::isOpen) {
        markAimIdle();
        return;
    }
    if (mc.isInGui()) {
        markAimIdle();
        return;
    }

    // Toggle the armor filter on/off with its dedicated keybind (edge-triggered).
    if (settings.armorFilterKeybind != 0) {
        bool keyDown = (GetAsyncKeyState(settings.armorFilterKeybind) & 0x8000) != 0;
        if (keyDown && !armorFilterKeyPrev_) {
            settings.armorFilterEnabled = !settings.armorFilterEnabled;
            NotificationManager::add("Armor Filter", settings.armorFilterEnabled);
        }
        armorFilterKeyPrev_ = keyDown;
    }

    bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (!settings.alwaysAim && !mouseDown) {
        // Letting go of the attack button normally ends the engagement, so the next
        // click picks a target fresh. Lock In is exactly the opt-out: it holds the
        // acquired entity across releases so re-clicking resumes on the same player.
        if (!settings.lockIn)
            resetTargetState(env);
        markAimIdle();
        return;
    }

    if (settings.breakBlocks && mouseDown) {
        if (mc.isTargetingBlock()) {
            markAimIdle();
            return;
        }
    }

    // Weapon-only check
    if (settings.weaponOnly && !isHoldingWeapon()) {
        markAimIdle();
        return;
    }

    float localYaw = player.getYaw();
    float localPitch = player.getPitch();
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();
    measurePosYFeetDelta(env);
    // Angles must originate from the eyes, not the feet, or the pitch aims ~1.62 blocks
    // too high (the "vertical aims above the player" bug). posY is already the eye
    // height on 1.7.10, so adding the offset there aimed a body-length too LOW instead.
    double eyeY = localY - posYFeetDelta_ + kEyeHeight;
    targetChangedThisFrame = false;

    double targetX = 0.0;
    double targetY = 0.0;
    double targetZ = 0.0;

    if (!findTarget(targetX, targetY, targetZ)) {
        // Do not leave the last correction residual alive when a target scan
        // misses. Otherwise the next click can inherit an old direction and pull
        // the camera toward the floor or a nearby block.
        yawRotationResidual = pitchRotationResidual = 0.0;
        // The velocity estimate belongs to a target we no longer have; a stale
        // lead is what makes the next acquisition start with a flick.
        predVelX = predVelY = predVelZ = 0.0;
        predTimer.Start();
        controllerTimer.Start();
        return;
    }

    // Estimate the target's velocity (blocks/sec, smoothed).
    //
    // Remote entity positions only advance on server ticks (~20 Hz) while this
    // module runs every ~2 ms, so differentiating on every iteration divided a
    // whole tick of movement by one loop period: the estimate spiked to 20-50x
    // the real speed on the single iteration where the step landed and decayed to
    // zero in between. Prediction turned that sawtooth into a random flick.
    // Sample on actual movement instead, over the time since the last movement.
    {
        // A target switch is a position discontinuity, not motion: re-baseline so
        // the gap between two players never reads as velocity.
        if (targetChangedThisFrame) {
            predVelX = predVelY = predVelZ = 0.0;
            predPrevX = targetX;
            predPrevY = targetY;
            predPrevZ = targetZ;
            predTimer.Start();
        }

        const double jdx = targetX - predPrevX;
        const double jdy = targetY - predPrevY;
        const double jdz = targetZ - predPrevZ;
        const double moved2 = jdx * jdx + jdy * jdy + jdz * jdz;
        const float dtp = predTimer.Elapsed<>() / 1000.0f;

        if (moved2 > 1e-8 && dtp >= 0.008f) {
            if (dtp > 0.5f || moved2 > 9.0) {
                // Hitch or teleport - not motion worth leading.
                predVelX = predVelY = predVelZ = 0.0;
            } else {
                const double sm = 0.35;
                predVelX += ((jdx / dtp) - predVelX) * sm;
                predVelY += ((jdy / dtp) - predVelY) * sm;
                predVelZ += ((jdz / dtp) - predVelZ) * sm;
            }
            predPrevX = targetX;
            predPrevY = targetY;
            predPrevZ = targetZ;
            predTimer.Start();
        } else if (dtp > 0.3f) {
            // Standing still long enough that the last estimate is stale.
            predVelX = predVelY = predVelZ = 0.0;
            predPrevX = targetX;
            predPrevY = targetY;
            predPrevZ = targetZ;
            predTimer.Start();
        }

        // A sprint-jumping player tops out near 8 blocks/sec; anything past this
        // is a sampling artifact, not something to lead.
        predVelX = std::clamp(predVelX, -12.0, 12.0);
        predVelY = std::clamp(predVelY, -12.0, 12.0);
        predVelZ = std::clamp(predVelZ, -12.0, 12.0);
    }

    // Resolve the aim point from the target's REAL position. Feeding a predicted
    // position in here tripped getAimPoint's "detached hitbox" guards as soon as
    // the lead passed 1.5 blocks: the box got rebuilt around the predicted point
    // and the aim teleported by the whole lead - a hard flick, and the only way
    // prediction affected the result at all, since the aim point is otherwise
    // derived from the (unpredicted) AABB. Prediction is a bounded offset on the
    // resolved point instead.
    // Damping runs everywhere; only 1.7.10 additionally runs the head/chest jump machinery.
    const double targetFeetY = targetHasHitbox ? (double)targetHitboxMinY : targetY;
    updateMotionDamping(env, targetFeetY);
    if (usesJumpAimLogic())
        updateAimHeightBlend(targetFeetY);

    double aimX, aimY, aimZ;
    getAimPoint(targetX, targetY, targetZ, aimX, aimY, aimZ);

    // Prediction: lead the aim point by the target's velocity so tracking sits
    // ahead of a strafing target instead of trailing it.
    //
    // The lead is bounded by the hitbox itself, not by a constant. A player box is
    // 0.6 blocks wide, so the +-0.75 block clamp this used to carry allowed an aim
    // point nearly half a block OUTSIDE the box: the rotation pointed at empty air,
    // the client's own raytrace stopped returning the entity, and any attack that
    // still went out was one the server raytraced into nothing -- which is exactly
    // what Intave reports as AttackRaytrace "attacked a player out of sight".
    // Leading is only worth anything while the led point is still on the target.
    if (settings.prediction > 0.0f) {
        const double leadSec = (settings.prediction / 100.0) * 0.16;
        // Keep the ray off the faces: a point exactly on the plane is a coin flip
        // between hit and miss once the server's copy of the position differs from
        // ours by a rounding step.
        constexpr double kFaceInset = 0.09;
        // Room from the aim point to each face, so a point already pushed to the near
        // side by aimAtClosestPoint gets the asymmetric budget it actually has.
        const auto lead = [&](double vel, double point, double lo, double hi) {
            const double back = std::max(0.0, (point - lo) - kFaceInset);
            const double fwd = std::max(0.0, (hi - point) - kFaceInset);
            return std::clamp(vel * leadSec, -back, fwd);
        };
        aimX += lead(predVelX, aimX, resolvedBox_.minX, resolvedBox_.maxX);
        aimZ += lead(predVelZ, aimZ, resolvedBox_.minZ, resolvedBox_.maxZ);
        // The vertical lead fades out as the target leaves the ground. A jump is a
        // parabola, so a linear lead points above the target on the way up and below it
        // on the way down -- it adds crosshair travel to the exact moment the anchored
        // aim point is there to remove it. Fading on the same eased signal keeps the
        // hand-off continuous.
        if (aimsPitch()) {
            const double airFade = 1.0 - (double)aimHeightBlend_;
            aimY += lead(predVelY, aimY, resolvedBox_.minY, resolvedBox_.maxY) * airFade;
        }
    }

    // How far the rotation may be nudged off the aim point before the ray leaves the
    // box, in degrees, measured at the target's current distance. packetRotation adds
    // its jitter to the C03 only -- the server raytraces a rotation the camera never
    // had -- so that offset has to be budgeted against the target's angular size or it
    // is a miss the player cannot see coming. Wide box and close range means a large
    // budget; at max reach the budget collapses and so does the jitter.
    {
        float yawBudget = 0.0f, pitchBudget = 0.0f;
        const double dx = aimX - localX;
        const double dz = aimZ - localZ;
        const double flat = std::sqrt(dx * dx + dz * dz);
        const double dy = aimY - eyeY;
        const double dist = std::sqrt(flat * flat + dy * dy);
        const double halfW =
            std::min(resolvedBox_.maxX - resolvedBox_.minX, resolvedBox_.maxZ - resolvedBox_.minZ) * 0.5;
        const double halfH = (resolvedBox_.maxY - resolvedBox_.minY) * 0.5;
        if (flat > 0.05 && halfW > 0.0)
            yawBudget = (float)(std::atan2(halfW - 0.09, flat) * 57.29577951308232);
        if (dist > 0.05 && halfH > 0.0)
            pitchBudget = (float)(std::atan2(halfH - 0.09, dist) * 57.29577951308232);
        // Only part of the budget is spent, because the rendered rotation is itself
        // still converging on the aim point and owns the rest of it.
        packetYawBudget_.store(std::clamp(yawBudget * 0.5f, 0.0f, 3.0f), std::memory_order_release);
        packetPitchBudget_.store(std::clamp(pitchBudget * 0.5f, 0.0f, 3.0f), std::memory_order_release);
    }

    auto [yawTarget, pitchTarget] = getAngles(localX, eyeY, localZ, aimX, aimY, aimZ);
    const double aimDx = aimX - localX;
    const double aimDz = aimZ - localZ;
    // When the two horizontal positions coincide there is no direction to turn
    // toward. Keep yaw where it is instead of letting atan2(0, 0) manufacture -90°.
    if (aimDx * aimDx + aimDz * aimDz < 1e-6)
        yawTarget = localYaw;

    float yawDiff = wrapTo180(-(localYaw - yawTarget));
    float pitchDiff = wrapTo180(-(localPitch - pitchTarget));

    // Vertical-solve trace. "aimAboveFeet" should read ~1.58 on a grounded target and
    // "eyeAboveFeet" ~1.62 on every version; anything else localises the fault to the
    // aim point or to the eye origin without having to guess from the crosshair.
    if (verticalTraceTimer_.Elapsed<>() >= 250.0f) {
        verticalTraceTimer_.Start();
        int crosshairHitType = 0;
        const double crosshairHitY = readCrosshairHitY(env, cachedTarget, crosshairHitType);
        log_debug(
            "[AIM-V] %s posY->feet=%.2f(known=%d) fields(bbox=%d,aabbY=%d,posY=%d) hitbox=%d "
            // wx/wz must both read 0.600 for a player. A swapped AABB field mapping binds
            // the wrong axis and shows up here as a wrong width or depth long before it
            // shows up as a wrong aim height.
            // Which entity is being tracked, not just how high the aim sits on it. With
            // several candidates around -- lobby NPCs, a second player further back --
            // a correct aim height on the wrong target looks exactly like a wrong aim
            // height on the right one.
            "targetXZ=(%.1f,%.1f) dist=%.2f "
            "targetPosY=%.3f box=[%.3f,%.3f] h=%.3f wx=%.3f wz=%.3f localPosY=%.3f eyeY=%.3f aimY=%.3f "
            // HIT* is the only line here the module did not compute from its own
            // assumptions. hitType 2 means the crosshair is on the tracked entity, and
            // then HITABOVEFEET is literally where the aim landed on the body.
            "aimAboveFeet=%.3f hitType=%d HITABOVEFEET=%.3f anchor=%.3f lag=%.3f onGround=%s airborne=%d "
            "blend=%.2f vdamp=%.2f localAir=%d pitch=%.1f/%.1f",
            Java::Version == MinecraftVersion::MC_1_7_10  ? "1.7.10"
            : Java::Version == MinecraftVersion::MC_1_8_9 ? "1.8.9"
                                                          : "UNDETECTED",
            posYFeetDelta_, (int)posYFeetDeltaKnown_, JniCache::fid_boundingBox != nullptr,
            JniCache::fid_aabb_minY != nullptr, JniCache::fid_posY != nullptr, (int)targetHasHitbox, targetX, targetZ,
            std::sqrt((targetX - localX) * (targetX - localX) + (targetY - localY) * (targetY - localY) +
                      (targetZ - localZ) * (targetZ - localZ)),
            targetY,
            resolvedBox_.minY, resolvedBox_.maxY, resolvedBox_.maxY - resolvedBox_.minY,
            resolvedBox_.maxX - resolvedBox_.minX, resolvedBox_.maxZ - resolvedBox_.minZ, localY, eyeY, aimY,
            aimY - resolvedBox_.minY, crosshairHitType, crosshairHitY - resolvedBox_.minY, groundFeetY_,
            resolvedBox_.minY - groundFeetY_,
            targetOnGroundRaw_ < 0 ? "n/a" : targetOnGroundRaw_ ? "true" : "false", (int)targetAirborne_,
            aimHeightBlend_, motionDamping_, (int)!player.isOnGround(), localPitch, pitchTarget);
    }

    // Stop On Hitbox: If crosshair is already inside the hitbox, let user aim freely.
    // Lock In means "stay pinned", so it never hands the rotation back mid-hitbox.
    if (settings.stopOnHitbox && targetHasHitbox && !settings.lockIn) {
        float hitboxWidth = targetHitboxMaxX - targetHitboxMinX;
        float hitboxHeight = targetHitboxMaxY - targetHitboxMinY;

        if (hitboxWidth > 0.01f && hitboxHeight > 0.01f) {
            // Calculate angles to hitbox edges
            float yawToMinX = getAngles(localX, eyeY, localZ, targetHitboxMinX, aimY, targetHitboxMinZ).first;
            float yawToMaxX = getAngles(localX, eyeY, localZ, targetHitboxMaxX, aimY, targetHitboxMaxZ).first;
            float pitchToMinY = getAngles(localX, eyeY, localZ, aimX, targetHitboxMinY, aimZ).second;
            float pitchToMaxY = getAngles(localX, eyeY, localZ, aimX, targetHitboxMaxY, aimZ).second;

            float yawDiffToMinX = wrapTo180(-(localYaw - yawToMinX));
            float yawDiffToMaxX = wrapTo180(-(localYaw - yawToMaxX));
            float pitchDiffToMinY = wrapTo180(-(localPitch - pitchToMinY));
            float pitchDiffToMaxY = wrapTo180(-(localPitch - pitchToMaxY));

            // Inside hitbox: signs of diffs to opposite edges are different
            bool insideYaw = (yawDiffToMinX * yawDiffToMaxX) <= 0.0f;
            bool insidePitch = (pitchDiffToMinY * pitchDiffToMaxY) <= 0.0f;

            bool insideHitbox = aimsPitch() ? (insideYaw && insidePitch) : insideYaw;

            if (insideHitbox) {
                markAimIdle();
                return;
            }
        }
    }

    // Hurt Time Lock: Instant aim when switching targets
    if (settings.targetMode == TargetMode::SWITCH && settings.switchSortBy == SortBy::HURT_TIME &&
        settings.hurtTimeLock && targetChangedThisFrame) {
        // Snapping is the point here, but the snap still has to land somewhere a mouse
        // could put it: a whole number of GCD steps away from the current rotation.
        // Writing the exact target instead leaves a delta that is not a multiple of the
        // step, which is the one thing about a flick that a gcd check can see. What is
        // given up is at most half a step, well below the resolution of a hitbox.
        const float gcdSnap = g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(mc.getMouseSensitivity());
        g_cachedGCD = gcdSnap;
        const float yawNow = player.getYaw();
        const float snappedYaw = yawNow + applyGCDStrict(wrapTo180(yawTarget - yawNow), gcdSnap);
        player.setYaw(snappedYaw);
        player.setPrevYaw(snappedYaw);
        if (aimsPitch()) {
            const float pitchNow = player.getPitch();
            const float stepped = pitchNow + applyGCDStrict(pitchTarget - pitchNow, gcdSnap);
            const float snappedPitch = limitPitchOnGrid(stepped, gcdSnap);
            player.setPitch(snappedPitch);
            player.setPrevPitch(snappedPitch);
        }
        markRotationWrite();
        // The rotation was written absolutely, so no jitter offset is folded in
        // any more - forget it instead of unwinding it on the next update.
        appliedJitterYaw_ = appliedJitterPitch_ = 0.0f;
        markAimIdle();
        return;
    }

    cachedMouseSensitivity = mc.getMouseSensitivity();
    applyAimController(yawDiff, pitchDiff);
}

bool AimAssist::readCachedTarget(double& targetX, double& targetY, double& targetZ) {
    JNIEnv* env = Java::Env();
    if (!env || !cachedTarget)
        return false;

    const auto drop = [&](const char* why) {
        log_debug("[AimAssist] target dropped: %s", why);
        releaseCachedTarget(env);
        targetHasHitbox = false;
        return false;
    };

    if (env->IsSameObject(cachedTarget, NULL))
        return drop("entity gone");
    if (!JniCache::fid_posX || !JniCache::fid_posY || !JniCache::fid_posZ)
        return drop("position fields unresolved");

    const double cX = env->GetDoubleField(cachedTarget, JniCache::fid_posX);
    const double cY = env->GetDoubleField(cachedTarget, JniCache::fid_posY);
    const double cZ = env->GetDoubleField(cachedTarget, JniCache::fid_posZ);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return drop("position read failed");
    }

    // The cached entity position is refreshed every iteration, so refresh its
    // absolute AABB with it. Keeping the first scan's box while the player strafes
    // makes the aim point drift to an old coordinate (the apparent random flick).
    if (JniCache::fid_boundingBox && JniCache::cls_AxisAlignedBB) {
        jobject aabb = env->GetObjectField(cachedTarget, JniCache::fid_boundingBox);
        if (aabb) {
            if (JniCache::fid_aabb_minX)
                targetHitboxMinX = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minX);
            if (JniCache::fid_aabb_minY)
                targetHitboxMinY = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minY);
            if (JniCache::fid_aabb_minZ)
                targetHitboxMinZ = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minZ);
            if (JniCache::fid_aabb_maxX)
                targetHitboxMaxX = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxX);
            if (JniCache::fid_aabb_maxY)
                targetHitboxMaxY = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxY);
            if (JniCache::fid_aabb_maxZ)
                targetHitboxMaxZ = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxZ);
            env->DeleteLocalRef(aabb);
            const float w = targetHitboxMaxX - targetHitboxMinX;
            const float h = targetHitboxMaxY - targetHitboxMinY;
            targetHasHitbox = w > 0.01f && h > 0.01f;
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    // Distance is the only thing that can break a lock. FOV deliberately plays no
    // part: it decides which players may be *acquired*, and testing it again here is
    // what dropped a target the moment it sprinted past the crosshair, since its
    // angular speed at point-blank range outruns any tracking speed by far.
    const double dx = cX - player.getPosX();
    const double dy = cY - player.getPosY();
    const double dz = cZ - player.getPosZ();
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > settings.distance + retentionSlack())
        return drop("out of range");

    targetX = cX;
    targetY = cY;
    targetZ = cZ;
    return true;
}

bool AimAssist::findTarget(double& targetX, double& targetY, double& targetZ) {
    // HYBRID TRACKING SYSTEM
    // 1. Fast Path (Every Frame): Read the locked target straight off its entity
    // 2. Slow Path (Scan Interval): Scan the full player list for a new target
    static Timer scanTimer;
    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    // CRITICAL: Clear any pending JNI exceptions that could cause silent failures
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        releaseCachedTarget(env);
        targetHasHitbox = false;
        log_warn("[AimAssist] Cleared JNI exception - cache reset");
    }

    const bool scanDue = scanTimer.Elapsed<>() >= 50;
    if (cachedTarget && !scanDue)
        return readCachedTarget(targetX, targetY, targetZ);
    if (!scanDue)
        return false;

    // SLOW PATH: Full Scan
    scanTimer.Start();

    auto players = mc.getPlayerList();

    const bool playerListReadFailed = env->ExceptionCheck();
    if (playerListReadFailed)
        env->ExceptionClear();

    if (friendsManager)
        friendsManager->resolvePending(players);

    if (players.empty()) {
        if (!cachedTarget)
            return false;
        // A failed JNI read says nothing about membership. A valid empty list does:
        // tolerate one scan to bridge a transient snapshot, then release the strong
        // GlobalRef before it can turn a removed entity into a persistent target.
        if (playerListReadFailed || ++cachedTargetMissingScans_ < 2)
            return readCachedTarget(targetX, targetY, targetZ);
        releaseCachedTarget(env);
        targetHasHitbox = false;
        return false;
    }

    const bool hadCachedTarget = cachedTarget != nullptr;
    float localYaw = player.getYaw();
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();

    float lowestHealth = FLT_MAX;
    float minimalAngleDiff = (float)settings.fov / 2.f;
    float closestDistance = FLT_MAX;
    float bestSwitchScore = FLT_MAX;  // Combined score for Switch mode
    bool found = false;

    jobject bestPlayerObjLocal = nullptr;
    PlayerInfo closestPlayer;
    bool lockedEntityFound = false;
    // Whether the locked entity showed up in this scan's list at all, regardless of
    // whether it passed the filters. Distinguishes "it stopped qualifying" (drop it)
    // from "the list read missed it" (keep it).
    bool lockedEntitySeen = false;
    auto releasePlayerRef = [env](PlayerInfo& candidate) {
        if (candidate.instance && env) {
            env->DeleteGlobalRef(candidate.instance);
            candidate.instance = nullptr;
        }
    };

    for (auto& p : players) {
        bool sameAsLocked = false;
        if (cachedTarget) {
            if (env->IsSameObject(cachedTarget, NULL))
                releaseCachedTarget(env);
            else
                sameAsLocked = env->IsSameObject(cachedTarget, p.instance);
        }
        if (sameAsLocked) {
            lockedEntitySeen = true;
            cachedTargetMissingScans_ = 0;
        }

        double dx = p.posX - localX;
        double dy = p.posY - localY;
        double dz = p.posZ - localZ;
        float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);

        // The target already held gets the same retention slack readCachedTarget uses,
        // so the two paths never disagree about whether it is still in range.
        float maxDist = sameAsLocked ? (float)(settings.distance + retentionSlack()) : settings.distance;
        if (dist > maxDist) {
            releasePlayerRef(p);
            continue;
        }
        if (p.isInvisible && !settings.invisibles) {
            releasePlayerRef(p);
            continue;
        }
        if (AntiBot::Filtered(p)) {
            releasePlayerRef(p);
            continue;
        }
        if (p.health <= 0.0f) {
            releasePlayerRef(p);
            continue;
        }

        if (friendsManager) {
            if (p.isOnLocalTeam && friendsManager->honorsVanillaTeams()) {
                releasePlayerRef(p);
                continue;
            }

            bool isApolloTeam = !p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid);
            if (isApolloTeam) {
                releasePlayerRef(p);
                continue;
            }

            bool isApolloNametag = !p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid);
            if (isApolloNametag) {
                releasePlayerRef(p);
                continue;
            }

            // Priority 3: Manual Friends
            bool isFriendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool isFriendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (isFriendByUUID || isFriendByName) {
                releasePlayerRef(p);
                continue;
            }

            // Priority 4: Scoreboard Teams (color matching)
            // Get local player's team color for comparison (cached per scan)
            static int localTeamColor = -1;
            static Timer colorRefreshTimer;
            if (colorRefreshTimer.Elapsed<>() > 1000) {  // Refresh every second
                localTeamColor = mc.getLocalPlayerTeamColor();
                colorRefreshTimer.Start();
            }

            ChatColor playerColor = static_cast<ChatColor>(p.teamColor);
            ChatColor localColor = static_cast<ChatColor>(localTeamColor);
            static std::string localPlayerName;
            static Timer nameRefreshTimer;
            if (localPlayerName.empty() || nameRefreshTimer.Elapsed<>() > 1000) {
                localPlayerName = mc.getLocalPlayerName();
                nameRefreshTimer.Start();
            }
            bool isScoreboardTeammate =
                friendsManager->isScoreboardTeammate(localPlayerName, p.profileName.empty() ? p.name : p.profileName) ||
                friendsManager->isTeammate(p.uuid, playerColor, localColor);
            if (isScoreboardTeammate) {
                releasePlayerRef(p);
                continue;
            }
        }

        // ── Armor filter (multi-select tiers) ─────────────────────────────────
        // Target the player if ANY of their armor pieces matches ANY checked
        // tier. ARMOR_NO_ARMOR matches players wearing nothing. Mask 0 = off.
        if (settings.armorFilterEnabled && settings.armorTierMask != 0) {
            auto tierBit = [](int id) -> int {
                if (id >= 298 && id <= 301)
                    return AimAssist::ARMOR_LEATHER;
                if (id >= 302 && id <= 305)
                    return AimAssist::ARMOR_CHAIN;
                if (id >= 306 && id <= 309)
                    return AimAssist::ARMOR_IRON;
                if (id >= 310 && id <= 313)
                    return AimAssist::ARMOR_DIAMOND;
                if (id >= 314 && id <= 317)
                    return AimAssist::ARMOR_GOLD;
                return 0;
            };
            bool match = false;
            bool hasAnyArmor = false;
            for (const auto& piece : p.armor) {
                if (piece.id > 0) {
                    hasAnyArmor = true;
                    int b = tierBit(piece.id);
                    if (b && (settings.armorTierMask & b)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match && (settings.armorTierMask & AimAssist::ARMOR_NO_ARMOR) && !hasAnyArmor)
                match = true;
            if (!match) {
                releasePlayerRef(p);
                continue;
            }
        }
        // ─────────────────────────────────────────────────────────────────────

        // Only the yaw is read out, so the heights here cancel and need no feet-space
        // correction; they are passed just to keep the call shape of the real aim path.
        float yawToTarget = getAngles(localX, localY, localZ, p.posX, p.posY, p.posZ).first;
        float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));

        // FOV gates acquisition only. The target already held is exempt, so it cannot
        // be dropped by its own angular speed when it runs past the crosshair. Nearby
        // targets get a wider acquisition cone because they can cross a narrow cone
        // entirely between the 50 ms scans.
        float acquisitionFov = settings.fov * 0.5f;
        if (dist < 3.0f)
            acquisitionFov = std::max(acquisitionFov, 150.0f);
        if (!sameAsLocked && yawDiff > acquisitionFov) {
            releasePlayerRef(p);
            continue;
        }

        // The target already held wins every comparison: this is a tracking aid, and
        // re-deciding who to follow while a fight is in progress is what made it let
        // go of a player over a fraction of a block. Switch mode is the opt-in for
        // continuous re-evaluation, and keeps its own hysteresis below -- unless Lock
        // In is on, which outranks every selection rule by definition.
        if (sameAsLocked && (settings.lockIn || settings.targetMode != TargetMode::SWITCH)) {
            if (bestPlayerObjLocal && env)
                env->DeleteLocalRef(bestPlayerObjLocal);
            bestPlayerObjLocal = env && p.instance ? env->NewLocalRef(p.instance) : nullptr;
            closestPlayer = p;
            found = true;
            lockedEntityFound = true;
        } else if (lockedEntityFound) {
            // Do not let later candidates replace the locked entity.
        } else if (settings.targetMode == TargetMode::DISTANCE) {
            if (dist < closestDistance) {
                closestDistance = dist;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        } else if (settings.targetMode == TargetMode::HEALTH) {
            if (p.health < lowestHealth) {
                lowestHealth = p.health;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        } else if (settings.targetMode == TargetMode::SWITCH) {
            // Switch mode: Always pick the best target based on switchSortBy criteria
            // Use composite score: primary criterion + small distance tie-breaker
            float score = FLT_MAX;
            float tieBreaker = dist * 0.001f;  // Small distance factor for tie-breaking

            switch (settings.switchSortBy) {
                case SortBy::DISTANCE:
                    score = dist;
                    break;
                case SortBy::HEALTH:
                    score = p.health + tieBreaker;
                    break;
                case SortBy::FOV:
                    score = yawDiff + tieBreaker;
                    break;
                case SortBy::HURT_TIME:
                    score = (float)p.hurtTime + tieBreaker;
                    break;
            }

            // Hysteresis: Give current target a score bonus to prevent rapid switching
            // Only switch if the new target is significantly better
            bool isCurrentTarget = cachedTarget && env && !env->IsSameObject(cachedTarget, NULL) &&
                                   env->IsSameObject(cachedTarget, p.instance);
            if (isCurrentTarget) {
                score -= 0.5f;  // Bonus for staying on current target
            }

            if (score < bestSwitchScore) {
                bestSwitchScore = score;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        } else  // TargetMode::FOV
        {
            if (yawDiff < minimalAngleDiff) {
                minimalAngleDiff = yawDiff;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        }

        releasePlayerRef(p);
    }

    // Do not switch merely because one otherwise-valid list snapshot omitted the
    // current entity. On the second consecutive miss, release the strong reference
    // and allow the already selected replacement to take over in this same frame.
    if (hadCachedTarget && cachedTarget && !lockedEntitySeen) {
        if (++cachedTargetMissingScans_ < 2) {
            if (bestPlayerObjLocal)
                env->DeleteLocalRef(bestPlayerObjLocal);
            return readCachedTarget(targetX, targetY, targetZ);
        }
        releaseCachedTarget(env);
    }

    if (found) {
        targetX = closestPlayer.posX;
        targetY = closestPlayer.posY;
        targetZ = closestPlayer.posZ;
        targetHitboxMinX = closestPlayer.hitboxMinX;
        targetHitboxMinY = closestPlayer.hitboxMinY;
        targetHitboxMinZ = closestPlayer.hitboxMinZ;
        targetHitboxMaxX = closestPlayer.hitboxMaxX;
        targetHitboxMaxY = closestPlayer.hitboxMaxY;
        targetHitboxMaxZ = closestPlayer.hitboxMaxZ;

        float hitboxWidth = targetHitboxMaxX - targetHitboxMinX;
        float hitboxHeight = targetHitboxMaxY - targetHitboxMinY;
        targetHasHitbox = (hitboxWidth > 0.01f) && (hitboxHeight > 0.01f);
    } else {
        targetHasHitbox = false;
    }

    if (found && bestPlayerObjLocal && (!cachedTarget || !env->IsSameObject(cachedTarget, bestPlayerObjLocal))) {
        releaseCachedTarget(env);
        cachedTarget = env->NewGlobalRef(bestPlayerObjLocal);
        targetChangedThisFrame = true;
    }

    if (bestPlayerObjLocal)
        env->DeleteLocalRef(bestPlayerObjLocal);

    if (found) {
        cachedTargetMissingScans_ = 0;
        return true;
    }

    releaseCachedTarget(env);
    targetHasHitbox = false;
    return false;
}

void AimAssist::updateSimulatedJitter() {
    float intensity = std::clamp(settings.jitterPercent, 0.0f, 100.0f) / 100.0f;
    if (intensity <= 0.0001f) {
        simulatedJitterYaw = simulatedJitterPitch = 0.0f;
        simulatedJitterTargetYaw = simulatedJitterTargetPitch = 0.0f;
        return;
    }

    if (simulatedJitterTimer.Elapsed<>() >= static_cast<float>(simulatedJitterIntervalMs)) {
        float amplitude = std::lerp(0.015f, 0.42f, intensity);
        float impulse = randFloat(0.0f, 1.0f) < (0.04f + intensity * 0.14f) ? randFloat(1.15f, 1.8f) : 1.0f;
        simulatedJitterTargetYaw = randFloat(-amplitude, amplitude) * impulse;
        simulatedJitterTargetPitch = randFloat(-amplitude * 0.65f, amplitude * 0.65f) * impulse;

        int intervalMin = static_cast<int>(std::lerp(70.0f, 11.0f, intensity));
        int intervalMax = static_cast<int>(std::lerp(140.0f, 38.0f, intensity));
        simulatedJitterIntervalMs = randInt(intervalMin, intervalMax);
        simulatedJitterTimer.Start();
    }

    float response = std::lerp(0.12f, 0.72f, intensity);
    simulatedJitterYaw = std::lerp(simulatedJitterYaw, simulatedJitterTargetYaw, response);
    simulatedJitterPitch = std::lerp(simulatedJitterPitch, simulatedJitterTargetPitch, response);
}

void AimAssist::applyAimController(float yawDiff, float pitchDiff) {
    const float dt = stepSeconds(controllerTimer);

    // Adaptive reads the whole error vector once, so both axes stay in step instead of
    // each speeding up on its own and bending the path between them.
    const float error = std::sqrt(yawDiff * yawDiff + pitchDiff * pitchDiff);
    const AimMode mode = settings.aimMode;

    // One setting per axis, mapped linearly onto the exponential response rate, so the
    // number on the slider is proportional to how fast the remaining error is eaten:
    // 1 closes ~2%/10 ms, 100 closes ~18%/10 ms. The mode only reshapes that rate.
    // Exponential response is frame-rate independent, so the same value behaves the
    // same on every machine.
    const auto alphaFor = [dt, mode, error](float speed) {
        float rate = 2.0f + std::clamp(speed, 1.0f, 100.0f) * 0.18f;
        if (mode == AimMode::ADAPTIVE)
            rate *= std::clamp(0.75f + error / 90.0f, 0.75f, 1.55f);
        else if (mode == AimMode::BLATANT)
            rate = 18.0f + std::clamp(speed, 1.0f, 100.0f) * 0.10f;
        return 1.0f - std::exp(-rate * dt);
    };

    // Both axes are damped together. Damping one alone bends the path between them: the
    // rotation would close horizontally faster than vertically and arrive at the aim point
    // along a curve rather than straight.
    float yawStep = yawDiff * alphaFor(settings.horizontalSpeed * motionDamping_);
    float pitchStep = aimsPitch() ? pitchDiff * alphaFor(settings.verticalSpeed * motionDamping_) : 0.0f;
    if (std::abs(yawDiff) < 0.03f)
        yawStep = yawDiff;
    if (aimsPitch() && std::abs(pitchDiff) < 0.03f)
        pitchStep = pitchDiff;

    updateSimulatedJitter();
    if (settings.packetRotation) {
        const float gcd = g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(mc.getMouseSensitivity());
        // Whole mouse steps, because the sent rotation has to stay on the same grid the
        // rendered one is on -- an offset that is not a multiple of the step is visible
        // to a gcd check even though the rotation itself looks fine.
        const auto steps = [gcd](float degrees) { return std::round(degrees / gcd); };
        // ...and no more steps than the target's angular size can absorb. Past that the
        // offset is not jitter any more, it is a rotation the server raytraces off the
        // hitbox while the camera sits dead centre on it, and the miss is invisible from
        // the player's side. A budget under one step means the target is too small at
        // this range to afford any offset, so the true rotation goes out unmodified.
        const float yawCap = std::floor(packetYawBudget_.load(std::memory_order_acquire) / gcd);
        const float pitchCap = std::floor(packetPitchBudget_.load(std::memory_order_acquire) / gcd);
        const float yawSteps = std::clamp(steps(simulatedJitterYaw), -yawCap, yawCap);
        const float pitchSteps =
            aimsPitch() ? std::clamp(steps(simulatedJitterPitch), -pitchCap, pitchCap) : 0.0f;
        serverYawOffset_.store(yawSteps * gcd, std::memory_order_relaxed);
        serverPitchOffset_.store(pitchSteps * gcd, std::memory_order_relaxed);
        hasServerRotation_.store(true, std::memory_order_release);
    }

    // Fold the jitter in as a bounded offset. `desired` is what the rotation
    // should currently carry (nothing at all while the jitter rides on the packet
    // instead, or on pitch when vertical correction is off), and only the difference
    // against what was already applied becomes a step. That keeps the offset at
    // exactly +-amplitude no matter how often this runs, instead of accumulating it
    // once per loop into a drift the tracking error has to fight.
    const float desiredJitterYaw = settings.packetRotation ? 0.0f : simulatedJitterYaw;
    const float desiredJitterPitch = (settings.packetRotation || !aimsPitch()) ? 0.0f : simulatedJitterPitch;
    yawStep += desiredJitterYaw - appliedJitterYaw_;
    pitchStep += desiredJitterPitch - appliedJitterPitch_;
    appliedJitterYaw_ = desiredJitterYaw;
    appliedJitterPitch_ = desiredJitterPitch;

    // Quantize what actually reaches the rotation onto the mouse GCD grid.
    //
    // A real mouse reports whole counts, so every rotation a legitimate client can
    // produce is an exact multiple of (sens * 0.6 + 0.2)^3 * 1.2. This controller
    // runs every ~2 ms and its raw step is a small fraction of one such unit, so
    // writing it straight through gave the server a pitch delta no mouse could ever
    // send — a standing gcd violation, and worst on pitch, where tracking steps are
    // small enough that the off-grid remainder dominates them.
    //
    // The residual carries the leftover fraction, so sub-step motion accumulates
    // until it is worth a whole step rather than being rounded away. Frames that owe
    // nothing write nothing, which is also what a real mouse does.
    const float gcdNow = g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(cachedMouseSensitivity);
    g_cachedGCD = gcdNow;
    yawRotationResidual += yawStep;
    pitchRotationResidual += pitchStep;
    float yawApply = applyGCDStrict(static_cast<float>(yawRotationResidual), gcdNow);
    float pitchApply = applyGCDStrict(static_cast<float>(pitchRotationResidual), gcdNow);
    varyStepCounts(yawApply, pitchApply, gcdNow, rng);
    yawRotationResidual -= yawApply;
    pitchRotationResidual -= pitchApply;

    if (std::abs(yawApply) > 0.0001f) {
        const float currentYaw = player.getYaw();
        const float nextYaw = currentYaw + yawApply;
        player.setYaw(nextYaw);
        player.setPrevYaw(std::lerp(currentYaw, nextYaw, 0.82f));
        markRotationWrite();
    }
    if (aimsPitch() && std::abs(pitchApply) > 0.0001f) {
        const float currentPitch = player.getPitch();
        // Stop just shy of straight up/down, and only ever on a whole step.
        const float nextPitch = limitPitchOnGrid(currentPitch + pitchApply, gcdNow);
        pitchApply = nextPitch - currentPitch;
        if (std::abs(pitchApply) > 0.0001f) {
            player.setPitch(nextPitch);
            player.setPrevPitch(std::lerp(currentPitch, nextPitch, 0.82f));
            markRotationWrite();
        }
    }
}

void AimAssist::onOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!enabled || !settings.packetRotation || !env)
        return;
    if (!hasServerRotation_.load(std::memory_order_acquire))
        return;

    resolveAimC03(env);
    if (!g_aimC03 || !env->IsInstanceOf(packet, g_aimC03))
        return;

    const float offY = serverYawOffset_.load(std::memory_order_relaxed);
    const float offP = serverPitchOffset_.load(std::memory_order_relaxed);

    // Compute the rotation we are about to send (render rotation + jitter offset).
    float finalYaw = 0.0f, finalPitch = 0.0f;
    bool haveYaw = g_aim_fid_yaw != nullptr;
    bool havePitch = g_aim_fid_pitch != nullptr && aimsPitch();
    if (haveYaw)
        finalYaw = env->GetFloatField(packet, g_aim_fid_yaw) + offY;
    if (havePitch) {
        const float raw = env->GetFloatField(packet, g_aim_fid_pitch) + offP;
        finalPitch = limitPitchOnGrid(raw, g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(cachedMouseSensitivity));
    }

    // Anti-duplicate (Intave AimDuplicateLook360). We force rotating=true on every
    // packet, so two consecutive look packets that carry byte-identical yaw+pitch
    // read as a "duplicate rotating look" and flag. That happens when the render
    // rotation is static (locked on a stationary target) AND the quantized jitter
    // offset lands on the same value two ticks running (guaranteed when jitter=0).
    // A real mouse never repeats a rotation exactly while the hand is moving, so
    // nudge the SENT rotation by one alternating GCD step to guarantee a fresh
    // value. The nudge stays on the mouse GCD grid (no gcd-mismatch flag) and only
    // touches the packet, so the first-person camera stays smooth. Network-thread
    // only, so plain statics are safe here.
    static float s_lastSentYaw = 0.0f;
    static float s_lastSentPitch = 0.0f;
    static bool s_haveLastSent = false;
    static float s_ditherSign = 1.0f;

    if (s_haveLastSent && haveYaw) {
        bool yawDup = std::abs(finalYaw - s_lastSentYaw) < 1e-4f;
        bool pitchDup = havePitch ? (std::abs(finalPitch - s_lastSentPitch) < 1e-4f) : true;
        if (yawDup && pitchDup) {
            float gcd = g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(cachedMouseSensitivity);
            if (gcd < 0.0001f)
                gcd = 0.15f;
            // Yaw alone is enough to make the pair fresh, and yaw alone is what a hand
            // resting on a mouse actually produces. Stepping both axes by the same signed
            // amount every time would make them perfectly correlated, which is a pattern
            // in its own right.
            //
            // Direction is chosen so the step lands back inside the hitbox budget rather
            // than always alternating: at the far edge of the box the outward step is the
            // one that turns a hit into "attacked a player out of sight", and stepping
            // inward makes the rotation just as fresh. Alternation is only the tiebreak
            // when both directions are safe.
            const float budget = packetYawBudget_.load(std::memory_order_acquire);
            float sign = s_ditherSign;
            if (budget > 0.0f && std::abs(offY + sign * gcd) > budget && std::abs(offY - sign * gcd) <= budget)
                sign = -sign;
            finalYaw += sign * gcd;
            s_ditherSign = -sign;
        }
    }

    if (haveYaw)
        env->SetFloatField(packet, g_aim_fid_yaw, finalYaw);
    if (havePitch)
        env->SetFloatField(packet, g_aim_fid_pitch, finalPitch);
    // Mark the packet as carrying a rotation so the server accepts the values.
    if (g_aim_fid_rotating)
        env->SetBooleanField(packet, g_aim_fid_rotating, JNI_TRUE);

    s_lastSentYaw = finalYaw;
    if (havePitch)
        s_lastSentPitch = finalPitch;
    s_haveLastSent = true;

    if (env->ExceptionCheck())
        env->ExceptionClear();
}

bool AimAssist::isHoldingWeapon() {
    int id = player.getHeldItemId();
    if (id == -1)
        return false;

    // Swords
    if (id == ItemIds::WOOD_SWORD || id == ItemIds::STONE_SWORD || id == ItemIds::IRON_SWORD ||
        id == ItemIds::DIAMOND_SWORD || id == ItemIds::GOLD_SWORD)
        return true;

    // Axes
    if (id == ItemIds::WOOD_AXE || id == ItemIds::STONE_AXE || id == ItemIds::IRON_AXE || id == ItemIds::DIAMOND_AXE ||
        id == ItemIds::GOLD_AXE)
        return true;

    return false;
}

void AimAssist::getAimPoint(double targetX, double targetY, double targetZ, double& aimX, double& aimY, double& aimZ) {
    const double localX = player.getPosX();
    const double localZ = player.getPosZ();

    double minX, maxX, minY, maxY, minZ, maxZ;
    if (targetHasHitbox) {
        minX = targetHitboxMinX;
        maxX = targetHitboxMaxX;
        minY = targetHitboxMinY;
        maxY = targetHitboxMaxY;
        minZ = targetHitboxMinZ;
        maxZ = targetHitboxMaxZ;

        // AABB values are absolute world coordinates. If a client mapping or
        // stale entity snapshot supplies a box in a different origin, rebuild
        // its offsets around the current feet position instead of aiming at a
        // detached box (which looks like a nearby block lock).
        const double boxCenterX = (minX + maxX) * 0.5;
        const double boxCenterZ = (minZ + maxZ) * 0.5;
        if (std::abs(boxCenterX - targetX) > 1.5 || std::abs(boxCenterZ - targetZ) > 1.5) {
            const double halfW = std::clamp((maxX - minX) * 0.5, 0.25, 0.45);
            const double halfD = std::clamp((maxZ - minZ) * 0.5, 0.25, 0.45);
            minX = targetX - halfW;
            maxX = targetX + halfW;
            minZ = targetZ - halfD;
            maxZ = targetZ + halfD;
        }
        // On 1.7.10 a remote player's box floor is not its feet.
        //
        // The server sends feet coordinates, EntityOtherPlayerMP stores them straight into
        // posY, but setPosition still builds the box as posY - yOffset. So posY is the
        // feet and minY sits a full 1.62 below the body. Aiming at minY + headRatio then
        // lands on the real feet, which is exactly the symptom this module had.
        //
        // Proven rather than assumed: with the local eye at posY, local feet and the
        // target's posY read the same floor height, while minY read 1.62 lower. Rebase the
        // box on posY, keeping the height the AABB reported so a sneaking or otherwise
        // resized target still scales.
        if (posYFeetDelta_ > 0.0) {
            const double reportedHeight = maxY - minY;
            minY = targetY;
            maxY = targetY + reportedHeight;
        }

        const double targetFeetY = minY;
        if (std::abs(minY - targetFeetY) > 1.5 || maxY < targetFeetY - 0.25) {
            const double height = maxY - minY;
            minY = targetFeetY;
            maxY = targetFeetY + std::clamp(height, 1.5, 2.0);
        }
    } else {
        // No AABB arrived, so assume the vanilla player box around the feet position.
        // posY is the feet for a remote player on both versions (see the box note above).
        constexpr double kHalfWidth = 0.3;
        const double targetFeetY = targetY;
        minX = targetX - kHalfWidth;
        maxX = targetX + kHalfWidth;
        minY = targetFeetY;
        maxY = targetFeetY + 1.8;
        minZ = targetZ - kHalfWidth;
        maxZ = targetZ + kHalfWidth;
    }

    // Publish the repaired box. Whatever offsets the aim after this point has to
    // stay inside it, and the repair above is the only place that knows which of
    // the raw AABB and the rebuilt one actually got used.
    resolvedBox_ = {minX, minY, minZ, maxX, maxY, maxZ};

    const double height = maxY - minY;

    // Anything but 1.7.10 takes one fixed ratio off the live box. No anchor, no head/chest
    // ease, no jump offset: those exist to repair a box that arrives a yOffset too low, and
    // on 1.8.9 minY really is the feet, so a plain ratio lands where it says it does. Holding
    // a single point also keeps the camera from riding the target's vertical motion.
    if (!usesJumpAimLogic()) {
        aimY = minY + height * kAimRatio1_8_9;
        resolveAimHorizontal(minX, maxX, minZ, maxZ, localX, localZ, aimX, aimZ);
        return;
    }

    // Trim knob for 1.7.10, added at the very end so the hitbox clamp cannot swallow it.
    const double versionBias = kAimHeightBias1_7_10;

    // Keeps the aim on the body. The top margin has to leave room for the head point
    // itself: kHeadRatio puts it 0.216 below the box top on a standing player, so the old
    // 0.25 clamped every grounded shot down to 1.55 and the aim never reached the eye line.
    const double maxAimY = maxY - std::min(0.15, height * 0.15);

    // Grounded: track the head off the live box, so vertical aim follows the target
    // around the map instead of sliding along its torso.
    const double headY = std::min(minY + height * kHeadRatio, maxAimY);

    // While the target is on the ground its feet are the reference a jump is measured
    // from. Refreshing it only while grounded is what lets the aim follow a jump
    // partially: mid-air the reference stays behind the rising box by exactly the
    // displacement of the jump, and kJumpFollow decides how much of that to give back.
    if (!targetAirborne_ || !hasGroundAnchor_) {
        groundFeetY_ = minY;
        hasGroundAnchor_ = true;
    }
    // Two invariants on the anchor, both of which the live trace caught being violated.
    //
    // It cannot sit above the feet: it means "the ground this target jumped from", and a
    // jump only goes up. Remote positions arrive at ~20 Hz, so on a laggy connection a gap
    // between packets outlasts the settle window, the target reads as landed in mid-air and
    // the anchor latches onto the apex. The trace showed that as a negative lag, after
    // which the aim height wandered for the rest of the jump. Clamping here also lets a
    // target that really did walk uphill pull the anchor along immediately.
    //
    // And it cannot fall further behind than a jump can explain, which is what makes
    // aiming at the feet impossible even when the airborne reading is wrong.
    groundFeetY_ = std::clamp(groundFeetY_, minY - kMaxAnchorLag, minY);

    const double referenceFeetY = std::lerp(groundFeetY_, minY, kJumpFollow);

    // The result still has to be a point on the target, so it is clamped into the live
    // box with room to spare -- a rotation that leaves the box is one the server
    // raytraces into empty air.
    const double inset = std::min(0.25, height * 0.25);
    const double airborneY = std::clamp(referenceFeetY + height * kChestRatio, minY + inset, maxAimY);

    aimY = std::lerp(headY, airborneY, (double)aimHeightBlend_) + versionBias;
    resolveAimHorizontal(minX, maxX, minZ, maxZ, localX, localZ, aimX, aimZ);
}

void AimAssist::resolveAimHorizontal(double minX, double maxX, double minZ, double maxZ, double localX, double localZ,
                                     double& aimX, double& aimZ) {
    if (settings.aimAtClosestPoint) {
        aimX = std::clamp(localX, minX, maxX);
        aimZ = std::clamp(localZ, minZ, maxZ);
        // Point-blank: if we're horizontally inside the hitbox the clamp collapses
        // the aim point onto our own position and the yaw becomes undefined
        // (atan2(0,0)), so the camera stops turning. Fall back to the hitbox centre
        // so it keeps tracking at very close range.
        const double hdx = aimX - localX, hdz = aimZ - localZ;
        if (hdx * hdx + hdz * hdz >= 0.02)
            return;
    }

    aimX = (minX + maxX) * 0.5;
    aimZ = (minZ + maxZ) * 0.5;
}

void AimAssist::measurePosYFeetDelta(JNIEnv* env) {
    if (posYFeetDeltaKnown_ || !env || !player.isValid())
        return;

    // 1.7.10 is the only version with Entity.yOffset, so it is the only one that can
    // need this. Settling the answer to zero on a *known* other version keeps 1.8.9 on
    // exactly the arithmetic it already had rather than at the mercy of a measurement.
    //
    // An undetected version falls through to the measurement instead of to zero. Version
    // detection can fail (java.cpp resorts to a BlockPos class probe and may still give
    // up), and returning zero there would silently disable the correction on a 1.7.10
    // client and bring the whole bug back with no code change. The measurement is
    // self-validating -- it can only report 1.62 on a client whose posY really is
    // eye-space -- so it is the safe default when the version is unknown.
    if (Java::Version != MinecraftVersion::MC_1_7_10 && Java::Version != MinecraftVersion::UNDEFINED) {
        posYFeetDelta_ = 0.0;
        posYFeetDeltaKnown_ = true;
        return;
    }

    if (!JniCache::fid_boundingBox || !JniCache::fid_aabb_minY)
        return;

    jobject aabb = env->GetObjectField(player.instance, JniCache::fid_boundingBox);
    if (!aabb) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }
    const double boxMinY = env->GetDoubleField(aabb, JniCache::fid_aabb_minY);
    env->DeleteLocalRef(aabb);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    // 1.7.10 builds the box as posY - yOffset + ySize, and ySize only decays to zero a
    // few ticks after stepping up. Measuring mid-step would read an offset between the
    // two conventions, so wait for a sample that is unambiguously one or the other.
    const double delta = player.getPosY() - boxMinY;
    if (delta > 0.2 && delta < 1.4)
        return;

    posYFeetDelta_ = delta >= 1.4 ? kEyeHeight : 0.0;
    posYFeetDeltaKnown_ = true;
    log_ok("[AimAssist] Entity.posY is %s-space (yOffset %.2f, measured %.3f)", posYFeetDelta_ > 0.0 ? "eye" : "feet",
           posYFeetDelta_, delta);
}

bool AimAssist::resolveTargetAirborne(JNIEnv* env, double targetFeetY) {
    resolveAimOnGround(env);

    targetOnGroundRaw_ = -1;
    if (g_aim_fid_onGround && cachedTarget) {
        targetOnGroundRaw_ = env->GetBooleanField(cachedTarget, g_aim_fid_onGround) != JNI_FALSE ? 1 : 0;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            targetOnGroundRaw_ = -1;
        }
    }

    if (Java::Version != MinecraftVersion::MC_1_7_10) {
        // Unknown grounded state means "aim at the head", which is the behaviour this
        // module had before the jump handling existed.
        return targetOnGroundRaw_ == 0;
    }

    if (hasLastFeetY_ && std::abs(targetFeetY - lastFeetY_) > kFeetMoveEpsilon)
        feetMovingTimer_.Start();
    const bool airborne = hasLastFeetY_ && feetMovingTimer_.Elapsed<>() < kFeetSettleMs;
    lastFeetY_ = targetFeetY;
    hasLastFeetY_ = true;
    return airborne;
}

void AimAssist::updateMotionDamping(JNIEnv* env, double targetFeetY) {
    // Another player's anchor is meaningless, and so is a blend measured against them.
    if (targetChangedThisFrame) {
        hasGroundAnchor_ = false;
        hasLastFeetY_ = false;
        aimHeightBlend_ = 0.0f;
        targetAirBlend_ = 0.0f;
    }

    targetAirborne_ = resolveTargetAirborne(env, targetFeetY);
    easeAlpha_ = 1.0f - std::exp(-kAimHeightEaseRate * stepSeconds(aimHeightTimer_));

    // The local flag is maintained by this client's own physics and is reliable; the remote one
    // is whatever resolveTargetAirborne could establish for this version.
    const bool localAirborne = !player.isOnGround();
    localAirBlend_ += ((localAirborne ? 1.0f : 0.0f) - localAirBlend_) * easeAlpha_;
    localAirBlend_ = std::clamp(localAirBlend_, 0.0f, 1.0f);
    targetAirBlend_ += ((targetAirborne_ ? 1.0f : 0.0f) - targetAirBlend_) * easeAlpha_;
    targetAirBlend_ = std::clamp(targetAirBlend_, 0.0f, 1.0f);

    // Whichever of the two is airborne more sets the softening.
    motionDamping_ = std::lerp(1.0f, kMotionDamping, std::max(targetAirBlend_, localAirBlend_));
}

void AimAssist::updateAimHeightBlend(double targetFeetY) {
    // How far the target has climbed off the ground it left, not merely whether it is off it.
    // groundFeetY_ is last update's anchor, which at ~500 Hz is close enough and is already
    // clamped to sit at or below the feet, so this cannot go negative.
    const double separation = hasGroundAnchor_ ? std::max(0.0, targetFeetY - groundFeetY_) : 0.0;
    const float bySeparation = (float)std::clamp(separation / kFullAirSeparation, 0.0, 1.0);
    const float targetGoal = targetAirborne_ ? std::max(kTakeoffAirFloor, bySeparation) : 0.0f;

    // A jumping local player moves the camera rather than the target, so it is worth only a
    // fraction of the height shift; the damping is what handles that case.
    const float goal = std::max(targetGoal, localAirBlend_ * kLocalAirHeightShare);
    float next = aimHeightBlend_ + (goal - aimHeightBlend_) * easeAlpha_;

    // Hold the peak for the rest of the jump. Easing back toward the head while the target is
    // still falling would walk the aim up just as the head drops past it, and that downstroke
    // is the second half of the nod. Only landing releases it.
    if ((targetAirborne_ || !player.isOnGround()) && next < aimHeightBlend_)
        next = aimHeightBlend_;

    aimHeightBlend_ = std::clamp(next, 0.0f, 1.0f);
}

void AimAssist::onRenderMenu() {
    static const char* targetModes[] = {"Lowest Health", "Closest to Player", "Closest to Crosshair", "Switch"};
    static const char* sortByModes[] = {"Distance", "Health", "FOV", "Hurt Time"};
    int currentMode = static_cast<int>(settings.targetMode);
    int currentSortBy = static_cast<int>(settings.switchSortBy);

    bool enabledValue = enabled;
    ui::two_column(
        "Settings",
        [this, &currentMode, &currentSortBy, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();
            static const char* aimModes[] = {"Smooth", "Adaptive", "Blatant"};
            int am = static_cast<int>(settings.aimMode);
            if (ui::Combo("Mode", &am, aimModes, IM_ARRAYSIZE(aimModes)))
                settings.aimMode = static_cast<AimMode>(am);
            if (settings.aimMode == AimMode::ADAPTIVE)
                ImGui::TextDisabled("Speeds scale with the angle to the target");
            else if (settings.aimMode == AimMode::BLATANT)
                ImGui::TextDisabled("Snaps onto the target - very obvious");

            ui::SliderFloat("Horizontal Speed", &settings.horizontalSpeed, 1.0f, 100.0f, "%.0f", true);
            // Hidden rather than disabled while Horizontal Only is on: the switch lives in
            // the Options column, so a greyed slider here would be a control with no
            // visible cause.
            if (!settings.horizontalOnly)
                ui::SliderFloat("Vertical Speed", &settings.verticalSpeed, 0.0f, 100.0f, "%.0f", true);
            ui::SliderInt("FOV", &settings.fov, 10, 360, "%d");
            ui::SliderFloat("Distance", &settings.distance, 3.0f, 8.0f, "%.1f");
            ui::SliderFloat("Prediction", &settings.prediction, 0.0f, 100.0f, "%.0f%%");
            ui::SliderFloat("Jitter", &settings.jitterPercent, 0.0f, 100.0f, "%.0f%%");
            ui::Checkbox("Smooth Camera", &settings.packetRotation);
            if (settings.packetRotation)
                ImGui::TextDisabled("Jitter only on sent packet (1st person smooth)");
        },
        "Options",
        [this, &currentMode, &currentSortBy]() {
            ui::Checkbox("Horizontal Only", &settings.horizontalOnly);
            ui::Checkbox("Always Aim", &settings.alwaysAim);
            ui::Checkbox("Break Blocks", &settings.breakBlocks);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
            ui::Checkbox("Aim Closest Point", &settings.aimAtClosestPoint);
            ui::Checkbox("Target Invisibles", &settings.invisibles);
            ui::Checkbox("Stop On Hitbox", &settings.stopOnHitbox);
            ui::Checkbox("Lock In", &settings.lockIn);
            if (settings.lockIn)
                ImGui::TextDisabled("Holds one target until it dies or escapes");
            ImGui::Spacing();
            if (ui::Combo("Target Mode", &currentMode, targetModes, IM_ARRAYSIZE(targetModes))) {
                settings.targetMode = static_cast<TargetMode>(currentMode);
            }
            // Show Sort By option only when Switch mode is selected
            if (settings.targetMode == TargetMode::SWITCH) {
                if (ui::Combo("Sort By", &currentSortBy, sortByModes, IM_ARRAYSIZE(sortByModes))) {
                    settings.switchSortBy = static_cast<SortBy>(currentSortBy);
                }
                // Show Lock option only when Hurt Time is selected
                if (settings.switchSortBy == SortBy::HURT_TIME) {
                    ui::Checkbox("Lock", &settings.hurtTimeLock);
                    if (settings.hurtTimeLock) {
                        ImGui::TextDisabled("Instant aim on target switch");
                    } else {
                        ImGui::TextDisabled("Targets least recently hit player");
                    }
                }
            }
            ImGui::Spacing();
            // Armor Filter (multi-select): check every tier you want to target.
            // A player is aimed at if they wear ANY of the checked tiers.
            ImGui::Text("Armor Filter (target tiers):");
            auto tierCheckbox = [this](const char* label, int bit) {
                bool on = (settings.armorTierMask & bit) != 0;
                if (ui::Checkbox(label, &on)) {
                    if (on)
                        settings.armorTierMask |= bit;
                    else
                        settings.armorTierMask &= ~bit;
                }
            };
            tierCheckbox("No Armor", ARMOR_NO_ARMOR);
            tierCheckbox("Leather", ARMOR_LEATHER);
            tierCheckbox("Chainmail", ARMOR_CHAIN);
            tierCheckbox("Iron", ARMOR_IRON);
            tierCheckbox("Gold", ARMOR_GOLD);
            tierCheckbox("Diamond", ARMOR_DIAMOND);
            if (settings.armorTierMask == 0) {
                ImGui::TextDisabled("No tiers checked - targets everyone");
            } else {
                ImGui::TextDisabled("Targets players matching any checked tier");

                // Optional key to toggle the armor filter on/off in-game.
                ui::Binder("Armor Filter Key", &settings.armorFilterKeybind);
                if (settings.armorFilterKeybind != 0)
                    ImGui::TextDisabled("Filter is currently %s", settings.armorFilterEnabled ? "ON" : "OFF");
            }
        });
}

std::string AimAssist::getDisplayInfo() {
    static const char* names[] = {"Smooth", "Adaptive", "Blatant"};
    return names[std::clamp(static_cast<int>(settings.aimMode), 0, 2)];
}

void AimAssist::saveSettings(json::Value& node) const {
    node.remove("speed");
    node.remove("reactionTime");
    node.remove("aimBone");
    node.remove("debugOverlay");

    node["aimMode"] = static_cast<int>(settings.aimMode);
    node["lockIn"] = settings.lockIn;
    node["horizontalSpeed"] = settings.horizontalSpeed;
    node["verticalSpeed"] = settings.verticalSpeed;
    node["horizontalOnly"] = settings.horizontalOnly;
    node["fov"] = settings.fov;
    node["distance"] = settings.distance;
    node["targetMode"] = static_cast<int>(settings.targetMode);
    node["switchSortBy"] = static_cast<int>(settings.switchSortBy);
    node["hurtTimeLock"] = settings.hurtTimeLock;
    node["breakBlocks"] = settings.breakBlocks;
    node["weaponOnly"] = settings.weaponOnly;
    node["aimAtClosestPoint"] = settings.aimAtClosestPoint;
    node["alwaysAim"] = settings.alwaysAim;
    node["invisibles"] = settings.invisibles;
    node["stopOnHitbox"] = settings.stopOnHitbox;
    node["jitterPercent"] = settings.jitterPercent;
    node["prediction"] = settings.prediction;
    node["packetRotation"] = settings.packetRotation;
    node["armorFilter"] = static_cast<int>(settings.armorFilter);
    node["armorTierMask"] = settings.armorTierMask;
    node["armorFilterEnabled"] = settings.armorFilterEnabled;
    node["armorFilterKeybind"] = settings.armorFilterKeybind;
}

void AimAssist::loadSettings(const json::Value& node) {
    // The single "speed" slider became one per axis; old configs fall back to it.
    const float legacySpeed = std::clamp(node["speed"].as_float(settings.horizontalSpeed), 1.0f, 100.0f);
    settings.horizontalSpeed = std::clamp(node["horizontalSpeed"].as_float(legacySpeed), 1.0f, 100.0f);
    settings.verticalSpeed = std::clamp(node["verticalSpeed"].as_float(legacySpeed), 0.0f, 100.0f);
    settings.horizontalOnly = node["horizontalOnly"].as_bool(settings.horizontalOnly);
    const int mode = node["aimMode"].as_int(static_cast<int>(settings.aimMode));
    settings.aimMode = static_cast<AimMode>(std::clamp(mode, 0, 2));
    // "entityLock"/"lockAim" were the old name for this switch.
    const bool legacyLock = node.has("entityLock") ? node["entityLock"].as_bool(false) : node["lockAim"].as_bool(false);
    settings.lockIn = node["lockIn"].as_bool(legacyLock);
    settings.fov = node["fov"].as_int(settings.fov);
    settings.distance = node["distance"].as_float(settings.distance);
    settings.targetMode = static_cast<TargetMode>(node["targetMode"].as_int(static_cast<int>(settings.targetMode)));
    settings.switchSortBy = static_cast<SortBy>(node["switchSortBy"].as_int(static_cast<int>(settings.switchSortBy)));
    settings.hurtTimeLock = node["hurtTimeLock"].as_bool(settings.hurtTimeLock);
    settings.breakBlocks = node["breakBlocks"].as_bool(settings.breakBlocks);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
    settings.aimAtClosestPoint = node["aimAtClosestPoint"].as_bool(settings.aimAtClosestPoint);
    settings.alwaysAim = node["alwaysAim"].as_bool(settings.alwaysAim);
    settings.invisibles = node["invisibles"].as_bool(settings.invisibles);
    settings.stopOnHitbox = node["stopOnHitbox"].as_bool(settings.stopOnHitbox);
    settings.jitterPercent = (std::clamp)(node["jitterPercent"].as_float(settings.jitterPercent), 0.0f, 100.0f);
    settings.prediction = (std::clamp)(node["prediction"].as_float(settings.prediction), 0.0f, 100.0f);
    settings.packetRotation = node["packetRotation"].as_bool(settings.packetRotation);
    // "antiBot" is still present in older configs and is intentionally ignored: the AntiBot
    // module owns that state now and carries its own node.
    settings.armorFilter = static_cast<ArmorFilter>(node["armorFilter"].as_int(static_cast<int>(settings.armorFilter)));
    if (node.has("armorTierMask")) {
        settings.armorTierMask = node["armorTierMask"].as_int(settings.armorTierMask);
    } else {
        // Migrate legacy single-select enum into the new multi-select bitmask.
        switch (settings.armorFilter) {
            case ArmorFilter::NONE:
                settings.armorTierMask = ARMOR_NO_ARMOR;
                break;
            case ArmorFilter::LEATHER:
                settings.armorTierMask = ARMOR_LEATHER;
                break;
            case ArmorFilter::CHAINMAIL:
                settings.armorTierMask = ARMOR_CHAIN;
                break;
            case ArmorFilter::IRON:
                settings.armorTierMask = ARMOR_IRON;
                break;
            case ArmorFilter::GOLD:
                settings.armorTierMask = ARMOR_GOLD;
                break;
            case ArmorFilter::DIAMOND:
                settings.armorTierMask = ARMOR_DIAMOND;
                break;
            default:
                settings.armorTierMask = 0;
                break;
        }
    }
    settings.armorFilterEnabled = node["armorFilterEnabled"].as_bool(settings.armorFilterEnabled);
    settings.armorFilterKeybind = node["armorFilterKeybind"].as_int(settings.armorFilterKeybind);
}
