#include "killaura.h"
#include "antibot.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

KillAura* killAura = nullptr;

namespace {
constexpr double kPi = 3.14159265358979323846;

// C03PacketPlayer (flying) resolution — private to KillAura's silent rotation.
jclass ka_C03 = nullptr;
jfieldID ka_yaw = nullptr, ka_pitch = nullptr, ka_rotating = nullptr;
bool ka_resolved = false;

void resolveC03(JNIEnv* env) {
    if (ka_resolved)
        return;
    ka_resolved = true;
    jclass local = nullptr;
    if (Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", local)) {
        ka_C03 = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        ka_yaw = env->GetFieldID(ka_C03, "yaw", "F");
        if (!ka_yaw) {
            env->ExceptionClear();
            ka_yaw = env->GetFieldID(ka_C03, "field_149476_e", "F");
        }
        ka_pitch = env->GetFieldID(ka_C03, "pitch", "F");
        if (!ka_pitch) {
            env->ExceptionClear();
            ka_pitch = env->GetFieldID(ka_C03, "field_149473_f", "F");
        }
        ka_rotating = env->GetFieldID(ka_C03, "rotating", "Z");
        if (!ka_rotating) {
            env->ExceptionClear();
            ka_rotating = env->GetFieldID(ka_C03, "field_149480_h", "Z");
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}
}  // namespace

KillAura::KillAura() : Module("KillAura") {
    keybind = 0;
    std::random_device rd;
    rng_.seed(rd());
    rollNextDelay();
}

std::uint64_t KillAura::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

float KillAura::wrapDegrees(float a) {
    a = std::fmod(a, 360.0f);
    if (a >= 180.0f)
        a -= 360.0f;
    if (a < -180.0f)
        a += 360.0f;
    return a;
}

double KillAura::unit() {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng_);
}

void KillAura::rollNextDelay() {
    int lo = std::clamp(std::min(settings.minCPS, settings.maxCPS), 1, 20);
    int hi = std::clamp(std::max(settings.minCPS, settings.maxCPS), 1, 20);
    std::uniform_int_distribution<int> d(lo, hi);
    const int cps = d(rng_);
    float base = 1000.0f / static_cast<float>(std::max(1, cps));
    if (!settings.clickRandomization) {
        nextDelayMs_ = base;
        return;
    }
    // Humanized (log-normal) jitter so the click spacing isn't a flat histogram.
    const double u1 = std::max(1e-9, unit());
    const double u2 = unit();
    const double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    const double sigma = 0.12;
    base *= static_cast<float>(std::exp(sigma * z - 0.5 * sigma * sigma));
    const float floorMs = 1000.0f / static_cast<float>(hi) * 0.6f;
    const float ceilMs = 1000.0f / static_cast<float>(lo) * 1.5f;
    nextDelayMs_ = std::clamp(base, floorMs, ceilMs);
}

KillAura::~KillAura() {
    if (Java::Initialized) {
        JNIEnv* env = Java::Env();
        if (env)
            clearTarget(env);
    }
}

void KillAura::clearTarget(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(targetMutex_);
    if (env) {
        if (targetInstance_) {
            env->DeleteGlobalRef(targetInstance_);
            targetInstance_ = nullptr;
        }
        for (jobject g : multiInstances_)
            if (g)
                env->DeleteGlobalRef(g);
    }
    multiInstances_.clear();
    haveTarget_ = false;
}

void KillAura::onEnable() {
    haveCur_ = false;
    hasAim_.store(false, std::memory_order_release);
    targetUuid_ = "";
    switchIndex_ = 0;
    lastAttackMs_ = 0;
    rotationSentTicks_ = 0;
    haveWritten_ = false;
    cameraOwned_ = false;
}

void KillAura::standDown() {
    hasAim_.store(false, std::memory_order_release);
    haveCur_ = false;
    rotationSentTicks_ = 0;
    // The module loop hits this ~500x/second while a GUI is open. Once idle there is
    // nothing left to release, so stop before touching JNI again.
    if (!haveTarget_ && !cameraOwned_)
        return;
    if (JNIEnv* env = Java::Env())
        clearTarget(env);
    if (player.isValid()) {
        syncRealRotation();
        releaseCamera();
    } else {
        // No player to read or write — forget the tracking instead of feeding it garbage.
        cameraOwned_ = false;
        haveWritten_ = false;
    }
}

void KillAura::onDisable() {
    targetUuid_ = "";
    standDown();
}

bool KillAura::isHoldingWeapon() {
    int id = player.getHeldItemId();
    return (id == 268 || id == 272 || id == 267 || id == 276 || id == 283 ||  // swords
            id == 271 || id == 275 || id == 258 || id == 279 || id == 286);    // axes
}

bool KillAura::hasLineOfSight(double ex, double ey, double ez, double tx, double ty, double tz) const {
    const double dx = tx - ex, dy = ty - ey, dz = tz - ez;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 0.1)
        return true;
    const int steps = static_cast<int>(dist / 0.4) + 1;
    for (int i = 1; i < steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const int bx = static_cast<int>(std::floor(ex + dx * t));
        const int by = static_cast<int>(std::floor(ey + dy * t));
        const int bz = static_cast<int>(std::floor(ez + dz * t));
        if (mc.getBlockId(bx, by, bz) != 0)
            return false;
    }
    return true;
}

