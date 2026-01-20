#include "hitbox.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/team/friends_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cmath>

Hitbox* hitbox = nullptr;

static constexpr float PI = 3.14159265358979323846f;
static constexpr float VANILLA_REACH = 3.0f;
static constexpr float DEG_TO_RAD = PI / 180.0f;
static constexpr float VANILLA_HALF_WIDTH = 0.3f;
static constexpr float PLAYER_HEIGHT = 1.8f;

Hitbox::Hitbox() : Module("Hitbox") {
    keybind = 0;
    locked = true;
    renders = true;
}

void Hitbox::onEnable() {
    overrideState.needsRestore = false;
    overrideState.originalEntityHit = nullptr;

    std::lock_guard<std::mutex> lock(renderCacheMutex);
    renderCache.clear();
}

void Hitbox::onDisable() {
    JNIEnv* env = Java::Env();
    if (env) {
        restoreEntityHit(env);
    }

    std::lock_guard<std::mutex> lock(renderCacheMutex);
    renderCache.clear();
}

void Hitbox::restoreEntityHit(JNIEnv* env) {
    if (!overrideState.needsRestore)
        return;

    if (JniCache::fid_objectMouseOver) {
        jobject mouseOver = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
        if (mouseOver) {
            if (JniCache::fid_entityHit) {
                env->SetObjectField(mouseOver, JniCache::fid_entityHit, overrideState.originalEntityHit);
            }
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

// Proper Ray-AABB intersection (slab method)
bool Hitbox::rayIntersectsAABB(float ox, float oy, float oz, float dx, float dy, float dz, float minX, float minY,
                               float minZ, float maxX, float maxY, float maxZ, float maxDist) {
    float tmin = 0.0f;
    float tmax = maxDist;

    // X
    if (fabsf(dx) < 0.00001f) {
        if (ox < minX || ox > maxX)
            return false;
    } else {
        float invD = 1.0f / dx;
        float t1 = (minX - ox) * invD;
        float t2 = (maxX - ox) * invD;
        if (t1 > t2) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        tmin = t1 > tmin ? t1 : tmin;
        tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax)
            return false;
    }

    // Y
    if (fabsf(dy) < 0.00001f) {
        if (oy < minY || oy > maxY)
            return false;
    } else {
        float invD = 1.0f / dy;
        float t1 = (minY - oy) * invD;
        float t2 = (maxY - oy) * invD;
        if (t1 > t2) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        tmin = t1 > tmin ? t1 : tmin;
        tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax)
            return false;
    }

    // Z
    if (fabsf(dz) < 0.00001f) {
        if (oz < minZ || oz > maxZ)
            return false;
    } else {
        float invD = 1.0f / dz;
        float t1 = (minZ - oz) * invD;
        float t2 = (maxZ - oz) * invD;
        if (t1 > t2) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        tmin = t1 > tmin ? t1 : tmin;
        tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax)
            return false;
    }

    return true;
}

