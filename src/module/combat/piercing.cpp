#include "piercing.h"

#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/team/friends_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

Piercing* piercing = nullptr;

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double EPSILON = 1.0e-8;

bool isWeapon(int itemId) {
    return itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
           itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286;
}
}  // namespace

Piercing::Piercing() : Module("Piercing") {
    keybind = 0;
    locked = true;
}

void Piercing::onEnable() {
}

void Piercing::onDisable() {
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

void Piercing::onUpdate() {
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    if (!enabled || Menu::isOpen || mc.isInGui() || !player.isValid() || !mc.instance) {
        return;
    }

    if ((settings.weaponsOnly && !isWeapon(player.getHeldItemId())) ||
        !JniCache::fid_objectMouseOver || !JniCache::fid_typeOfHit || !JniCache::fid_entityHit ||
        !JniCache::cls_MovingObjectType || !JniCache::fid_ENTITY) {
        return;
    }

    if (env->ExceptionCheck())
        env->ExceptionClear();

    jobject mouseOver = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return;
    }

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
    PlayerInfo* target = nullptr;
    double closestHit = std::numeric_limits<double>::max();

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

        double hitDistance = 0.0;
        if (rayIntersectsBox(originX, originY, originZ,
                             directionX, directionY, directionZ,
                             minX, minY, minZ, maxX, maxY, maxZ,
                             settings.distance, hitDistance) &&
            hitDistance < closestHit) {
            closestHit = hitDistance;
            target = &candidate;
        }
    }

    if (target && target->instance) {
        jobject entityType = env->GetStaticObjectField(JniCache::cls_MovingObjectType, JniCache::fid_ENTITY);
        if (entityType) {
            const bool typeReady = !env->ExceptionCheck();
            if (typeReady)
                env->SetObjectField(mouseOver, JniCache::fid_typeOfHit, entityType);

            if (typeReady && !env->ExceptionCheck())
                env->SetObjectField(mouseOver, JniCache::fid_entityHit, target->instance);

            if (typeReady && !env->ExceptionCheck() &&
                JniCache::fid_entityRenderer && JniCache::fid_pointedEntity) {
                jobject renderer = env->GetObjectField(mc.instance, JniCache::fid_entityRenderer);
                if (renderer) {
                    if (!env->ExceptionCheck())
                        env->SetObjectField(renderer, JniCache::fid_pointedEntity, target->instance);
                    env->DeleteLocalRef(renderer);
                }
            }

            env->DeleteLocalRef(entityType);
        }
    }

    for (auto& candidate : players) {
        if (candidate.instance) {
            env->DeleteLocalRef(candidate.instance);
            candidate.instance = nullptr;
        }
    }

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
            ui::SliderFloat("Distance", &settings.distance, 3.0f, 6.0f, "%.2f");
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
