#include "reach.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/team/friends_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>

Reach* reach = nullptr;

static constexpr float PI = 3.14159265358979323846f;

Reach::Reach() : Module("Reach") {
    keybind = 0;
}

void Reach::onEnable() {
}

void Reach::onDisable() {
}

void Reach::onUpdate() {
    if (!enabled) return;
    if (Menu::isOpen) return;
    if (mc.isInGui()) return;
    if (!player.isValid()) return;
    if (!mc.instance) return;
    
    JNIEnv* env = Java::Env();
    if (!env) return;
    
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    
    // Get player list
    auto playerList = mc.getPlayerList();
    if (playerList.empty()) return;
    
    // Local player position
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();
    
    // Find closest target within 6 blocks
    PlayerInfo* target = nullptr;
    float finalDist = 9999.0f;
    
    for (auto& p : playerList) {
        if (!p.instance) continue;
        
        // Skip self check is handled by playerList
        
        // Skip invisible
        if (p.isInvisible) continue;
        
        // Skip NPCs
        if (p.isNPC) continue;
        
        // Skip friends
        if (friendsManager && friendsManager->isFriendByUUID(p.uuid)) continue;
        if (friendsManager && friendsManager->isApolloTeammate(p.uuid)) continue;
        if (friendsManager && friendsManager->isLunarTeammate(p.uuid)) continue;
        
        // Calculate distance
        double dx = localX - p.posX;
        double dy = localY - p.posY;
        double dz = localZ - p.posZ;
        float dist = (float)sqrt(dx * dx + dy * dy + dz * dz);
        
        if (dist <= finalDist && dist <= 6.0f) {
            finalDist = dist;
            target = &p;
        }
    }
    
    // Cleanup all instances we don't need
    for (auto& p : playerList) {
        if (p.instance && (&p != target)) {
            env->DeleteLocalRef(p.instance);
            p.instance = nullptr;
        }
    }
    
    if (!target || !target->instance) return;
    
    // Check distance conditions (only apply for players beyond vanilla reach but within our reach)
    float distBetween = finalDist;
    if (distBetween > settings.distance + 0.5f || distBetween <= 3.0f) {
        env->DeleteLocalRef(target->instance);
        return;
    }
    
    // Knight reach algorithm: move bounding box towards local player
    float targetX = (float)target->posX;
    float targetZ = (float)target->posZ;
    
    float hypothenuseDistance = hypotf((float)(localX - target->posX), (float)(localZ - target->posZ));
    float dist = settings.distance - 3.0f;
    
    // Adjust distance calculation
    while (distBetween > 3.0f && (distBetween < (dist + 3.0f)) && dist > 0.05f) {
        dist -= 0.05f;
    }
    
    if (hypothenuseDistance < dist) {
        dist -= hypothenuseDistance;
    }
    
    // Calculate angle to target
    float angleX = targetX - (float)localX;
    float angleZ = targetZ - (float)localZ;
    
    float angle = (float)(-atan2f(angleX, angleZ) * 180.0f / PI);
    if (angleZ < 0.0f && angleX < 0.0f) {
        angle = 90.0f + (float)(atan2f(angleZ, angleX) * 180.0f / PI);
    }
    else if (angleZ < 0.0f && angleX > 0.0f) {
        angle = -90.0f + (float)(atan2f(angleZ, angleX) * 180.0f / PI);
    }
    
    // Calculate new position
    float ax = cosf((angle + 90.0f) * PI / 180.0f);
    float az = sinf((angle + 90.0f) * PI / 180.0f);
    
    float newX = targetX - ax * dist;
    float newZ = targetZ - az * dist;
    
    // Get the bounding box of the target entity
    if (!JniCache::fid_boundingBox) {
        env->DeleteLocalRef(target->instance);
        return;
    }
    
    jobject aabb = env->GetObjectField(target->instance, JniCache::fid_boundingBox);
    if (!aabb) {
        env->DeleteLocalRef(target->instance);
        return;
    }
    
    // Read current bounding box values
    if (!JniCache::fid_aabb_minX || !JniCache::fid_aabb_maxX) {
        env->DeleteLocalRef(aabb);
        env->DeleteLocalRef(target->instance);
        return;
    }
    
    double curMinY = env->GetDoubleField(aabb, JniCache::fid_aabb_minY);
    double curMaxY = env->GetDoubleField(aabb, JniCache::fid_aabb_maxY);
    
    // Entity width is 0.6, half width is 0.3
    float newWidth = 0.6f / 2.0f;
    
    // Set new bounding box (moved towards player)
    env->SetDoubleField(aabb, JniCache::fid_aabb_minX, (double)(newX - newWidth));
    env->SetDoubleField(aabb, JniCache::fid_aabb_minZ, (double)(newZ - newWidth));
    env->SetDoubleField(aabb, JniCache::fid_aabb_maxX, (double)(newX + newWidth));
    env->SetDoubleField(aabb, JniCache::fid_aabb_maxZ, (double)(newZ + newWidth));
    // Keep Y values unchanged
    env->SetDoubleField(aabb, JniCache::fid_aabb_minY, curMinY);
    env->SetDoubleField(aabb, JniCache::fid_aabb_maxY, curMaxY);
    
    env->DeleteLocalRef(aabb);
    env->DeleteLocalRef(target->instance);
    
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void Reach::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();
            
            ui::SliderFloat("Distance", &settings.distance, 3.0f, 6.0f, "%.1f");
        },
        "Info",
        [this]() {
            ImGui::TextWrapped("Extends attack reach by moving target hitboxes closer.");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Only affects players beyond 3.0 blocks.");
        }
    );
}

std::string Reach::getDisplayInfo() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", settings.distance);
    return buf;
}
