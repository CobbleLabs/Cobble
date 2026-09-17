#include "silentaura.h"

#include "aimassist.h"
#include "antibot.h"
#include "autoclicker.h"
#include "blockhit.h"
#include "reach.h"
#include "../../jni/java.h"
#include "../../module/movement/blink.h"
#include "../../module/movement/fakelag.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../sdk/jni/member_resolver.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

SilentAura* silentAura = nullptr;

namespace {

constexpr float kPi = 3.14159265358979323846f;

float wrapAngle(float angle) {
    angle = std::fmod(angle + 180.0f, 360.0f);
    if (angle < 0.0f)
        angle += 360.0f;
    return angle - 180.0f;
}

std::pair<float, float> anglesTo(double fromX, double fromY, double fromZ,
                                 double toX, double toY, double toZ) {
    const double dx = toX - fromX;
    const double dy = toY - fromY;
    const double dz = toZ - fromZ;
    const double horizontal = std::sqrt(dx * dx + dz * dz);
    const float yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi) - 90.0f;
    const float pitch = static_cast<float>(-std::atan2(dy, horizontal) * 180.0 / kPi);
    return {yaw, pitch};
}

float armorValue(const PlayerInfo& target) {
    float score = 0.0f;
    for (const auto& piece : target.armor) {
        if (piece.id >= 298 && piece.id <= 301)
            score += 1.0f;
        else if (piece.id >= 302 && piece.id <= 305)
            score += 2.0f;
        else if (piece.id >= 306 && piece.id <= 309)
            score += 3.0f;
        else if (piece.id >= 310 && piece.id <= 313)
            score += 4.0f;
        else if (piece.id >= 314 && piece.id <= 317)
            score += 2.0f;
        score += static_cast<float>(piece.protection) * 0.5f;
    }
    return score;
}

float weaponValue(const PlayerInfo& target) {
    float score = 1.0f;
    switch (target.weapon.id) {
        case 268:
        case 283:
            score = 4.0f;
            break;
        case 272:
            score = 5.0f;
            break;
        case 267:
            score = 6.0f;
            break;
        case 276:
            score = 7.0f;
            break;
        case 271:
        case 286:
            score = 3.0f;
            break;
        case 275:
            score = 4.0f;
            break;
        case 258:
            score = 5.0f;
            break;
        case 279:
            score = 6.0f;
            break;
    }
    return score + static_cast<float>(target.weapon.sharpness) * 1.25f;
}

struct C03Fields {
    jclass packetClass = nullptr;
    jfieldID yaw = nullptr;
    jfieldID pitch = nullptr;
    jfieldID rotating = nullptr;
};

C03Fields g_c03;
jclass g_c02 = nullptr;
jclass g_c0a = nullptr;
std::mutex g_c03Mutex;
std::atomic<bool> g_c03Resolved{false};
std::atomic<bool> g_c02Resolved{false};
std::atomic<bool> g_c0aResolved{false};
thread_local bool g_silentAuraAttackEnqueue = false;

