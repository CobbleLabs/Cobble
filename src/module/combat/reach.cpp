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
#include <random>
#include <cmath>

Reach* reach = nullptr;

static std::mt19937 rng(std::random_device{}());

// Constants
static constexpr float PI = 3.14159265358979323846f;
static constexpr float VANILLA_REACH = 3.0f;
static constexpr float PLAYER_WIDTH = 0.6f;

Reach::Reach() : Module("Reach")
{
	keybind = 0;
	locked = true;
}

void Reach::onEnable()
{
	updateReach();
	consecutiveExtendedHits = 0;
	lastHitTime = 0;
	originalHitbox.valid = false;
}

void Reach::onDisable()
{
	JNIEnv* env = Java::GetEnv();
	if (env) {
		restoreHitbox(env);
	}
	consecutiveExtendedHits = 0;
}

bool Reach::isHoldingWeapon()
{
	int itemId = player.getHeldItemId();
	return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
			itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

void Reach::updateReach()
{
	if (settings.randomize) {
		std::uniform_real_distribution<float> dist(settings.minDistance, settings.maxDistance);
		currentReach = dist(rng);
	} else {
		currentReach = settings.distance;
	}
}

bool Reach::shouldApplyThisSwing()
{
	std::uniform_int_distribution<int> chanceDist(1, 100);
	return chanceDist(rng) <= settings.hitChance;
}

void Reach::restoreHitbox(JNIEnv* env)
{
	if (!originalHitbox.valid || !originalHitbox.entityRef) return;
	
	if (JniCache::fid_boundingBox && JniCache::cls_AxisAlignedBB) {
		jobject aabb = env->GetObjectField(originalHitbox.entityRef, JniCache::fid_boundingBox);
		if (aabb) {
			if (JniCache::fid_aabb_minX) env->SetDoubleField(aabb, JniCache::fid_aabb_minX, originalHitbox.minX);
			if (JniCache::fid_aabb_maxX) env->SetDoubleField(aabb, JniCache::fid_aabb_maxX, originalHitbox.maxX);
			if (JniCache::fid_aabb_minY) env->SetDoubleField(aabb, JniCache::fid_aabb_minY, originalHitbox.minY);
			if (JniCache::fid_aabb_maxY) env->SetDoubleField(aabb, JniCache::fid_aabb_maxY, originalHitbox.maxY);
			if (JniCache::fid_aabb_minZ) env->SetDoubleField(aabb, JniCache::fid_aabb_minZ, originalHitbox.minZ);
			if (JniCache::fid_aabb_maxZ) env->SetDoubleField(aabb, JniCache::fid_aabb_maxZ, originalHitbox.maxZ);
			env->DeleteLocalRef(aabb);
		}
		env->DeleteLocalRef(originalHitbox.entityRef);
	}
	
	originalHitbox.entityRef = nullptr;
	originalHitbox.valid = false;
}

void Reach::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;
	
	JNIEnv* env = Java::GetEnv();
	if (!env) return;
	
	// Always restore previous hitbox first
	restoreHitbox(env);
	
	// Check conditions
	if (settings.weaponsOnly && !isHoldingWeapon()) return;
	if (settings.sprintingOnly && !player.isSprinting()) return;
	if (settings.movingOnly && !(GetAsyncKeyState('W') & 0x8000)) return;
	
	// Only when attacking (left click held)
	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
	
	ULONGLONG now = GetTickCount64();
	
	// Reset consecutive hits after timeout
	if (now - lastHitTime > 2000) {
		consecutiveExtendedHits = 0;
	}
	
	// Force vanilla after too many consecutive extended hits
	if (consecutiveExtendedHits >= settings.maxConsecutiveHits) {
		consecutiveExtendedHits = 0;
		return;
	}
	
	// Random chance to skip (anti-pattern)
	if (!shouldApplyThisSwing()) return;
	
	// Get player list
	auto playerList = mc.getPlayerList();
	if (playerList.empty()) return;
	
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
		if (!p.instance) continue;
		
		// Skip friends/teammates
		if (friendsManager && friendsManager->isFriendByUUID(p.uuid)) continue;
		if (friendsManager && friendsManager->isApolloTeammate(p.uuid)) continue;
		if (friendsManager && friendsManager->isLunarTeammate(p.uuid)) continue;
		
		// Skip invisible/NPCs
		if (p.isInvisible) continue;
		if (p.isNPC) continue;
		
		// Calculate distance
		double dx = localX - p.posX;
		double dy = localY - p.posY;
		double dz = localZ - p.posZ;
		float dist = sqrtf((float)(dx*dx + dy*dy + dz*dz));
		
		// Only extend for targets beyond vanilla but within our reach
		if (dist > VANILLA_REACH && dist < closestDist) {
			// Check if target is moving toward us (more legitimate hit)
			if (settings.onlyMovingTargets) {
				double prevDx = localX - p.lastPosX;
				double prevDz = localZ - p.lastPosZ;
				double currDx = localX - p.posX;
				double currDz = localZ - p.posZ;
				float prevDist = sqrtf((float)(prevDx*prevDx + prevDz*prevDz));
				float currDist = sqrtf((float)(currDx*currDx + currDz*currDz));
				
				// Skip if moving away
				if (currDist > prevDist + 0.01f) continue;
			}
			
			closestDist = dist;
			target = &p;
		}
	}
	
	// Cleanup local refs
	for (auto& p : playerList) {
		if (p.instance && (&p != target)) {
			env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}
	
	if (!target || !target->instance) return;
	
	// Calculate minimal extension needed (just enough to hit)
	float neededExtension = closestDist - VANILLA_REACH + 0.05f;
	float maxExtension = currentReach - VANILLA_REACH;
	float extension = (neededExtension < maxExtension) ? neededExtension : maxExtension;
	
	// Add small random variance
	std::uniform_real_distribution<float> varianceDist(-0.01f, 0.01f);
	extension += varianceDist(rng);
	if (extension < 0.02f) extension = 0.02f;
	
	// Direction from target to player (full 3D)
	float dx = (float)(localX - target->posX);
	float dy = (float)(localY - target->posY);
	float dz = (float)(localZ - target->posZ);
	float totalDist = sqrtf(dx*dx + dy*dy + dz*dz);
	
	if (totalDist < 0.01f) {
		env->DeleteLocalRef(target->instance);
		return;
	}
	
	// Normalize direction (3D)
	float dirX = dx / totalDist;
	float dirY = dy / totalDist;
	float dirZ = dz / totalDist;
	
	// Get and modify bounding box
	if (JniCache::fid_boundingBox && JniCache::cls_AxisAlignedBB) {
		jobject aabb = env->GetObjectField(target->instance, JniCache::fid_boundingBox);
		if (aabb) {
			// Save original
			originalHitbox.minX = env->GetDoubleField(aabb, JniCache::fid_aabb_minX);
			originalHitbox.maxX = env->GetDoubleField(aabb, JniCache::fid_aabb_maxX);
			originalHitbox.minY = env->GetDoubleField(aabb, JniCache::fid_aabb_minY);
			originalHitbox.maxY = env->GetDoubleField(aabb, JniCache::fid_aabb_maxY);
			originalHitbox.minZ = env->GetDoubleField(aabb, JniCache::fid_aabb_minZ);
			originalHitbox.maxZ = env->GetDoubleField(aabb, JniCache::fid_aabb_maxZ);
			originalHitbox.entityRef = env->NewLocalRef(target->instance);
			originalHitbox.valid = true;
			
			// Extend toward player (all 3 axes)
			double newMinX = originalHitbox.minX;
			double newMaxX = originalHitbox.maxX;
			double newMinY = originalHitbox.minY;
			double newMaxY = originalHitbox.maxY;
			double newMinZ = originalHitbox.minZ;
			double newMaxZ = originalHitbox.maxZ;
			
			if (dirX > 0) newMaxX += extension * dirX;
			else newMinX += extension * dirX;
			
			if (dirY > 0) newMaxY += extension * dirY;
			else newMinY += extension * dirY;
			
			if (dirZ > 0) newMaxZ += extension * dirZ;
			else newMinZ += extension * dirZ;
			
			// Apply all 6 sides
			if (JniCache::fid_aabb_minX) env->SetDoubleField(aabb, JniCache::fid_aabb_minX, newMinX);
			if (JniCache::fid_aabb_maxX) env->SetDoubleField(aabb, JniCache::fid_aabb_maxX, newMaxX);
			if (JniCache::fid_aabb_minY) env->SetDoubleField(aabb, JniCache::fid_aabb_minY, newMinY);
			if (JniCache::fid_aabb_maxY) env->SetDoubleField(aabb, JniCache::fid_aabb_maxY, newMaxY);
			if (JniCache::fid_aabb_minZ) env->SetDoubleField(aabb, JniCache::fid_aabb_minZ, newMinZ);
			if (JniCache::fid_aabb_maxZ) env->SetDoubleField(aabb, JniCache::fid_aabb_maxZ, newMaxZ);
			
			env->DeleteLocalRef(aabb);
			
			consecutiveExtendedHits++;
			lastHitTime = now;
		}
	}
	
	env->DeleteLocalRef(target->instance);
}

