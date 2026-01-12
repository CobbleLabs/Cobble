#include "reach.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/team/friends_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cmath>
#include <random>

Reach* reach = nullptr;

static std::mt19937 rng(std::random_device{}());

// Constants
static constexpr float PI = 3.14159265358979323846f;
static constexpr float VANILLA_REACH = 3.0f;
static constexpr float DEG_TO_RAD = PI / 180.0f;
static constexpr float RAD_TO_DEG = 180.0f / PI;

Reach::Reach() : Module("Reach") {
    keybind = 0;
}

void Reach::onEnable() {
    updateReach();
    consecutiveExtendedHits = 0;
    lastHitTime = 0;
    overrideState.needsRestore = false;
    overrideState.originalEntityHit = nullptr;
    wasClickHeld = false;
    currentClickAllowed = false;
}

void Reach::onDisable() {
    JNIEnv* env = Java::GetEnv();
    if (env) {
        restoreEntityHit(env);
    }
    consecutiveExtendedHits = 0;
}

bool Reach::isHoldingWeapon() {
    int itemId = player.getHeldItemId();
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 || itemId == 271 ||
            itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

void Reach::updateReach() {
    if (settings.intervalMode) {
        // Ensure min <= max
        if (settings.minDistance > settings.maxDistance) {
            settings.minDistance = settings.maxDistance;
        }
        std::uniform_real_distribution<float> dist(settings.minDistance, settings.maxDistance);
        currentReach = dist(rng);
    } else {
        currentReach = settings.distance;
    }
}

bool Reach::shouldApplyThisSwing() {
    std::uniform_int_distribution<int> chanceDist(1, 100);
    return chanceDist(rng) <= settings.hitChance;
}

void Reach::restoreEntityHit(JNIEnv* env) {
    if (!overrideState.needsRestore)
        return;

    if (JniCache::fid_objectMouseOver) {
        jobject mouseOver = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
        if (mouseOver) {
            // Restore original entityHit (can be nullptr)
            if (JniCache::fid_entityHit) {
                env->SetObjectField(mouseOver, JniCache::fid_entityHit, overrideState.originalEntityHit);
            }
            // Restore original typeOfHit
            if (JniCache::fid_typeOfHit && overrideState.originalTypeOfHit) {
                env->SetObjectField(mouseOver, JniCache::fid_typeOfHit, overrideState.originalTypeOfHit);
            }
            env->DeleteLocalRef(mouseOver);
        }
    }

    if (overrideState.originalEntityHit) {
        env->DeleteLocalRef(overrideState.originalEntityHit);
        overrideState.originalEntityHit = nullptr;
    }
    if (overrideState.originalTypeOfHit) {
        env->DeleteLocalRef(overrideState.originalTypeOfHit);
        overrideState.originalTypeOfHit = nullptr;
    }

    overrideState.needsRestore = false;
}

void Reach::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    JNIEnv* env = Java::GetEnv();
    if (!env)
        return;

    // Always restore previous override at start of frame
    restoreEntityHit(env);

    // Check conditions
    if (settings.weaponsOnly && !isHoldingWeapon())
        return;
    if (settings.sprintingOnly && !player.isSprinting())
        return;
    if (settings.movingOnly && !(GetAsyncKeyState('W') & 0x8000))
        return;

    // Only when attacking (left click held)
    bool clickHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // Track click state - evaluate hit chance only on NEW click
    if (!clickHeld) {
        wasClickHeld = false;
        return;
    }

    // New click started - roll hit chance once
    if (!wasClickHeld) {
        wasClickHeld = true;
        currentClickAllowed = shouldApplyThisSwing();
    }

    // If this click was not allowed by hit chance, skip
    if (!currentClickAllowed)
        return;

    ULONGLONG now = GetTickCount64();

    // Reset consecutive hits after timeout
    if (now - lastHitTime > 2000) {
        consecutiveExtendedHits = 0;
    }

    // Force vanilla after too many consecutive extended hits (0 = disabled)
    if (settings.maxConsecutiveHits > 0 && consecutiveExtendedHits >= settings.maxConsecutiveHits) {
        consecutiveExtendedHits = 0;
        return;
    }

    // Check if already targeting an entity with vanilla raycast
    // If so, we don't need to do anything
    if (mc.isTargetingEntity())
        return;

    // Get player list
    auto playerList = mc.getPlayerList();
    if (playerList.empty())
        return;

    // Local player position
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();

    // Update reach value
    updateReach();

    // Find closest target within our reach range but beyond vanilla
    PlayerInfo* target = nullptr;
    float closestDist = currentReach + 0.3f;

    for (auto& p : playerList) {
        if (!p.instance)
            continue;

        // Skip friends/teammates
        if (friendsManager && friendsManager->isFriendByUUID(p.uuid))
            continue;
        if (friendsManager && friendsManager->isApolloTeammate(p.uuid))
            continue;
        if (friendsManager && friendsManager->isLunarTeammate(p.uuid))
            continue;

        // Skip invisible/NPCs
        if (p.isInvisible)
            continue;
        if (p.isNPC)
            continue;

        // Calculate distance
        double dx = localX - p.posX;
        double dy = localY - p.posY;
        double dz = localZ - p.posZ;
        float dist = sqrtf((float)(dx * dx + dy * dy + dz * dz));

        // Only extend for targets beyond vanilla but within our reach
        if (dist > VANILLA_REACH && dist < closestDist) {
            // Check if target is moving toward us (more legitimate hit)
            if (settings.onlyMovingTargets) {
                double prevDx = localX - p.lastPosX;
                double prevDz = localZ - p.lastPosZ;
                double currDx = localX - p.posX;
                double currDz = localZ - p.posZ;
                float prevDist = sqrtf((float)(prevDx * prevDx + prevDz * prevDz));
                float currDist = sqrtf((float)(currDx * currDx + currDz * currDz));

                // Skip if moving away
                if (currDist > prevDist + 0.01f)
                    continue;
            }

            closestDist = dist;
            target = &p;
        }
    }

    // Cleanup local refs for non-target entities
    for (auto& p : playerList) {
        if (p.instance && (&p != target)) {
            env->DeleteLocalRef(p.instance);
            p.instance = nullptr;
        }
    }

    if (!target || !target->instance)
        return;

    // Get objectMouseOver
    if (!JniCache::fid_objectMouseOver || !JniCache::fid_entityHit) {
        env->DeleteLocalRef(target->instance);
        return;
    }

    jobject mouseOver = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) {
        env->DeleteLocalRef(target->instance);
        return;
    }

    // Save original entityHit
    jobject originalEntity = env->GetObjectField(mouseOver, JniCache::fid_entityHit);
    overrideState.originalEntityHit = originalEntity ? env->NewLocalRef(originalEntity) : nullptr;
    if (originalEntity)
        env->DeleteLocalRef(originalEntity);

    // Save original typeOfHit
    if (JniCache::fid_typeOfHit) {
        jobject originalType = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
        overrideState.originalTypeOfHit = originalType ? env->NewLocalRef(originalType) : nullptr;
        if (originalType)
            env->DeleteLocalRef(originalType);
    }

    // Override entityHit with our target
    env->SetObjectField(mouseOver, JniCache::fid_entityHit, target->instance);

    // Override typeOfHit to ENTITY so Minecraft processes the attack
    if (JniCache::fid_typeOfHit && JniCache::fid_ENTITY && JniCache::cls_MovingObjectType) {
        jobject entityType = env->GetStaticObjectField(JniCache::cls_MovingObjectType, JniCache::fid_ENTITY);
        if (entityType) {
            env->SetObjectField(mouseOver, JniCache::fid_typeOfHit, entityType);
            env->DeleteLocalRef(entityType);
        }
    }

    overrideState.needsRestore = true;

    env->DeleteLocalRef(mouseOver);
    env->DeleteLocalRef(target->instance);

    consecutiveExtendedHits++;
    lastHitTime = now;
}

