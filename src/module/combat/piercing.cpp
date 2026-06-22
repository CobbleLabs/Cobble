#include "piercing.h"
#include "backtrack.h"

#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/aes_string.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

Piercing* piercing = nullptr;

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double EPSILON = 1.0e-8;
constexpr std::uint64_t DUPLICATE_PACKET_GUARD_MS = 60;

bool isWeapon(int itemId) {
    return itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
           itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286;
}

bool isAttackOrSwingPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet)
        return false;

    jclass packetClass = env->GetObjectClass(packet);
    jclass classClass = packetClass ? env->GetObjectClass(packetClass) : nullptr;
    jmethodID getName =
        classClass ? env->GetMethodID(classClass, OBFUSCATE_STRING("getName").c_str(),
                                      OBFUSCATE_STRING("()Ljava/lang/String;").c_str())
                   : nullptr;
    jstring name = getName ? static_cast<jstring>(env->CallObjectMethod(packetClass, getName)) : nullptr;

    bool result = false;
    if (name && !env->ExceptionCheck()) {
        const char* chars = env->GetStringUTFChars(name, nullptr);
        if (chars) {
            result = std::strstr(chars, OBFUSCATE_STRING("C02PacketUseEntity").c_str()) != nullptr ||
                     std::strstr(chars, OBFUSCATE_STRING("C0APacketAnimation").c_str()) != nullptr;
            env->ReleaseStringUTFChars(name, chars);
        }
    }

    if (name)
        env->DeleteLocalRef(name);
    if (classClass)
        env->DeleteLocalRef(classClass);
    if (packetClass)
        env->DeleteLocalRef(packetClass);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    return result;
}
}  // namespace

Piercing::Piercing() : Module("Piercing") {
    keybind = 0;
}

void Piercing::onEnable() {
    manualAttackActive_.store(false, std::memory_order_release);
    suppressDuplicatePacketsUntilMs_.store(0, std::memory_order_release);
}

void Piercing::onDisable() {
    manualAttackActive_.store(false, std::memory_order_release);
    suppressDuplicatePacketsUntilMs_.store(0, std::memory_order_release);
}

std::uint64_t Piercing::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

bool Piercing::rayIntersectsBox(double originX, double originY, double originZ,
                                double directionX, double directionY, double directionZ,
                                double minX, double minY, double minZ,
                                double maxX, double maxY, double maxZ,
                                double maxDistance, double& hitDistance) {
    double entry = 0.0;
    double exit = maxDistance;

    const double origins[3] = {originX, originY, originZ};
    const double directions[3] = {directionX, directionY, directionZ};
    const double minimums[3] = {minX, minY, minZ};
    const double maximums[3] = {maxX, maxY, maxZ};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(directions[axis]) < EPSILON) {
            if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis])
                return false;
            continue;
        }

        double first = (minimums[axis] - origins[axis]) / directions[axis];
        double second = (maximums[axis] - origins[axis]) / directions[axis];
        if (first > second)
            std::swap(first, second);

        entry = std::max(entry, first);
        exit = std::min(exit, second);
        if (entry > exit)
            return false;
    }

    if (exit < 0.0 || entry > maxDistance)
        return false;

    hitDistance = std::max(0.0, entry);
    return true;
}