void Reach::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::Checkbox("Randomize", &settings.randomize);
			if (settings.randomize) {
				ui::SliderFloat("Min Distance", &settings.minDistance, 3.0f, 6.0f, "%.2f", true);
				ui::SliderFloat("Max Distance", &settings.maxDistance, 3.0f, 6.0f, "%.2f", true);
			} else {
				ui::SliderFloat("Distance", &settings.distance, 3.0f, 6.0f, "%.2f", true);
			}
		},
		"Anti-Detection", [this]() {
			ui::SliderInt("Randomization", &settings.hitChance, 0, 100, "%d%%");
			ui::SliderInt("Max Consecutive", &settings.maxConsecutiveHits, 1, 10, "%d");
			ui::Checkbox("Only Moving Targets", &settings.onlyMovingTargets);
			ImGui::Spacing();
			ui::Checkbox("Weapons Only", &settings.weaponsOnly);
			ui::Checkbox("Sprinting Only", &settings.sprintingOnly);
			ui::Checkbox("Moving Only", &settings.movingOnly);
		}
	);
}

std::string Reach::getDisplayInfo() {
	char buf[16];
	if (settings.randomize) {
		snprintf(buf, sizeof(buf), "%.2f-%.2f", settings.minDistance, settings.maxDistance);
	} else {
		snprintf(buf, sizeof(buf), "%.2f", settings.distance);
	}
	return buf;
}