void KillAura::syncRealRotation() {
    const float gameYaw = player.getYaw();
    const float gamePitch = player.getPitch();
    if (!haveWritten_) {
        realYaw_ = gameYaw;
        realPitch_ = gamePitch;
    } else {
        // Everything the client reports beyond our own last write is mouse input, whether
        // or not the camera got restored in between — which is exactly why this is a delta
        // and not a fresh read.
        realYaw_ += gameYaw - lastWrittenYaw_;
        realPitch_ = std::clamp(realPitch_ + (gamePitch - lastWrittenPitch_), -90.0f, 90.0f);
    }
    // Consume the delta. The module loop syncs on every idle iteration, so leaving the old
    // baseline in place would count the same mouse movement over and over.
    lastWrittenYaw_ = gameYaw;
    lastWrittenPitch_ = gamePitch;
    haveWritten_ = true;
}

void KillAura::releaseCamera() {
    if (!cameraOwned_)
        return;
    cameraOwned_ = false;
    if (!settings.silent || !player.isValid())
        return;
    player.setYaw(realYaw_);
    player.setPitch(realPitch_);
    lastWrittenYaw_ = realYaw_;
    lastWrittenPitch_ = realPitch_;
    haveWritten_ = true;
}

void KillAura::applyMoveFix(JNIEnv* env, jobject playerInstance, float diffDeg) {
    if (!moveFieldsResolved_) {
        moveFieldsResolved_ = true;
        jclass pcls = env->GetObjectClass(playerInstance);
        if (pcls) {
            const auto findField = [env, pcls](const char* name, const char* srg) -> jfieldID {
                jfieldID f = env->GetFieldID(pcls, name, "F");
                if (!f) {
                    env->ExceptionClear();
                    f = env->GetFieldID(pcls, srg, "F");
                }
                env->ExceptionClear();
                return f;
            };
            entStrafe_ = findField("moveStrafing", "field_70702_br");
            entForward_ = findField("moveForward", "field_70701_bs");
            env->DeleteLocalRef(pcls);
        }
        moveFieldsOk_ = entStrafe_ && entForward_;
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!moveFieldsOk_)
        return;

    const float strafe0 = env->GetFloatField(playerInstance, entStrafe_);
    const float forward0 = env->GetFloatField(playerInstance, entForward_);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    if (strafe0 == 0.0f && forward0 == 0.0f)
        return;

    // EntityLivingBase.moveFlying builds the world motion as
    //     motion = Rot(rotationYaw) * (strafe, forward)
    // and rotationYaw now carries the aura yaw, so to keep the motion pointing
    // where the real (mouse) yaw points we pre-rotate the input by the difference:
    //     (strafe', forward') = Rot(realYaw - auraYaw) * (strafe, forward)
    const float d = diffDeg * static_cast<float>(kPi) / 180.0f;
    const float cs = std::cos(d), sn = std::sin(d);
    const float idealStrafe = strafe0 * cs - forward0 * sn;
    const float idealForward = forward0 * cs + strafe0 * sn;

    // ...then snap that back onto the grid of inputs the client can actually
    // produce. Vanilla only ever feeds these fields {-1, 0, +1} * scale, so the
    // exact rotated vector is a value no combination of keypresses can generate:
    // a simulation anticheat replays the legal combinations against the yaw we
    // sent, finds none that reproduces our motion, and flags every tick we
    // strafe. The closest legal combination costs at most 22.5 deg of heading
    // and leaves both field values indistinguishable from a real keypress.
    const float scale = std::max(std::abs(strafe0), std::abs(forward0));
    if (scale <= 0.0001f)
        return;

    // Cardinal and diagonal inputs differ in magnitude, and moveFlying only
    // renormalises once the magnitude reaches 1 — so at full walking speed all
    // eight directions travel at exactly the same speed and are interchangeable.
    // Below that (sneaking, item use, NoSlowDown) speed IS the magnitude, so the
    // replacement has to keep the same number of pressed axes or the player
    // visibly changes speed.
    const bool preserveAxisCount = scale < 0.999f;
    const int axes0 = (strafe0 != 0.0f ? 1 : 0) + (forward0 != 0.0f ? 1 : 0);

    float bestStrafe = strafe0, bestForward = forward0, bestDot = -1e9f;
    for (int s = -1; s <= 1; ++s) {
        for (int f = -1; f <= 1; ++f) {
            if (s == 0 && f == 0)
                continue;
            if (preserveAxisCount && ((s != 0 ? 1 : 0) + (f != 0 ? 1 : 0)) != axes0)
                continue;
            const float candS = static_cast<float>(s), candF = static_cast<float>(f);
            const float dot =
                (candS * idealStrafe + candF * idealForward) / std::sqrt(candS * candS + candF * candF);
            if (dot > bestDot) {
                bestDot = dot;
                bestStrafe = candS * scale;
                bestForward = candF * scale;
            }
        }
    }

    env->SetFloatField(playerInstance, entStrafe_, bestStrafe);
    env->SetFloatField(playerInstance, entForward_, bestForward);
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void KillAura::onPreMotion(JNIEnv* env, jobject playerInstance) {
    if (!enabled || !env || !playerInstance)
        return;

    // Order matters: read the user's rotation back BEFORE touching the camera, then hand
    // the camera back if last tick's flying packet never came (dropped by FakeLag/Blink,
    // or rotation owned by Clutch/ThrowPots). Doing both unconditionally is what keeps a
    // missed restore a one-tick cosmetic hiccup instead of a permanent drift.
    syncRealRotation();
    releaseCamera();

    // Snapshot the target under the lock, taking LOCAL refs so we can release it
    // before calling into the game (attackEntity re-enters other gates).
    jobject localTarget = nullptr;
    std::vector<jobject> localMulti;
    float aimYaw, aimPitch, dist;
    int hurtTime;
    {
        std::lock_guard<std::mutex> lock(targetMutex_);
        if (!haveTarget_) {
            hasAim_.store(false, std::memory_order_release);
            haveCur_ = false;
            rotationSentTicks_ = 0;
            return;
        }
        aimYaw = aimYaw_;
        aimPitch = aimPitch_;
        dist = targetDist_;
        hurtTime = targetHurtTime_;
        if (targetInstance_)
            localTarget = env->NewLocalRef(targetInstance_);
        if (settings.switchMode == SwitchMode::Multi)
            for (jobject g : multiInstances_)
                if (g)
                    localMulti.push_back(env->NewLocalRef(g));
    }

    const std::uint64_t now = nowMs();
    const bool attackWindow = (now - lastAttackMs_) >= static_cast<std::uint64_t>(nextDelayMs_);
    // rotateOnHitOnly: only spin up the aim ~2 ticks before the click is due.
    const bool doRotate =
        !settings.rotateOnHitOnly || (now - lastAttackMs_) >= static_cast<std::uint64_t>(std::max(0.0f, nextDelayMs_ - 100.0f));

    if (!doRotate) {
        hasAim_.store(false, std::memory_order_release);
        haveCur_ = false;
        rotationSentTicks_ = 0;
        if (localTarget)
            env->DeleteLocalRef(localTarget);
        for (jobject t : localMulti)
            env->DeleteLocalRef(t);
        return;
    }

    // The real (mouse-driven) rotation for this tick is already in realYaw_/realPitch_,
    // tracked by delta in syncRealRotation() rather than re-read here.

    // Step the aura rotation toward the aim (once per tick), tracked independently of
    // the real rotation.
    if (!haveCur_) {
        curYaw_ = realYaw_;
        curPitch_ = realPitch_;
        haveCur_ = true;
    }
    float speed = settings.rotationSpeed;
    if (settings.randomization)
        speed *= 0.75f + static_cast<float>(unit()) * 0.5f;
    speed = std::clamp(speed, 1.0f, 180.0f);

    const float dYaw = wrapDegrees(aimYaw - curYaw_);
    const float dPitch = wrapDegrees(aimPitch - curPitch_);
    float newYaw = curYaw_ + std::clamp(dYaw, -speed, speed);
    float newPitch = curPitch_ + std::clamp(dPitch, -speed * 0.7f, speed * 0.7f);

    if (settings.randomization) {
        // Quantize onto the client's mouse GCD grid (AimModulo360-safe).
        const float sens = mc.getMouseSensitivity();
        const float gf = sens * 0.6f + 0.2f;
        const float gcd = gf * gf * gf * 1.2f;
        if (gcd > 0.0001f) {
            newYaw = curYaw_ + std::round(wrapDegrees(newYaw - curYaw_) / gcd) * gcd;
            newPitch = curPitch_ + std::round(wrapDegrees(newPitch - curPitch_) / gcd) * gcd;
            if (settings.jitter > 0.01f) {
                jitterToggle_ = !jitterToggle_;
                const float jAmt = std::round((settings.jitter * static_cast<float>(unit())) / gcd) * gcd;
                newYaw += jitterToggle_ ? jAmt : -jAmt;
            }
        }
    }
    curYaw_ = newYaw;
    curPitch_ = std::clamp(newPitch, -90.0f, 90.0f);

    // Apply the aura rotation NOW — before the game moves the player this tick — so the
    // movement the game (and Grim) simulate uses it and the flying packet carries it.
    player.setYaw(curYaw_);
    player.setPitch(curPitch_);
    lastWrittenYaw_ = curYaw_;
    lastWrittenPitch_ = curPitch_;
    haveWritten_ = true;
    cameraOwned_ = true;
    // MoveFix: rotate the movement input so the world movement stays in the real look
    // direction. Without it, Grim's simulation sees movement that can't come from a
    // valid input relative to the sent (aura) yaw → Simulation flag while moving.
    // MoveFix is meaningful for silent rotations. With Silent disabled the
    // camera already follows the aura yaw, so rotating input again would make
    // WASD feel offset and was the source of the broken visible-mode behavior.
    if (settings.moveFix && settings.silent)
        applyMoveFix(env, playerInstance, wrapDegrees(realYaw_ - curYaw_));
    hasAim_.store(true, std::memory_order_release);

    // Attack pre-motion (before the flying packet → clean packet order). Needs a prior
    // flying packet to have carried this rotation so the server validates the hit.
    const bool aimed =
        std::abs(wrapDegrees(aimYaw - curYaw_)) <= 6.0f && std::abs(wrapDegrees(aimPitch - curPitch_)) <= 6.0f;
    const bool inRange = dist <= settings.reach;
    const bool hurtOk = hurtTime <= settings.maxHurtTime;
    const bool chanceOk = settings.hitChance >= 100 || (unit() * 100.0 < static_cast<double>(settings.hitChance));
    if (attackWindow && aimed && inRange && hurtOk && chanceOk && rotationSentTicks_ >= 1 && localTarget) {
        bool did = mc.attackEntity(localTarget);
        if (settings.switchMode == SwitchMode::Multi)
            for (jobject t : localMulti)
                if (t)
                    mc.attackEntity(t);
        if (did) {
            lastAttackMs_ = now;
            rollNextDelay();
            if (settings.switchMode == SwitchMode::Switch)
                ++switchIndex_;
        }
    }

    if (localTarget)
        env->DeleteLocalRef(localTarget);
    for (jobject t : localMulti)
        env->DeleteLocalRef(t);
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void KillAura::onOutgoingPacket(JNIEnv* env, jobject packet) {
    // onPreMotion already computed + applied the rotation. Here we only pin the flying
    // packet's yaw/pitch to it (so nothing else perturbs the sent rotation) and count
    // that a flying packet carried the aim (gates the pre-motion attack).
    if (!enabled || !env || !packet || !hasAim_.load(std::memory_order_acquire))
        return;
    resolveC03(env);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (!ka_C03 || env->IsInstanceOf(packet, ka_C03) != JNI_TRUE)
        return;
    if (ka_yaw)
        env->SetFloatField(packet, ka_yaw, curYaw_);
    if (ka_pitch)
        env->SetFloatField(packet, ka_pitch, curPitch_);
    if (ka_rotating)
        env->SetBooleanField(packet, ka_rotating, JNI_TRUE);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    ++rotationSentTicks_;

    // Silent: the flying packet (aura yaw) is out and movement already used the aura
    // yaw this tick, so restore the client rotation to the real mouse rotation. The
    // render camera never visibly snaps to the target.
    if (settings.silent) {
        player.setYaw(realYaw_);
        player.setPitch(realPitch_);
        lastWrittenYaw_ = realYaw_;
        lastWrittenPitch_ = realPitch_;
        haveWritten_ = true;
    }
    cameraOwned_ = false;
}

void KillAura::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        // The pre-motion hook keys off haveTarget_ alone, so a target left over from
        // before the GUI opened would keep the aura rotating behind the inventory screen.
        standDown();
        return;
    }
    if (settings.weaponOnly && !isHoldingWeapon()) {
        standDown();
        return;
    }

    JNIEnv* env = Java::Env();
    if (!env)
        return;
    if (env->ExceptionCheck())
        env->ExceptionClear();

    const double eyeX = player.getPosX();
    const double eyeY = player.getPosY() + 1.62;
    const double eyeZ = player.getPosZ();
    const float realYaw = player.getYaw();

    std::vector<PlayerInfo> players = mc.getPlayerList();

    static int localTeamColor = -1;
    static unsigned long lastColorRefresh = 0;
    unsigned long nowTick = GetTickCount();
    if (nowTick - lastColorRefresh > 1000) {
        localTeamColor = mc.getLocalPlayerTeamColor();
        lastColorRefresh = nowTick;
    }
    static std::string localName;
    static unsigned long lastNameRefresh = 0;
    if (localName.empty() || nowTick - lastNameRefresh > 1000) {
        localName = mc.getLocalPlayerName();
        lastNameRefresh = nowTick;
    }

    // Build the candidate list (filtered + within reach & FOV).
    struct Cand {
        int idx;
        double dist;
        double aimX, aimY, aimZ;
        float yaw, pitch;
        float health;
        int hurtTime;
    };
    std::vector<Cand> cands;
    const float halfFov = static_cast<float>(settings.fov) * 0.5f;

    for (int i = 0; i < static_cast<int>(players.size()); ++i) {
        PlayerInfo& p = players[i];
        if (AntiBot::Filtered(p))
            continue;
        if (!settings.invisibles && p.isInvisible)
            continue;

        if (friendsManager) {
            bool friendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool friendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (friendByUUID || friendByName)
                continue;
            if (p.isOnLocalTeam && friendsManager->honorsVanillaTeams())
                continue;
            if (!p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid))
                continue;
            if (!p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid))
                continue;
            ChatColor pc = static_cast<ChatColor>(p.teamColor);
            ChatColor lc = static_cast<ChatColor>(localTeamColor);
            if (pc != ChatColor::WHITE && pc != ChatColor::GRAY && pc != ChatColor::UNKNOWN) {
                if (friendsManager->isScoreboardTeammate(localName, p.profileName.empty() ? p.name : p.profileName))
                    continue;
            }
            if (friendsManager->isTeammate(p.uuid, pc, lc))
                continue;
        }

        // Best hit vec (Augustus-style): clamp the eye onto the slightly-expanded
        // hitbox AABB — the closest reachable point. Aim + reach both use it.
        const double bb = 0.1;  // collision border expand
        const double aimX = std::clamp(eyeX, static_cast<double>(p.hitboxMinX) - bb, static_cast<double>(p.hitboxMaxX) + bb);
        const double aimY = std::clamp(eyeY, static_cast<double>(p.hitboxMinY) - bb, static_cast<double>(p.hitboxMaxY) + bb);
        const double aimZ = std::clamp(eyeZ, static_cast<double>(p.hitboxMinZ) - bb, static_cast<double>(p.hitboxMaxZ) + bb);
        const double rdx = aimX - eyeX, rdy = aimY - eyeY, rdz = aimZ - eyeZ;
        const double reachDist = std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
        if (reachDist > settings.reach)
            continue;

        const double dx = aimX - eyeX, dy = aimY - eyeY, dz = aimZ - eyeZ;
        const double horiz = std::sqrt(dx * dx + dz * dz);
        const float yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
        const float pitch =
            std::clamp(static_cast<float>(-std::atan2(dy, horiz) * 180.0 / kPi), -90.0f, 90.0f);

        if (settings.fov < 180 && std::abs(wrapDegrees(yaw - realYaw)) > halfFov)
            continue;

        if (!settings.throughWalls && !hasLineOfSight(eyeX, eyeY, eyeZ, aimX, aimY, aimZ))
            continue;

        cands.push_back({i, reachDist, aimX, aimY, aimZ, yaw, pitch, p.health, p.hurtTime});
    }

    // Sort by the selected priority.
    if (!cands.empty()) {
        switch (settings.targetMode) {
            case TargetMode::Health:
                std::sort(cands.begin(), cands.end(),
                          [](const Cand& a, const Cand& b) { return a.health < b.health; });
                break;
            case TargetMode::Angle:
                std::sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
                    return std::abs(wrapDegrees(a.yaw - realYaw)) < std::abs(wrapDegrees(b.yaw - realYaw));
                });
                break;
            case TargetMode::Distance:
            default:
                std::sort(cands.begin(), cands.end(),
                          [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
                break;
        }
    }

    // Pick the primary (rotation) target.
    const Cand* primary = nullptr;
    if (!cands.empty()) {
        if (settings.switchMode == SwitchMode::Switch) {
            primary = &cands[switchIndex_ % static_cast<int>(cands.size())];
        } else {
            // Single/Multi: keep a sticky target when it is still a valid candidate.
            if (!targetUuid_.empty()) {
                for (const auto& c : cands) {
                    if (players[c.idx].uuid == targetUuid_) {
                        primary = &c;
                        break;
                    }
                }
            }
            if (!primary)
                primary = &cands[0];
        }
        targetUuid_ = players[primary->idx].uuid;
    } else {
        targetUuid_ = "";
    }

    // Hand the selection to onOutgoingPacket (network thread). It does the rotation
    // and the attack, tick-synchronised and pre-flying. We only publish the target
    // (as fresh GlobalRefs) + the aim angles here.
    if (!primary) {
        clearTarget(env);  // locks targetMutex_ internally
    } else {
        jobject newPrimary = env->NewGlobalRef(players[primary->idx].instance);
        std::vector<jobject> newMulti;
        if (settings.switchMode == SwitchMode::Multi) {
            const int limit = std::clamp(settings.multiTargets, 1, 6);
            int extra = 0;
            for (const auto& c : cands) {
                if (extra >= limit - 1)
                    break;
                if (&c == primary)
                    continue;
                if (players[c.idx].instance) {
                    newMulti.push_back(env->NewGlobalRef(players[c.idx].instance));
                    ++extra;
                }
            }
        }
        std::lock_guard<std::mutex> lock(targetMutex_);
        if (targetInstance_)
            env->DeleteGlobalRef(targetInstance_);
        targetInstance_ = newPrimary;
        for (jobject g : multiInstances_)
            if (g)
                env->DeleteGlobalRef(g);
        multiInstances_ = std::move(newMulti);
        aimYaw_ = primary->yaw;
        aimPitch_ = primary->pitch;
        targetDist_ = static_cast<float>(primary->dist);
        targetHurtTime_ = players[primary->idx].hurtTime;
        haveTarget_ = true;
    }

    // Free the JNI GlobalRefs from the player list.
    for (auto& p : players) {
        if (p.instance) {
            env->DeleteGlobalRef(p.instance);
            p.instance = nullptr;
        }
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void KillAura::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Target & Rotation",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled)
                    setEnabled(enabledValue);
            }
            ImGui::Spacing();

            ui::SliderFloat("Reach", &settings.reach, 3.0f, 6.0f, "%.2f blocks");
            ui::SliderInt("FOV", &settings.fov, 0, 180, "%d deg");

            const char* targetItems[] = {"Distance", "Health", "Angle"};
            int tm = static_cast<int>(settings.targetMode);
            if (ui::Combo("Target", &tm, targetItems, IM_ARRAYSIZE(targetItems)))
                settings.targetMode = static_cast<TargetMode>(tm);

            const char* switchItems[] = {"Single", "Switch", "Multi"};
            int sm = static_cast<int>(settings.switchMode);
            if (ui::Combo("Targets", &sm, switchItems, IM_ARRAYSIZE(switchItems)))
                settings.switchMode = static_cast<SwitchMode>(sm);
            if (settings.switchMode == SwitchMode::Multi)
                ui::SliderInt("Max Targets", &settings.multiTargets, 1, 6, "%d");

            ImGui::Spacing();

            ui::Checkbox("MoveFix", &settings.moveFix);
            ui::Checkbox("Silent", &settings.silent);

            ui::SliderFloat("Rotation Speed", &settings.rotationSpeed, 5.0f, 180.0f, "%.0f deg/tick");
            ui::Checkbox("Randomization", &settings.randomization);
            if (settings.randomization)
                ui::SliderFloat("Jitter", &settings.jitter, 0.0f, 4.0f, "%.1f deg");
            ui::Checkbox("Rotate On Hit Only", &settings.rotateOnHitOnly);
        },
        "Clicking & Filters",
        [this]() {
            ui::SliderInt("Min CPS", &settings.minCPS, 1, 20, "%d");
            if (settings.minCPS > settings.maxCPS)
                settings.maxCPS = settings.minCPS;
            ui::SliderInt("Max CPS", &settings.maxCPS, 1, 20, "%d");
            if (settings.maxCPS < settings.minCPS)
                settings.minCPS = settings.maxCPS;
            ui::Checkbox("Click Randomization", &settings.clickRandomization);
            ui::SliderInt("Hit Chance", &settings.hitChance, 0, 100, "%d%%");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ui::SliderInt("Max Hurt Time", &settings.maxHurtTime, 0, 10, "%d");
            ui::Checkbox("Through Walls", &settings.throughWalls);
            ui::Checkbox("Target Invisibles", &settings.invisibles);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (!targetUuid_.empty())
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Target locked");
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No target");
        });
}