void Reach::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();
            ui::Checkbox("Interval Mode", &settings.intervalMode);
            if (settings.intervalMode) {
                ui::SliderFloat("Min Distance", &settings.minDistance, 3.0f, 6.0f, "%.2f", false);
                ui::SliderFloat("Max Distance", &settings.maxDistance, 3.0f, 6.0f, "%.2f", false);
                // Enforce min <= max
                if (settings.minDistance > settings.maxDistance) {
                    settings.minDistance = settings.maxDistance;
                }
            } else {
                ui::SliderFloat("Distance", &settings.distance, 3.0f, 6.0f, "%.2f", false);
            }
            ui::SliderInt("Hit Chance", &settings.hitChance, 0, 100, "%d%%");
        },
        "Anti-Detection",
        [this]() {
            ui::SliderInt("Max Consecutive", &settings.maxConsecutiveHits, 0, 10,
                          settings.maxConsecutiveHits == 0 ? "Off" : "%d");
            ui::Checkbox("Only Moving Targets", &settings.onlyMovingTargets);
            ImGui::Spacing();
            ui::Checkbox("Weapons Only", &settings.weaponsOnly);
            ui::Checkbox("Sprinting Only", &settings.sprintingOnly);
            ui::Checkbox("Moving Only", &settings.movingOnly);
        });
}

std::string Reach::getDisplayInfo() {
    char buf[24];
    if (settings.intervalMode) {
        snprintf(buf, sizeof(buf), "%.2f-%.2f", settings.minDistance, settings.maxDistance);
    } else {
        snprintf(buf, sizeof(buf), "%.2f", settings.distance);
    }
    return buf;
}