bool resolveC03(JNIEnv* env) {
    if (g_c03Resolved.load(std::memory_order_acquire))
        return true;

    std::lock_guard<std::mutex> lock(g_c03Mutex);
    if (g_c03Resolved.load(std::memory_order_relaxed))
        return true;

    jclass localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", localClass) &&
        !(Java::RuntimeObfuscated && Java::Version == MinecraftVersion::MC_1_7_10 &&
          Java::AssignClass("jd", localClass))) {
        return false;
    }

    jclass packetClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
    if (!packetClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    auto field = [env, packetClass](const char* mcp, const char* srg, const char* notch, const char* signature) {
        jfieldID id = env->GetFieldID(packetClass, mcp, signature);
        if (!id) {
            env->ExceptionClear();
            id = env->GetFieldID(packetClass, srg, signature);
        }
        if (!id) {
            env->ExceptionClear();
            id = env->GetFieldID(packetClass, notch, signature);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return id;
    };

    jfieldID yaw = field("yaw", "field_149476_e", "e", "F");
    jfieldID pitch = field("pitch", "field_149473_f", "f", "F");
    jfieldID rotating = field("rotating", "field_149481_i", "i", "Z");
    if (!yaw || !pitch) {
        const auto floats = jni_resolver::findFieldsBySignature(packetClass, "F", false);
        if (floats.size() == 2) {
            yaw = floats[0];
            pitch = floats[1];
        }
    }

    if (!yaw || !pitch || !rotating) {
        env->DeleteGlobalRef(packetClass);
        return false;
    }

    g_c03.packetClass = packetClass;
    g_c03.yaw = yaw;
    g_c03.pitch = pitch;
    g_c03.rotating = rotating;
    g_c03Resolved.store(true, std::memory_order_release);
    return true;
}

bool resolveC02(JNIEnv* env) {
    if (g_c02Resolved.load(std::memory_order_acquire))
        return true;

    std::lock_guard<std::mutex> lock(g_c03Mutex);
    if (g_c02Resolved.load(std::memory_order_relaxed))
        return true;

    jclass localClass = nullptr;
    bool loaded = Java::AssignClass("net.minecraft.network.play.client.C02PacketUseEntity", localClass);
    if (!loaded && Java::RuntimeObfuscated) {
        const char* rawName = Java::Version == MinecraftVersion::MC_1_7_10 ? "ja" : "io";
        loaded = Java::AssignClass(rawName, localClass);
    }
    if (!loaded)
        return false;
    g_c02 = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
    if (!g_c02 || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    g_c02Resolved.store(true, std::memory_order_release);
    return true;
}

bool resolveC0A(JNIEnv* env) {
    if (g_c0aResolved.load(std::memory_order_acquire))
        return true;

    std::lock_guard<std::mutex> lock(g_c03Mutex);
    if (g_c0aResolved.load(std::memory_order_relaxed))
        return true;

    jclass localClass = nullptr;
    if (!Java::AssignClass("net.minecraft.network.play.client.C0APacketAnimation", localClass))
        return false;
    g_c0a = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
    if (!g_c0a || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    g_c0aResolved.store(true, std::memory_order_release);
    return true;
}

void resetC03(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(g_c03Mutex);
    g_c03Resolved.store(false, std::memory_order_release);
    g_c02Resolved.store(false, std::memory_order_release);
    g_c0aResolved.store(false, std::memory_order_release);
    if (g_c03.packetClass && env)
        env->DeleteGlobalRef(g_c03.packetClass);
    if (g_c02 && env)
        env->DeleteGlobalRef(g_c02);
    if (g_c0a && env)
        env->DeleteGlobalRef(g_c0a);
    g_c03 = {};
    g_c02 = nullptr;
    g_c0a = nullptr;
}

bool gameFocused() {
    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

bool packetQueueActive() {
    return (blink && blink->enabled) || (fakeLag && fakeLag->enabled);
}

bool readProtocolPosition(JNIEnv* env, jobject entity, double& x, double& y, double& z) {
    if (!env || !entity)
        return false;

    if (JniCache::fid_serverPosX && JniCache::fid_serverPosY && JniCache::fid_serverPosZ) {
        const int fixedX = env->GetIntField(entity, JniCache::fid_serverPosX);
        const int fixedY = env->GetIntField(entity, JniCache::fid_serverPosY);
        const int fixedZ = env->GetIntField(entity, JniCache::fid_serverPosZ);
        if (!env->ExceptionCheck()) {
            x = static_cast<double>(fixedX) / 32.0;
            y = static_cast<double>(fixedY) / 32.0;
            z = static_cast<double>(fixedZ) / 32.0;
            return true;
        }
        env->ExceptionClear();
    }

    if (!JniCache::fid_posX || !JniCache::fid_posY || !JniCache::fid_posZ)
        return false;
    x = env->GetDoubleField(entity, JniCache::fid_posX);
    y = env->GetDoubleField(entity, JniCache::fid_posY);
    z = env->GetDoubleField(entity, JniCache::fid_posZ);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

bool localAttackOrigin(double& x, double& y, double& z) {
    if (player.hasLastReportedPosition()) {
        x = player.getLastReportedPosX();
        y = player.getLastReportedPosY();
        z = player.getLastReportedPosZ();
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
            return true;
    }
    x = player.getPosX();
    y = player.getPosY();
    z = player.getPosZ();
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

float mouseRotationStep() {
    const float base = mc.getMouseSensitivity() * 0.6f + 0.2f;
    return base * base * base * 1.2f;
}


}  // namespace

SilentAura::SilentAura() : Module("SilentAura"), rng_(std::random_device{}()) {}

SilentAura::~SilentAura() {
    onDisable();
    resetC03(Java::Env());
}

void SilentAura::onEnable() {
    if (blockHit && blockHit->enabled)
        blockHit->setEnabled(false);
    if (aimAssist && aimAssist->enabled)
        aimAssist->setEnabled(false);
    if (reach && reach->enabled)
        reach->setEnabled(false);

    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    clearTarget(Java::Env());
    const auto now = std::chrono::steady_clock::now();
    nextScanAt_ = now;
    nextAttackAt_ = now + std::chrono::milliseconds(50);
    controllerUpdatedAt_ = now;
    packetPreparedAt_ = now;
    observedClickerClick_ = autoClicker ? autoClicker->lastAutoClick : std::chrono::steady_clock::time_point{};
    clickerAttackPending_ = false;
    preparedPacketSerial_ = rotationPacketSerial_.load(std::memory_order_acquire);
    consumedPacketSerial_ = preparedPacketSerial_;
}

void SilentAura::onDisable() {
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    clearTarget(Java::Env());
}

void SilentAura::onWorldChange() {
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    clearTarget(Java::Env());
    {
        std::lock_guard<std::mutex> publicationLock(publicationMutex_);
        wireRotationKnown_.store(false, std::memory_order_relaxed);
        publicationSequence_.fetch_add(1, std::memory_order_relaxed);
    }
    nextScanAt_ = std::chrono::steady_clock::now();
}

void SilentAura::clearTarget(JNIEnv* env) {
    rotationActive_.store(false, std::memory_order_release);
    invalidatePublishedRotation();
    managedRotationInitialized_ = false;
    clickerAttackPending_ = false;
    yawResidual_ = 0.0;
    pitchResidual_ = 0.0;
    if (target_.entity) {
        if (!env)
            return;
        env->DeleteGlobalRef(target_.entity);
    }
    target_ = {};
}

bool SilentAura::selectTarget(JNIEnv* env) {
    auto candidates = mc.getPlayerList();
    if (candidates.empty()) {
        clearTarget(env);
        return false;
    }

    double localX = 0.0;
    double localY = 0.0;
    double localZ = 0.0;
    if (!localAttackOrigin(localX, localY, localZ)) {
        clearTarget(env);
        return false;
    }
    const double eyeY = localY + (player.isSneaking() ? 1.54 : 1.62);
    const float visibleYaw = player.getYaw();
    const float visiblePitch = player.getPitch();
    const double range = 3.0 + std::clamp(static_cast<double>(settings.extraSwingDistance), 0.0, 3.0);
    const double rangeSq = range * range;
    const float maxAngle = std::clamp(static_cast<float>(settings.maxAngle) * 0.5f, 0.5f, 180.0f);

    const int localTeamColor = friendsManager ? mc.getLocalPlayerTeamColor() : -1;
    const std::string localName = friendsManager ? mc.getLocalPlayerName() : std::string{};

    int bestIndex = -1;
    float bestScore = std::numeric_limits<float>::max();
    double bestAimX = 0.0;
    double bestAimY = 0.0;
    double bestAimZ = 0.0;
    double bestPosX = 0.0;
    double bestPosY = 0.0;
    double bestPosZ = 0.0;

    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const PlayerInfo& candidate = candidates[i];
        if (!candidate.instance || candidate.health <= 0.0f || AntiBot::Filtered(candidate))
            continue;
        if (candidate.isInvisible && !settings.targetInvisibles)
            continue;

        double candidateX = 0.0;
        double candidateY = 0.0;
        double candidateZ = 0.0;
        if (!readProtocolPosition(env, candidate.instance, candidateX, candidateY, candidateZ))
            continue;

        const double dx = candidateX - localX;
        const double dy = candidateY - localY;
        const double dz = candidateZ - localZ;
        const double distanceSq = dx * dx + dy * dy + dz * dz;
        if (distanceSq < 0.0064 || distanceSq > rangeSq)
            continue;

        if (friendsManager) {
            const bool directTeam = candidate.isOnLocalTeam && friendsManager->honorsVanillaTeams();
            const bool lunarTeam = !candidate.uuid.empty() && friendsManager->isLunarTeammate(candidate.uuid);
            const bool apolloTeam = !candidate.uuid.empty() && friendsManager->isApolloTeammate(candidate.uuid);
            const bool friendUuid = !candidate.uuid.empty() && friendsManager->isFriendByUUID(candidate.uuid);
            const bool friendName = friendsManager->isFriend(FriendsManager::stripColorCodes(candidate.name));
            const std::string targetName = candidate.profileName.empty() ? candidate.name : candidate.profileName;
            const bool scoreboardTeam = friendsManager->isScoreboardTeammate(localName, targetName);
            const bool colorTeam = friendsManager->isTeammate(
                candidate.uuid, static_cast<ChatColor>(candidate.teamColor), static_cast<ChatColor>(localTeamColor));
            if (directTeam || lunarTeam || apolloTeam || friendUuid || friendName || scoreboardTeam || colorTeam)
                continue;
        }

        const double targetHeight = candidate.isSneaking ? 1.62 : 1.8;
        const double minX = candidateX - 0.3;
        const double maxX = candidateX + 0.3;
        const double minZ = candidateZ - 0.3;
        const double maxZ = candidateZ + 0.3;
        double aimX = candidateX;
        double aimY = candidateY + targetHeight * 0.5;
        double aimZ = candidateZ;
        if (settings.targetArea == TargetArea::CLOSEST) {
            constexpr double inset = 0.05;
            aimX = std::clamp(localX, minX + inset, maxX - inset);
            aimZ = std::clamp(localZ, minZ + inset, maxZ - inset);
            const double pointDx = aimX - localX;
            const double pointDz = aimZ - localZ;
            if (pointDx * pointDx + pointDz * pointDz < 0.02) {
                aimX = candidateX;
                aimZ = candidateZ;
            }
        }

        const auto [yaw, pitch] = anglesTo(localX, eyeY, localZ, aimX, aimY, aimZ);
        const float yawError = std::abs(wrapAngle(yaw - visibleYaw));
        const float pitchError = std::abs(pitch - visiblePitch);
        const float angularDistance = std::sqrt(yawError * yawError + pitchError * pitchError);
        if (angularDistance > maxAngle)
            continue;

        const float distance = static_cast<float>(std::sqrt(distanceSq));
        float score = 0.0f;
        switch (settings.targetMode) {
            case TargetMode::DISTANCE:
                score = distance;
                break;
            case TargetMode::YAW:
                score = angularDistance;
                break;
            case TargetMode::ARMOR:
                score = armorValue(candidate) + distance * 0.001f;
                break;
            case TargetMode::THREAT:
                score = -(weaponValue(candidate) + armorValue(candidate) * 0.25f) + distance * 0.001f;
                break;
            case TargetMode::HEALTH:
                score = candidate.health + distance * 0.001f;
                break;
        }
        if (settings.switchTargets && candidate.hurtTime > 0)
            score += 1000.0f + static_cast<float>(candidate.hurtTime);

        if (score < bestScore) {
            bestScore = score;
            bestIndex = i;
            bestAimX = aimX;
            bestAimY = aimY;
            bestAimZ = aimZ;
            bestPosX = candidateX;
            bestPosY = candidateY;
            bestPosZ = candidateZ;
        }
    }

    if (bestIndex < 0) {
        for (auto& candidate : candidates) {
            if (candidate.instance)
                env->DeleteGlobalRef(candidate.instance);
        }
        clearTarget(env);
        return false;
    }

    PlayerInfo& selected = candidates[bestIndex];
    jobject selectedEntity = selected.instance;
    selected.instance = nullptr;
    for (auto& candidate : candidates) {
        if (candidate.instance)
            env->DeleteGlobalRef(candidate.instance);
    }

    bool sameTarget = false;
    if (target_.entity && target_.entityId == selected.entityId) {
        sameTarget = env->IsSameObject(target_.entity, selectedEntity) == JNI_TRUE;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            sameTarget = false;
        }
    }
    if (sameTarget) {
        env->DeleteGlobalRef(selectedEntity);
    } else {
        clearTarget(env);
        target_.entity = selectedEntity;
        target_.entityId = selected.entityId;
        managedRotationInitialized_ = false;
        preparedPacketSerial_ = rotationPacketSerial_.load(std::memory_order_acquire);
        consumedPacketSerial_ = preparedPacketSerial_;
    }

    target_.entityId = selected.entityId;
    target_.posX = bestPosX;
    target_.posY = bestPosY;
    target_.posZ = bestPosZ;
    target_.aimX = bestAimX;
    target_.aimY = bestAimY;
    target_.aimZ = bestAimZ;
    return true;
}

bool SilentAura::updateRotation(std::chrono::steady_clock::time_point now) {
    JNIEnv* env = Java::Env();
    double localX = 0.0;
    double localY = 0.0;
    double localZ = 0.0;
    if (!target_.entity || !env || !resolveC02(env) || !localAttackOrigin(localX, localY, localZ)) {
        rotationActive_.store(false, std::memory_order_release);
        return false;
    }

    const double eyeY = localY + (player.isSneaking() ? 1.54 : 1.62);
    const auto [targetYaw, targetPitch] = anglesTo(localX, eyeY, localZ,
                                                   target_.aimX, target_.aimY, target_.aimZ);

    if (controllerRebaseRequested_.exchange(false, std::memory_order_acq_rel))
        managedRotationInitialized_ = false;

    if (!managedRotationInitialized_) {
        std::lock_guard<std::mutex> publicationLock(publicationMutex_);
        const bool wireKnown = wireRotationKnown_.load(std::memory_order_relaxed);
        managedYaw_ = wireKnown ? wireYaw_.load(std::memory_order_relaxed) : player.getYaw();
        managedPitch_ = wireKnown ? wirePitch_.load(std::memory_order_relaxed) : player.getPitch();
        yawResidual_ = 0.0;
        pitchResidual_ = 0.0;
        managedRotationInitialized_ = true;
        controllerUpdatedAt_ = now;
    }

    float dt = std::chrono::duration<float>(now - controllerUpdatedAt_).count();
    controllerUpdatedAt_ = now;
    if (dt <= 0.0f || dt > 0.05f)
        dt = 0.002f;

    const float degreesPerSecond = 45.0f + std::clamp(settings.aimSpeed, 1.0f, 10.0f) * 35.0f;
    const float maxYawStep = degreesPerSecond * dt;
    const float maxPitchStep = maxYawStep * 0.75f;
    const float gcd = mouseRotationStep();
    if (!std::isfinite(gcd) || gcd < 0.0001f)
        return false;

    yawResidual_ += static_cast<double>(std::clamp(wrapAngle(targetYaw - managedYaw_),
                                                   -maxYawStep, maxYawStep));
    pitchResidual_ += static_cast<double>(std::clamp(targetPitch - managedPitch_,
                                                     -maxPitchStep, maxPitchStep));
    const long long yawSteps = std::llround(yawResidual_ / static_cast<double>(gcd));
    const long long requestedPitchSteps = std::llround(pitchResidual_ / static_cast<double>(gcd));
    const long long minimumPitchSteps = static_cast<long long>(
        std::ceil((-90.0 - static_cast<double>(managedPitch_)) / static_cast<double>(gcd)));
    const long long maximumPitchSteps = static_cast<long long>(
        std::floor((90.0 - static_cast<double>(managedPitch_)) / static_cast<double>(gcd)));
    const long long pitchSteps = std::clamp(requestedPitchSteps, minimumPitchSteps, maximumPitchSteps);
    const float yawStep = static_cast<float>(yawSteps) * gcd;
    const float pitchStep = static_cast<float>(pitchSteps) * gcd;
    yawResidual_ -= static_cast<double>(yawStep);
    pitchResidual_ -= static_cast<double>(pitchStep);
    managedYaw_ += yawStep;
    managedPitch_ += pitchStep;

    rotationSequence_.fetch_add(1, std::memory_order_acq_rel);
    serverYaw_.store(managedYaw_, std::memory_order_relaxed);
    serverPitch_.store(managedPitch_, std::memory_order_relaxed);
    rotationSequence_.fetch_add(1, std::memory_order_release);
    rotationUpdatedAtMs_.store(GetTickCount64(), std::memory_order_release);
    rotationActive_.store(true, std::memory_order_release);

    const float yawError = std::abs(wrapAngle(targetYaw - managedYaw_));
    const float pitchError = std::abs(targetPitch - managedPitch_);
    return std::sqrt(yawError * yawError + pitchError * pitchError) <= 3.0f;
}

bool SilentAura::publishedRayHitsTarget(JNIEnv* env, float yaw, float pitch) {
    if (!env || !target_.entity || !JniCache::mid_isSneaking)
        return false;

    double entityX = 0.0;
    double entityY = 0.0;
    double entityZ = 0.0;
    if (!readProtocolPosition(env, target_.entity, entityX, entityY, entityZ))
        return false;

    const bool targetSneaking = env->CallBooleanMethod(target_.entity, JniCache::mid_isSneaking) == JNI_TRUE;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    const double height = targetSneaking ? 1.62 : 1.8;
    const double minX = entityX - 0.3;
    const double minY = entityY;
    const double minZ = entityZ - 0.3;
    const double maxX = entityX + 0.3;
    const double maxY = entityY + height;
    const double maxZ = entityZ + 0.3;

    double originX = 0.0;
    double originYFeet = 0.0;
    double originZ = 0.0;
    if (!localAttackOrigin(originX, originYFeet, originZ))
        return false;
    const double originY = originYFeet + (player.isSneaking() ? 1.54 : 1.62);
    const double centerDx = entityX - originX;
    const double centerDy = entityY - originYFeet;
    const double centerDz = entityZ - originZ;
    constexpr double attackReach = 2.90;
    if (centerDx * centerDx + centerDy * centerDy + centerDz * centerDz > attackReach * attackReach)
        return false;
    const double yawRadians = static_cast<double>(yaw) * 3.14159265358979323846 / 180.0;
    const double pitchRadians = static_cast<double>(pitch) * 3.14159265358979323846 / 180.0;
    const double pitchCos = std::cos(pitchRadians);
    const double directionX = -std::sin(yawRadians) * pitchCos;
    const double directionY = -std::sin(pitchRadians);
    const double directionZ = std::cos(yawRadians) * pitchCos;

    double entry = 0.0;
    double exit = attackReach;
    auto intersectAxis = [&entry, &exit](double origin, double direction, double minimum, double maximum) {
        if (std::abs(direction) < 1e-9)
            return origin >= minimum && origin <= maximum;
        double first = (minimum - origin) / direction;
        double second = (maximum - origin) / direction;
        if (first > second)
            std::swap(first, second);
        entry = std::max(entry, first);
        exit = std::min(exit, second);
        return entry <= exit;
    };

    if (!intersectAxis(originX, directionX, minX, maxX) ||
        !intersectAxis(originY, directionY, minY, maxY) ||
        !intersectAxis(originZ, directionZ, minZ, maxZ) || entry < 0.0 || entry > attackReach) {
        return false;
    }

    Minecraft::Vec3 from(static_cast<float>(originX), static_cast<float>(originY), static_cast<float>(originZ));
    Minecraft::Vec3 hitPoint(static_cast<float>(originX + directionX * entry),
                             static_cast<float>(originY + directionY * entry),
                             static_cast<float>(originZ + directionZ * entry));
    Minecraft::Vec3 blockHit{};
    const auto blockResult = mc.rayTraceBlocks(from, hitPoint, blockHit, false);
    return blockResult == Minecraft::RAYTRACE_BLOCKS_RESULT::NO_HIT ||
           blockResult == Minecraft::RAYTRACE_BLOCKS_RESULT::NO_HIT_FROM_AIRBLOCK;
}

void SilentAura::tryAttack(JNIEnv* env, std::chrono::steady_clock::time_point now, bool aimReady) {
    if (!aimReady || !target_.entity)
        return;

    const bool clickerPaced = autoClicker && autoClicker->enabled;
    if (clickerPaced) {
        if (autoClicker->lastAutoClick != observedClickerClick_) {
            observedClickerClick_ = autoClicker->lastAutoClick;
            clickerAttackPending_ = true;
        }
        if (!clickerAttackPending_)
            return;
    } else if (now < nextAttackAt_) {
        return;
    }

    std::uint64_t publicationVersion = 0;
    std::uint64_t packetSerial = 0;
    float publishedYaw = 0.0f;
    float publishedPitch = 0.0f;
    {
        std::lock_guard<std::mutex> lock(publicationMutex_);
        if (!publishedRotationValid_.load(std::memory_order_relaxed))
            return;
        publicationVersion = publicationSequence_.load(std::memory_order_relaxed);
        packetSerial = rotationPacketSerial_.load(std::memory_order_relaxed);
        publishedYaw = publishedYaw_.load(std::memory_order_relaxed);
        publishedPitch = publishedPitch_.load(std::memory_order_relaxed);
    }

    if (packetSerial == consumedPacketSerial_)
        return;
    if (packetSerial != preparedPacketSerial_) {
        preparedPacketSerial_ = packetSerial;
        packetPreparedAt_ = now;
        return;
    }
    if (now - packetPreparedAt_ < std::chrono::milliseconds(2))
        return;

    if (!publishedRayHitsTarget(env, publishedYaw, publishedPitch)) {
        nextScanAt_ = now;
        nextAttackAt_ = now + std::chrono::milliseconds(20);
        return;
    }

    bool attacked = false;
    {
        std::lock_guard<std::mutex> lock(publicationMutex_);
        if (!publishedRotationValid_.load(std::memory_order_relaxed) ||
            publicationSequence_.load(std::memory_order_relaxed) != publicationVersion ||
            rotationPacketSerial_.load(std::memory_order_relaxed) != packetSerial) {
            return;
        }
        consumedPacketSerial_ = packetSerial;
        g_silentAuraAttackEnqueue = true;
        attacked = mc.attackEntity(target_.entity);
        g_silentAuraAttackEnqueue = false;
    }

    if (attacked) {
        clickerAttackPending_ = false;
        scheduleNextAttack(now);
    } else {
        nextAttackAt_ = now + std::chrono::milliseconds(50);
    }
}

void SilentAura::scheduleNextAttack(std::chrono::steady_clock::time_point now) {
    float minCps = std::clamp(settings.minCps, 1.0f, 20.0f);
    float maxCps = std::clamp(settings.maxCps, 1.0f, 20.0f);
    if (minCps > maxCps)
        std::swap(minCps, maxCps);

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float blend = (unit(rng_) + unit(rng_)) * 0.5f;
    const float cps = minCps + (maxCps - minCps) * blend;
    const int delayMs = static_cast<int>(std::lround(1000.0f / std::max(cps, 1.0f)));
    nextAttackAt_ = now + std::chrono::milliseconds(delayMs);
}

bool SilentAura::isHoldingWeapon() const {
    const int id = player.getHeldItemId();
    return id == 268 || id == 272 || id == 267 || id == 276 || id == 283 ||
           id == 271 || id == 275 || id == 258 || id == 279 || id == 286;
}

void SilentAura::onUpdate() {
    std::lock_guard<std::recursive_mutex> lock(stateMutex_);
    JNIEnv* env = Java::Env();
    if (!env) {
        rotationActive_.store(false, std::memory_order_release);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (target_.entity && now - controllerUpdatedAt_ > std::chrono::milliseconds(100)) {
        clearTarget(env);
        nextScanAt_ = now;
        preparedPacketSerial_ = rotationPacketSerial_.load(std::memory_order_acquire);
        consumedPacketSerial_ = preparedPacketSerial_;
    }

    const bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (!player.isValid() || Menu::isOpen || mc.isInGui() || packetQueueActive() ||
        (settings.requireMouseDown && !mouseDown) ||
        (settings.weaponOnly && !isHoldingWeapon()) ||
        (settings.breakBlocks && mouseDown && mc.isTargetingBlock())) {
        clearTarget(env);
        controllerUpdatedAt_ = now;
        return;
    }

    if (now >= nextScanAt_) {
        selectTarget(env);
        nextScanAt_ = now + std::chrono::milliseconds(50);
    }
    if (!target_.entity)
        return;

    tryAttack(env, now, updateRotation(now));
}

bool SilentAura::isRewritingRotation() const {
    if (!rotationActive_.load(std::memory_order_acquire) || !gameFocused() || packetQueueActive())
        return false;
    const std::uint64_t updatedAt = rotationUpdatedAtMs_.load(std::memory_order_acquire);
    const std::uint64_t now = GetTickCount64();
    return updatedAt != 0 && now >= updatedAt && now - updatedAt <= 100;
}

bool SilentAura::shouldSendOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet || !isRewritingRotation())
        return true;
    if (!resolveC02(env))
        return false;
    const bool useEntity = env->IsInstanceOf(packet, g_c02) == JNI_TRUE;
    const bool animation = resolveC0A(env) && env->IsInstanceOf(packet, g_c0a) == JNI_TRUE;
    if (!useEntity && !animation)
        return true;
    return g_silentAuraAttackEnqueue;
}

void SilentAura::invalidatePublishedRotation() {
    std::lock_guard<std::mutex> lock(publicationMutex_);
    publishedRotationValid_.store(false, std::memory_order_relaxed);
    publicationSequence_.fetch_add(1, std::memory_order_relaxed);
}

void SilentAura::observeAcceptedOutgoingPacket(JNIEnv* env, jobject packet, bool invalidateAttack) {
    if (!env || !packet || !resolveC03(env) ||
        env->IsInstanceOf(packet, g_c03.packetClass) != JNI_TRUE) {
        return;
    }

    const bool rotating = env->GetBooleanField(packet, g_c03.rotating) == JNI_TRUE;
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (rotating) {
        yaw = env->GetFloatField(packet, g_c03.yaw);
        pitch = env->GetFloatField(packet, g_c03.pitch);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    std::lock_guard<std::mutex> lock(publicationMutex_);
    if (rotating) {
        wireYaw_.store(yaw, std::memory_order_relaxed);
        wirePitch_.store(pitch, std::memory_order_relaxed);
        wireRotationKnown_.store(true, std::memory_order_relaxed);
        if (invalidateAttack)
            controllerRebaseRequested_.store(true, std::memory_order_release);
    }
    if (invalidateAttack)
        publishedRotationValid_.store(false, std::memory_order_relaxed);
    publicationSequence_.fetch_add(1, std::memory_order_relaxed);
}

void SilentAura::onOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet || !isRewritingRotation())
        return;

    if (!resolveC03(env) || env->IsInstanceOf(packet, g_c03.packetClass) != JNI_TRUE)
        return;

    float yaw = 0.0f;
    float pitch = 0.0f;
    for (;;) {
        const std::uint64_t before = rotationSequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        yaw = serverYaw_.load(std::memory_order_relaxed);
        pitch = serverPitch_.load(std::memory_order_relaxed);
        if (rotationSequence_.load(std::memory_order_acquire) == before)
            break;
    }

    std::lock_guard<std::mutex> publicationLock(publicationMutex_);
    if (!isRewritingRotation())
        return;

    const bool wireKnown = wireRotationKnown_.load(std::memory_order_relaxed);
    const float previousYaw = wireYaw_.load(std::memory_order_relaxed);
    const float previousPitch = wirePitch_.load(std::memory_order_relaxed);
    const bool changed = !wireKnown || std::memcmp(&yaw, &previousYaw, sizeof(float)) != 0 ||
                         std::memcmp(&pitch, &previousPitch, sizeof(float)) != 0;

    if (changed) {
        env->SetFloatField(packet, g_c03.yaw, yaw);
        env->SetFloatField(packet, g_c03.pitch, pitch);
        env->SetBooleanField(packet, g_c03.rotating, JNI_TRUE);
    } else {
        env->SetBooleanField(packet, g_c03.rotating, JNI_FALSE);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    if (changed) {
        wireYaw_.store(yaw, std::memory_order_relaxed);
        wirePitch_.store(pitch, std::memory_order_relaxed);
        wireRotationKnown_.store(true, std::memory_order_relaxed);
    }
    publishedYaw_.store(yaw, std::memory_order_relaxed);
    publishedPitch_.store(pitch, std::memory_order_relaxed);
    publishedRotationValid_.store(true, std::memory_order_relaxed);
    rotationPacketSerial_.fetch_add(1, std::memory_order_relaxed);
    publicationSequence_.fetch_add(1, std::memory_order_relaxed);
}

void SilentAura::onRenderMenu() {
    static const char* targetModes[] = {"Distance", "Yaw", "Armor", "Threat", "Health"};
    static const char* targetAreas[] = {"Center", "Closest"};
    bool enabledValue = enabled;
    int targetMode = static_cast<int>(settings.targetMode);
    int targetArea = static_cast<int>(settings.targetArea);

    ui::two_column(
        "Settings",
        [this, &enabledValue, &targetMode, &targetArea]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);
            ui::SliderFloat("Aim Speed", &settings.aimSpeed, 1.0f, 10.0f, "%.1f");
            ui::SliderFloat("Extra Swing Distance", &settings.extraSwingDistance, 0.0f, 3.0f, "%.1f");
            float minLimit = 1.0f;
            float maxLimit = 20.0f;
            ui::SliderFloat("Min CPS", &settings.minCps, 1.0f, 20.0f, &minLimit, &settings.maxCps, "%.1f");
            ui::SliderFloat("Max CPS", &settings.maxCps, 1.0f, 20.0f, &settings.minCps, &maxLimit, "%.1f");
            ui::SliderInt("Max Angle", &settings.maxAngle, 1, 360, "%d");
            if (ui::Combo("Target Mode", &targetMode, targetModes, IM_ARRAYSIZE(targetModes)))
                settings.targetMode = static_cast<TargetMode>(targetMode);
            if (ui::Combo("Target Area", &targetArea, targetAreas, IM_ARRAYSIZE(targetAreas)))
                settings.targetArea = static_cast<TargetArea>(targetArea);
        },
        "Conditions",
        [this]() {
            ui::Checkbox("Switch", &settings.switchTargets);
            ui::Checkbox("Require Mouse Down", &settings.requireMouseDown);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
            ui::Checkbox("Break Blocks", &settings.breakBlocks);
            ui::Checkbox("Target Invisibles", &settings.targetInvisibles);
        });
}