void Hitbox::onUpdate() {
    if (!enabled)
        return;

    lastUpdateTick = GetTickCount64();

    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    // Always restore at start of frame - we'll re-apply if needed
    restoreEntityHit(env);

    auto playerList = mc.getPlayerList();

    float localX = (float)player.getPosX();
    float localY = (float)player.getPosY() + 1.62f;  // Eye height
    float localZ = (float)player.getPosZ();

    float expandedHalfWidth = VANILLA_HALF_WIDTH + settings.expansion;

    // Build render cache for visualization
    if (settings.showHitbox) {
        RenderPos renderPos = mc.getRenderPos();
        float partialTicks = mc.getPartialTicks();

        std::vector<HitboxPlayerCache> newCache;

        for (auto& p : playerList) {
            if (!p.instance)
                continue;
            if (friendsManager && friendsManager->isFriendByUUID(p.uuid))
                continue;
            if (friendsManager && friendsManager->isApolloTeammate(p.uuid))
                continue;
            if (friendsManager && friendsManager->isLunarTeammate(p.uuid))
                continue;
            if (p.isInvisible || p.isNPC)
                continue;

            float dx = (float)p.posX - localX;
            float dy = (float)p.posY - (localY - 1.62f);
            float dz = (float)p.posZ - localZ;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);

            if (dist > VANILLA_REACH + 1.5f || dist < 0.5f)
                continue;

            HitboxPlayerCache cached;
            cached.interpX = (float)(p.lastPosX + (p.posX - p.lastPosX) * partialTicks);
            cached.interpY = (float)(p.lastPosY + (p.posY - p.lastPosY) * partialTicks);
            cached.interpZ = (float)(p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks);
            cached.valid = true;
            newCache.push_back(cached);
        }

        {
            std::lock_guard<std::mutex> lock(renderCacheMutex);
            renderCache = std::move(newCache);
            cachedRenderX = (float)renderPos.x;
            cachedRenderY = (float)renderPos.y;
            cachedRenderZ = (float)renderPos.z;
        }
    }

    // Always apply hitbox expansion - no click check needed
    // The override just means IF an attack happens, it will hit the target

    // Look direction
    float yaw = player.getYaw() * DEG_TO_RAD;
    float pitch = player.getPitch() * DEG_TO_RAD;

    float lookDirX = -sinf(yaw) * cosf(pitch);
    float lookDirY = -sinf(pitch);
    float lookDirZ = cosf(yaw) * cosf(pitch);

    // Find target using ray-AABB intersection
    PlayerInfo* target = nullptr;
    float closestDist = VANILLA_REACH + 0.5f;

    for (auto& p : playerList) {
        if (!p.instance)
            continue;
        if (friendsManager && friendsManager->isFriendByUUID(p.uuid))
            continue;
        if (friendsManager && friendsManager->isApolloTeammate(p.uuid))
            continue;
        if (friendsManager && friendsManager->isLunarTeammate(p.uuid))
            continue;
        if (p.isInvisible || p.isNPC)
            continue;

        float px = (float)p.posX;
        float py = (float)p.posY;
        float pz = (float)p.posZ;

        // Distance check
        float dx = px - localX;
        float dy = py - (localY - 1.62f);
        float dz = pz - localZ;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);

        if (dist > closestDist || dist < 0.5f)
            continue;

        // Expanded AABB
        float boxMinX = px - expandedHalfWidth;
        float boxMaxX = px + expandedHalfWidth;
        float boxMinY = py;
        float boxMaxY = py + PLAYER_HEIGHT;
        float boxMinZ = pz - expandedHalfWidth;
        float boxMaxZ = pz + expandedHalfWidth;

        // Ray-AABB intersection
        if (rayIntersectsAABB(localX, localY, localZ, lookDirX, lookDirY, lookDirZ, boxMinX, boxMinY, boxMinZ, boxMaxX,
                              boxMaxY, boxMaxZ, closestDist)) {
            closestDist = dist;
            target = &p;
        }
    }

    for (auto& p : playerList) {
        if (p.instance && (&p != target)) {
            env->DeleteLocalRef(p.instance);
            p.instance = nullptr;
        }
    }

    if (!target || !target->instance)
        return;

    if (!JniCache::fid_objectMouseOver || !JniCache::fid_entityHit) {
        env->DeleteLocalRef(target->instance);
        return;
    }

    jobject mouseOver = env->GetObjectField(mc.instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) {
        env->DeleteLocalRef(target->instance);
        return;
    }

    jobject originalEntity = env->GetObjectField(mouseOver, JniCache::fid_entityHit);
    overrideState.originalEntityHit = originalEntity ? env->NewLocalRef(originalEntity) : nullptr;
    if (originalEntity)
        env->DeleteLocalRef(originalEntity);

    if (JniCache::fid_typeOfHit) {
        jobject originalType = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
        overrideState.originalTypeOfHit = originalType ? env->NewLocalRef(originalType) : nullptr;
        if (originalType)
            env->DeleteLocalRef(originalType);
    }

    env->SetObjectField(mouseOver, JniCache::fid_entityHit, target->instance);

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
}