std::string KillAura::getDisplayInfo() {
    switch (settings.targetMode) {
        case TargetMode::Health:
            return "Health";
        case TargetMode::Angle:
            return "Angle";
        default:
            return "Distance";
    }
}

void KillAura::saveSettings(json::Value& node) const {
    node["reach"] = settings.reach;
    node["fov"] = settings.fov;
    node["targetMode"] = static_cast<int>(settings.targetMode);
    node["switchMode"] = static_cast<int>(settings.switchMode);
    node["multiTargets"] = settings.multiTargets;
    node["throughWalls"] = settings.throughWalls;
    node["invisibles"] = settings.invisibles;
    node["weaponOnly"] = settings.weaponOnly;
    node["maxHurtTime"] = settings.maxHurtTime;
    node["moveFix"] = settings.moveFix;
    node["silent"] = settings.silent;
    node["rotationSpeed"] = settings.rotationSpeed;
    node["randomization"] = settings.randomization;
    node["jitter"] = settings.jitter;
    node["rotateOnHitOnly"] = settings.rotateOnHitOnly;
    node["minCPS"] = settings.minCPS;
    node["maxCPS"] = settings.maxCPS;
    node["clickRandomization"] = settings.clickRandomization;
    node["hitChance"] = settings.hitChance;
}