jobject Piercing::selectTarget(bool hiddenOnly, bool& targetVisible) {
    targetVisible = false;

    JNIEnv* env = Java::Env();
    if (!env)
        return nullptr;

    if (!enabled || !shouldUpdate() || Menu::isOpen || mc.isInGui() || !player.isValid() || !mc.instance)
        return nullptr;

    if (settings.weaponsOnly && !isWeapon(player.getHeldItemId()))
        return nullptr;

    if (hiddenOnly && !settings.throughWalls)
        return nullptr;

    if (env->ExceptionCheck())
        env->ExceptionClear();

    const double yaw = static_cast<double>(player.getYaw()) * PI / 180.0;
    const double pitch = static_cast<double>(player.getPitch()) * PI / 180.0;
    const double cosPitch = std::cos(pitch);
    const double directionX = -std::sin(yaw) * cosPitch;
    const double directionY = -std::sin(pitch);
    const double directionZ = std::cos(yaw) * cosPitch;

    const double originX = player.getPosX();
    const double originY = player.getPosY() + (player.isSneaking() ? 1.54 : 1.62);
    const double originZ = player.getPosZ();

    auto players = mc.getPlayerList();
    jobject selectedTarget = nullptr;
    double closestHit = std::numeric_limits<double>::max();

    const double expandVal = static_cast<double>(settings.expand);

    for (auto& candidate : players) {
        if (!candidate.instance || candidate.isNPC)
            continue;
        if (candidate.isInvisible && !settings.targetInvisibles)
            continue;
        if (settings.ignoreTeams && candidate.isOnLocalTeam)
            continue;
        if (settings.ignoreFriends && friendsManager &&
            (friendsManager->isFriendByUUID(candidate.uuid) ||
             friendsManager->isApolloTeammate(candidate.uuid) ||
             friendsManager->isLunarTeammate(candidate.uuid)))
            continue;

        const bool isVisible = mc.canEntityBeSeen(candidate.instance);
        if (hiddenOnly && isVisible)
            continue;
        if (!isVisible && !settings.throughWalls)
            continue;

        const double maxDistance =
            isVisible ? static_cast<double>(settings.distance) : static_cast<double>(settings.throughWallsRange);

        double minX = candidate.hitboxMinX;
        double minY = candidate.hitboxMinY;
        double minZ = candidate.hitboxMinZ;
        double maxX = candidate.hitboxMaxX;
        double maxY = candidate.hitboxMaxY;
        double maxZ = candidate.hitboxMaxZ;

        if (maxX <= minX || maxY <= minY || maxZ <= minZ) {
            minX = candidate.posX - 0.3;
            maxX = candidate.posX + 0.3;
            minY = candidate.posY;
            maxY = candidate.posY + (candidate.isSneaking ? 1.62 : 1.8);
            minZ = candidate.posZ - 0.3;
            maxZ = candidate.posZ + 0.3;
        }

        // Apply hitbox expansion
        if (expandVal > 0.0) {
            minX -= expandVal;
            minY -= expandVal;
            minZ -= expandVal;
            maxX += expandVal;
            maxY += expandVal;
            maxZ += expandVal;
        }

        double candidateHit = std::numeric_limits<double>::max();

        if (!hiddenOnly && settings.backtrackCheck && backtrack && backtrack->enabled) {
            const auto* history = backtrack->getPositionHistory(candidate.uuid);
            if (history && !history->empty()) {
                for (const auto& snap : *history) {
                    double snapMinX = snap.x - 0.3 - expandVal;
                    double snapMaxX = snap.x + 0.3 + expandVal;
                    double snapMinY = snap.y - expandVal;
                    double snapMaxY = snap.y + (candidate.isSneaking ? 1.62 : 1.8) + expandVal;
                    double snapMinZ = snap.z - 0.3 - expandVal;
                    double snapMaxZ = snap.z + 0.3 + expandVal;

                    double backHitDistance = 0.0;
                    if (rayIntersectsBox(originX, originY, originZ,
                                         directionX, directionY, directionZ,
                                         snapMinX, snapMinY, snapMinZ, snapMaxX, snapMaxY, snapMaxZ,
                                         maxDistance, backHitDistance)) {
                        candidateHit = std::min(candidateHit, backHitDistance);
                    }
                }
            }
        }

        double currentHit = 0.0;
        if (rayIntersectsBox(originX, originY, originZ,
                            directionX, directionY, directionZ,
                            minX, minY, minZ, maxX, maxY, maxZ,
                            maxDistance, currentHit)) {
            candidateHit = std::min(candidateHit, currentHit);
        }

        if (candidateHit < closestHit) {
            jobject newTarget = env->NewLocalRef(candidate.instance);
            if (newTarget) {
                if (selectedTarget)
                    env->DeleteLocalRef(selectedTarget);
                selectedTarget = newTarget;
                targetVisible = isVisible;
                closestHit = candidateHit;
            }
        }
    }

    for (auto& candidate : players) {
        if (candidate.instance) {
            env->DeleteLocalRef(candidate.instance);
            candidate.instance = nullptr;
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (selectedTarget) {
            env->DeleteLocalRef(selectedTarget);
            selectedTarget = nullptr;
        }
    }
    return selectedTarget;
}

bool Piercing::handleAttackInput() {
    bool targetVisible = false;
    jobject target = selectTarget(true, targetVisible);
    if (!target)
        return false;

    // LWJGL polls mouse state independently from the Windows message. Mark the
    // synchronous attack so its own swing/C02 packets pass, then guard against
    // Minecraft processing the same physical click again on the next tick.
    manualAttackActive_.store(true, std::memory_order_release);
    const bool attacked = mc.attackEntity(target);
    if (attacked) {
        suppressDuplicatePacketsUntilMs_.store(nowMs() + DUPLICATE_PACKET_GUARD_MS,
                                               std::memory_order_release);
        mc.setLeftClickCounter(2);
    }
    manualAttackActive_.store(false, std::memory_order_release);

    Java::Env()->DeleteLocalRef(target);
    return attacked;
}

bool Piercing::shouldSendOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!enabled || manualAttackActive_.load(std::memory_order_acquire))
        return true;

    const std::uint64_t deadline = suppressDuplicatePacketsUntilMs_.load(std::memory_order_acquire);
    if (deadline == 0 || nowMs() > deadline)
        return true;

    // A second vanilla pass emits another animation packet and another use-
    // entity attack. Suppress only those packet classes during this short,
    // attack-scoped window; movement and every unrelated packet still pass.
    return !isAttackOrSwingPacket(env, packet);
}