void Hitbox::onRender() {
    if (!enabled || !settings.showHitbox)
        return;
    if (!RenderData::updated)
        return;
    if (GetTickCount64() - lastUpdateTick > 300)
        return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw)
        return;

    std::vector<HitboxPlayerCache> localCache;
    float renderX, renderY, renderZ;
    {
        std::lock_guard<std::mutex> lock(renderCacheMutex);
        if (renderCache.empty())
            return;
        localCache = renderCache;
        renderX = cachedRenderX;
        renderY = cachedRenderY;
        renderZ = cachedRenderZ;
    }

    float expandedHalfWidth = VANILLA_HALF_WIDTH + settings.expansion;

    ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(settings.hitboxColor[0], settings.hitboxColor[1], settings.hitboxColor[2], settings.hitboxColor[3]));
    ImU32 outlineColor = IM_COL32(0, 0, 0, (int)(settings.hitboxColor[3] * 180));
    ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.hitboxColor[0], settings.hitboxColor[1],
                                                            settings.hitboxColor[2], settings.hitboxColor[3] * 0.3f));

    for (const auto& p : localCache) {
        if (!p.valid)
            continue;

        // 3D Box corners
        Vector3 corners[8] = {
            {p.interpX - expandedHalfWidth, p.interpY, p.interpZ - expandedHalfWidth},
            {p.interpX + expandedHalfWidth, p.interpY, p.interpZ - expandedHalfWidth},
            {p.interpX + expandedHalfWidth, p.interpY, p.interpZ + expandedHalfWidth},
            {p.interpX - expandedHalfWidth, p.interpY, p.interpZ + expandedHalfWidth},
            {p.interpX - expandedHalfWidth, p.interpY + PLAYER_HEIGHT, p.interpZ - expandedHalfWidth},
            {p.interpX + expandedHalfWidth, p.interpY + PLAYER_HEIGHT, p.interpZ - expandedHalfWidth},
            {p.interpX + expandedHalfWidth, p.interpY + PLAYER_HEIGHT, p.interpZ + expandedHalfWidth},
            {p.interpX - expandedHalfWidth, p.interpY + PLAYER_HEIGHT, p.interpZ + expandedHalfWidth},
        };

        Vector2 screenCorners[8];
        bool visible[8];
        int visibleCount = 0;

        for (int i = 0; i < 8; i++) {
            Vector3 v = {corners[i].x - renderX, corners[i].y - renderY, corners[i].z - renderZ};
            visible[i] =
                CWorldToScreen::WorldToScreen(v, RenderData::modelView, RenderData::projection, RenderData::screenWidth,
                                              RenderData::screenHeight, screenCorners[i]);
            if (visible[i])
                visibleCount++;
        }

        if (visibleCount < 3)
            continue;

        // Faces for filled rendering
        if (settings.filled) {
            static constexpr int faces[6][4] = {{0, 1, 2, 3}, {7, 6, 5, 4}, {3, 2, 6, 7},
                                                {1, 0, 4, 5}, {0, 3, 7, 4}, {2, 1, 5, 6}};

            for (int f = 0; f < 6; f++) {
                int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2], i3 = faces[f][3];
                if (!visible[i0] || !visible[i1] || !visible[i2] || !visible[i3])
                    continue;

                // Back-face culling
                float ax = screenCorners[i1].x - screenCorners[i0].x;
                float ay = screenCorners[i1].y - screenCorners[i0].y;
                float bx = screenCorners[i2].x - screenCorners[i0].x;
                float by = screenCorners[i2].y - screenCorners[i0].y;
                if (ax * by - ay * bx > 0) {
                    ImVec2 points[4] = {
                        ImVec2(screenCorners[i0].x, screenCorners[i0].y),
                        ImVec2(screenCorners[i1].x, screenCorners[i1].y),
                        ImVec2(screenCorners[i2].x, screenCorners[i2].y),
                        ImVec2(screenCorners[i3].x, screenCorners[i3].y),
                    };
                    draw->AddConvexPolyFilled(points, 4, fillColor);
                }
            }
        }

        // Edges
        static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                             {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

        for (int i = 0; i < 12; i++) {
            int a = edges[i][0], b = edges[i][1];
            if (visible[a] && visible[b]) {
                draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y),
                              ImVec2(screenCorners[b].x, screenCorners[b].y), outlineColor, 3.0f);
            }
        }
        for (int i = 0; i < 12; i++) {
            int a = edges[i][0], b = edges[i][1];
            if (visible[a] && visible[b]) {
                draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y),
                              ImVec2(screenCorners[b].x, screenCorners[b].y), boxColor, 1.5f);
            }
        }
    }
}

void Hitbox::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(ui::s(100));
            ui::SliderFloat("Expansion", &settings.expansion, 0.0f, 1.0f, "%.2f", false);
        },
        "Visual",
        [this]() {
            ui::Checkbox("Show Hitbox", &settings.showHitbox);
            if (settings.showHitbox) {
                ui::Checkbox("Filled", &settings.filled);
                ui::color("Box Color", settings.hitboxColor);
            }
        });
}

std::string Hitbox::getDisplayInfo() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", settings.expansion);
    return buf;
}