void KillAura::loadSettings(const json::Value& node) {
    settings.reach = std::clamp(node["reach"].as_float(settings.reach), 3.0f, 6.0f);
    settings.fov = std::clamp(node["fov"].as_int(settings.fov), 0, 180);
    settings.targetMode = static_cast<TargetMode>(std::clamp(node["targetMode"].as_int(static_cast<int>(settings.targetMode)), 0, 2));
    settings.switchMode = static_cast<SwitchMode>(std::clamp(node["switchMode"].as_int(static_cast<int>(settings.switchMode)), 0, 2));
    settings.multiTargets = std::clamp(node["multiTargets"].as_int(settings.multiTargets), 1, 6);
    settings.throughWalls = node["throughWalls"].as_bool(settings.throughWalls);
    settings.invisibles = node["invisibles"].as_bool(settings.invisibles);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
    // Older configs still carry "antiBot" here; ignored on purpose, the AntiBot module owns it.
    settings.maxHurtTime = std::clamp(node["maxHurtTime"].as_int(settings.maxHurtTime), 0, 10);
    settings.moveFix = node["moveFix"].as_bool(settings.moveFix);
    settings.silent = node["silent"].as_bool(settings.silent);
    settings.rotationSpeed = std::clamp(node["rotationSpeed"].as_float(settings.rotationSpeed), 5.0f, 180.0f);
    settings.randomization = node["randomization"].as_bool(settings.randomization);
    settings.jitter = std::clamp(node["jitter"].as_float(settings.jitter), 0.0f, 4.0f);
    settings.rotateOnHitOnly = node["rotateOnHitOnly"].as_bool(settings.rotateOnHitOnly);
    settings.minCPS = std::clamp(node["minCPS"].as_int(settings.minCPS), 1, 20);
    settings.maxCPS = std::clamp(node["maxCPS"].as_int(settings.maxCPS), 1, 20);
    settings.clickRandomization = node["clickRandomization"].as_bool(settings.clickRandomization);
    settings.hitChance = std::clamp(node["hitChance"].as_int(settings.hitChance), 0, 100);
}