void Piercing::onUpdate() {
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    bool targetVisible = false;
    jobject target = selectTarget(false, targetVisible);
    if (!target)
        return;

    // Hidden targets are attacked synchronously from the input hook. Leaving
    // them in objectMouseOver would race the renderer and can duplicate hits.
    if (!targetVisible || !JniCache::fid_objectMouseOver || !JniCache::fid_typeOfHit ||
        !JniCache::fid_entityHit || !JniCache::cls_MovingObjectType || !JniCache::fid_ENTITY) {
        env->DeleteLocalRef(target);
        return;
    }

    jobject mouseOver = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) {
        env->DeleteLocalRef(target);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }

    if (target) {
        jobject entityType = env->GetStaticObjectField(JniCache::cls_MovingObjectType, JniCache::fid_ENTITY);
        if (entityType) {
            const bool typeReady = !env->ExceptionCheck();
            if (typeReady)
                env->SetObjectField(mouseOver, JniCache::fid_typeOfHit, entityType);

            if (typeReady && !env->ExceptionCheck())
                env->SetObjectField(mouseOver, JniCache::fid_entityHit, target);

            if (typeReady && !env->ExceptionCheck() &&
                JniCache::fid_entityRenderer && JniCache::fid_pointedEntity) {
                jobject renderer = env->GetObjectField(mc.instance, JniCache::fid_entityRenderer);
                if (renderer) {
                    if (!env->ExceptionCheck())
                        env->SetObjectField(renderer, JniCache::fid_pointedEntity, target);
                    env->DeleteLocalRef(renderer);
                }
            }

            env->DeleteLocalRef(entityType);
        }
    }

    env->DeleteLocalRef(target);
    env->DeleteLocalRef(mouseOver);
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void Piercing::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ImGui::Spacing();
            ui::SliderFloat("Visible Distance", &settings.distance, 3.0f, 6.0f, "%.2f");
            ui::Checkbox("Through Walls", &settings.throughWalls);
            if (settings.throughWalls) {
                ui::SliderFloat("Wall Distance", &settings.throughWallsRange, 3.0f, 6.0f, "%.2f");
            }
            ImGui::Spacing();
            ui::SliderFloat("Hitbox Expansion", &settings.expand, 0.0f, 1.0f, "%.2f");
            ui::Checkbox("Backtrack Check", &settings.backtrackCheck);
            ui::Checkbox("Weapons Only", &settings.weaponsOnly);
        },
        "Targets",
        [this]() {
            ui::Checkbox("Target Invisibles", &settings.targetInvisibles);
            ui::Checkbox("Ignore Friends", &settings.ignoreFriends);
            ui::Checkbox("Ignore Teams", &settings.ignoreTeams);
        });
}

std::string Piercing::getDisplayInfo() {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.2f", settings.distance);
    return buffer;
}



void Piercing::saveSettings(json::Value& node) const {
    node["distance"] = settings.distance;
    node["weaponsOnly"] = settings.weaponsOnly;
    node["targetInvisibles"] = settings.targetInvisibles;
    node["ignoreFriends"] = settings.ignoreFriends;
    node["ignoreTeams"] = settings.ignoreTeams;
    node["throughWalls"] = settings.throughWalls;
    node["throughWallsRange"] = settings.throughWallsRange;
    node["backtrackCheck"] = settings.backtrackCheck;
    node["expand"] = settings.expand;
}

void Piercing::loadSettings(const json::Value& node) {
    settings.distance = node["distance"].as_float(settings.distance);
    settings.weaponsOnly = node["weaponsOnly"].as_bool(settings.weaponsOnly);
    settings.targetInvisibles = node["targetInvisibles"].as_bool(settings.targetInvisibles);
    settings.ignoreFriends = node["ignoreFriends"].as_bool(settings.ignoreFriends);
    settings.ignoreTeams = node["ignoreTeams"].as_bool(settings.ignoreTeams);
    settings.throughWalls = node["throughWalls"].as_bool(settings.throughWalls);
    settings.throughWallsRange = node["throughWallsRange"].as_float(settings.throughWallsRange);
    settings.backtrackCheck = node["backtrackCheck"].as_bool(settings.backtrackCheck);
    settings.expand = node["expand"].as_float(settings.expand);
}
