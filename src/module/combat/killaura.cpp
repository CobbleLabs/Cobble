#include "killaura.h"
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

// Temporarily suspended per user request (WIP): while true, KillAura performs no
// rotation, no attacks and touches no packets, regardless of its enabled/saved state.
// Flip to false to re-arm it.
static constexpr bool kKillAuraSuspended = true;

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
    locked = true;  // WIP — hidden/grayed until experiments are unlocked
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
}

void KillAura::onDisable() {
    hasAim_.store(false, std::memory_order_release);
    haveCur_ = false;
    targetUuid_ = "";
    rotationSentTicks_ = 0;
    JNIEnv* env = Java::Env();
    if (env)
        clearTarget(env);
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

void KillAura::applyMoveFix(JNIEnv* env, jobject playerInstance, float diffDeg) {
    if (!moveFieldsResolved_) {
        moveFieldsResolved_ = true;
        jclass pcls = env->GetObjectClass(playerInstance);
        if (pcls) {
            miField_ = env->GetFieldID(pcls, "movementInput", "Lnet/minecraft/util/MovementInput;");
            if (!miField_) {
                env->ExceptionClear();
                miField_ = env->GetFieldID(pcls, "field_71158_b", "Lnet/minecraft/util/MovementInput;");
            }
            env->DeleteLocalRef(pcls);
        }
        if (miField_) {
            jobject mi = env->GetObjectField(playerInstance, miField_);
            if (mi) {
                jclass micls = env->GetObjectClass(mi);
                if (micls) {
                    miStrafe_ = env->GetFieldID(micls, "moveStrafe", "F");
                    if (!miStrafe_) {
                        env->ExceptionClear();
                        miStrafe_ = env->GetFieldID(micls, "field_78902_a", "F");
                    }
                    miForward_ = env->GetFieldID(micls, "moveForward", "F");
                    if (!miForward_) {
                        env->ExceptionClear();
                        miForward_ = env->GetFieldID(micls, "field_78900_b", "F");
                    }
                    env->DeleteLocalRef(micls);
                }
                env->DeleteLocalRef(mi);
            }
        }
        moveFieldsOk_ = miField_ && miStrafe_ && miForward_;
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!moveFieldsOk_)
        return;

    jobject mi = env->GetObjectField(playerInstance, miField_);
    if (!mi) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }
    const float strafe = env->GetFloatField(mi, miStrafe_);
    const float forward = env->GetFloatField(mi, miForward_);
    if (strafe != 0.0f || forward != 0.0f) {
        const float d = diffDeg * static_cast<float>(kPi) / 180.0f;
        const float cs = std::cos(d), sn = std::sin(d);
        env->SetFloatField(mi, miStrafe_, strafe * cs - forward * sn);
        env->SetFloatField(mi, miForward_, forward * cs + strafe * sn);
    }
    env->DeleteLocalRef(mi);
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void KillAura::onPreMotion(JNIEnv* env, jobject playerInstance) {
    if (kKillAuraSuspended)
        return;
    if (!enabled || !env || !playerInstance)
        return;

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

    // The real (mouse-driven) rotation this tick — valid because `silent` restores
    // rotationYaw to it at the end of every tick, so the game accumulates raw mouse
    // movement on top of it.
    realYaw_ = player.getYaw();
    realPitch_ = player.getPitch();

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
    // MoveFix: rotate the movement input so the world movement stays in the real look
    // direction. Without it, Grim's simulation sees movement that can't come from a
    // valid input relative to the sent (aura) yaw → Simulation flag while moving.
    if (settings.moveFix)
        applyMoveFix(env, playerInstance, realYaw_ - curYaw_);
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
    if (kKillAuraSuspended)
        return;
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
    }
}

void KillAura::onUpdate() {
    if (kKillAuraSuspended)
        return;
    if (!enabled)
        return;
    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        hasAim_.store(false, std::memory_order_release);
        haveCur_ = false;
        return;
    }
    if (settings.weaponOnly && !isHoldingWeapon()) {
        hasAim_.store(false, std::memory_order_release);
        haveCur_ = false;
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
        if (p.isNPC)
            continue;
        if (!settings.invisibles && p.isInvisible)
            continue;

        if (friendsManager) {
            bool friendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool friendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (friendByUUID || friendByName)
                continue;
            if (p.isOnLocalTeam)
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
    node["antiBot"] = settings.antiBot;
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
    settings.antiBot = node["antiBot"].as_bool(settings.antiBot);
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