std::string SilentAura::getDisplayInfo() {
    static const char* names[] = {"Distance", "Yaw", "Armor", "Threat", "Health"};
    return names[std::clamp(static_cast<int>(settings.targetMode), 0, 4)];
}

void SilentAura::saveSettings(json::Value& node) const {
    node["aimSpeed"] = settings.aimSpeed;
    node["extraSwingDistance"] = settings.extraSwingDistance;
    node["minCps"] = settings.minCps;
    node["maxCps"] = settings.maxCps;
    node["maxAngle"] = settings.maxAngle;
    node["targetMode"] = static_cast<int>(settings.targetMode);
    node["targetArea"] = static_cast<int>(settings.targetArea);
    node["switch"] = settings.switchTargets;
    node["requireMouseDown"] = settings.requireMouseDown;
    node["weaponOnly"] = settings.weaponOnly;
    node["breakBlocks"] = settings.breakBlocks;
    node["targetInvisibles"] = settings.targetInvisibles;
}

void SilentAura::loadSettings(const json::Value& node) {
    settings.aimSpeed = std::clamp(node["aimSpeed"].as_float(settings.aimSpeed), 1.0f, 10.0f);
    settings.extraSwingDistance =
        std::clamp(node["extraSwingDistance"].as_float(settings.extraSwingDistance), 0.0f, 3.0f);
    settings.minCps = std::clamp(node["minCps"].as_float(settings.minCps), 1.0f, 20.0f);
    settings.maxCps = std::clamp(node["maxCps"].as_float(settings.maxCps), 1.0f, 20.0f);
    if (settings.minCps > settings.maxCps)
        std::swap(settings.minCps, settings.maxCps);
    settings.maxAngle = std::clamp(node["maxAngle"].as_int(settings.maxAngle), 1, 360);
    settings.targetMode = static_cast<TargetMode>(
        std::clamp(node["targetMode"].as_int(static_cast<int>(settings.targetMode)), 0, 4));
    settings.targetArea = static_cast<TargetArea>(
        std::clamp(node["targetArea"].as_int(static_cast<int>(settings.targetArea)), 0, 1));
    settings.switchTargets = node["switch"].as_bool(settings.switchTargets);
    settings.requireMouseDown = node["requireMouseDown"].as_bool(settings.requireMouseDown);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
    settings.breakBlocks = node["breakBlocks"].as_bool(settings.breakBlocks);
    settings.targetInvisibles = node["targetInvisibles"].as_bool(settings.targetInvisibles);
}
