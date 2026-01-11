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
static constexpr ULONGLONG SWING_DEBOUNCE_MS = 50;    // Minimum ms between reach applications
static constexpr ULONGLONG HIT_RESET_MS = 2000;       // Reset consecutive hits after 2s without hitting

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
	lastSwingTime = 0;
	originalHitbox.valid = false;
}

void Reach::onDisable()
{
	// Restore any modified hitbox on disable
	JNIEnv* env = Java::GetEnv();
	if (env) {
		restoreHitbox(env);
	}
	consecutiveExtendedHits = 0;
}

bool Reach::isHoldingWeapon()
{
	int itemId = player.getHeldItemId();
	// Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
	// Axes: 271, 275, 258, 279, 286
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
	// Random chance to skip (breaks detection patterns)
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
			if (JniCache::fid_aabb_minZ) env->SetDoubleField(aabb, JniCache::fid_aabb_minZ, originalHitbox.minZ);
			if (JniCache::fid_aabb_maxZ) env->SetDoubleField(aabb, JniCache::fid_aabb_maxZ, originalHitbox.maxZ);
			env->DeleteLocalRef(aabb);
		}
	}
	
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
	
	// Restore previous hitbox modification (important: do this early)
	restoreHitbox(env);
	
	// Check conditions
	if (settings.weaponsOnly && !isHoldingWeapon()) return;
	if (settings.sprintingOnly && !player.isSprinting()) return;
	if (settings.movingOnly && !(GetAsyncKeyState('W') & 0x8000)) return;
	
	// Only process when left clicking
	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
	
	// Debounce rapid swings
	ULONGLONG now = GetTickCount64();
	if (now - lastSwingTime < SWING_DEBOUNCE_MS) return;
	lastSwingTime = now;
	
	// Reset consecutive hit counter if too much time passed
	if (now - lastHitTime > HIT_RESET_MS) {
		consecutiveExtendedHits = 0;
	}
	
	// Force vanilla reach after too many consecutive extended hits (anti-pattern)
	if (consecutiveExtendedHits >= settings.maxConsecutiveHits) {
		consecutiveExtendedHits = 0;  // Reset for next cycle
		return;  // Skip this hit - use vanilla reach
	}
	
	// Random chance to skip (breaks patterns)
	if (!shouldApplyThisSwing()) return;
	
	// Get player list
	auto playerList = mc.getPlayerList();
	if (playerList.empty()) return;
	
	// Local player position
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();
	
	// Find closest visible target within 6 blocks
	PlayerInfo* target = nullptr;
	float closestDist = 6.0f;
	
	for (auto& p : playerList) {
		if (!p.instance) continue;
		
		// Skip friends/teammates
		if (friendsManager && friendsManager->isFriendByUUID(p.uuid)) continue;
		if (friendsManager && friendsManager->isApolloTeammate(p.uuid)) continue;
		if (friendsManager && friendsManager->isLunarTeammate(p.uuid)) continue;
		
		// Skip invisible players
		if (p.isInvisible) continue;
		
		// Skip NPCs
		if (p.isNPC) continue;
		
		// Check visibility
		if (!mc.canEntityBeSeen(p.instance)) continue;
		
		// Only apply to targets moving toward player (more plausible hits)
		if (settings.onlyMovingTargets) {
			// Check if target is moving toward us by comparing last pos
			double prevDx = localX - p.lastPosX;
			double prevDz = localZ - p.lastPosZ;
			double currDx = localX - p.posX;
			double currDz = localZ - p.posZ;
			float prevDist = sqrtf((float)(prevDx*prevDx + prevDz*prevDz));
			float currDist = sqrtf((float)(currDx*currDx + currDz*currDz));
			
			// Skip if target is moving away (distance increasing)
			if (currDist > prevDist + 0.01f) continue;
		}
		
		// Calculate distance
		double dx = localX - p.posX;
		double dy = localY - p.posY;
		double dz = localZ - p.posZ;
		float dist = sqrtf((float)(dx*dx + dy*dy + dz*dz));
		
		if (dist < closestDist) {
			closestDist = dist;
			target = &p;
		}
	}
	
	// Cleanup local refs from player list (except target if we have one)
	for (auto& p : playerList) {
		if (p.instance && (&p != target)) {
			env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}
	
	if (!target || !target->instance) return;
	
	// Update reach value with randomization
	updateReach();
	
	// Only extend reach if target is between vanilla reach and our reach
	// Tighter window: don't extend if too far (more obvious) or already in range (not needed)
	float maxEffectiveReach = currentReach + 0.3f;  // Smaller buffer than before
	if (closestDist <= VANILLA_REACH || closestDist > maxEffectiveReach) {
		env->DeleteLocalRef(target->instance);
		return;
	}
	
	// Calculate exact extension needed (just enough to hit, not more)
	// This is key: extend ONLY what's needed, not the full reach
	float neededExtension = closestDist - VANILLA_REACH + 0.05f;  // Small buffer for server-side variance
	float maxExtension = currentReach - VANILLA_REACH;
	float reachExtension = (neededExtension < maxExtension) ? neededExtension : maxExtension;
	
	// Add micro-variance to the extension (breaks exact patterns)
	std::uniform_real_distribution<float> varianceDist(-0.02f, 0.02f);
	reachExtension += varianceDist(rng);
	if (reachExtension < 0.02f) reachExtension = 0.02f;  // Minimum extension
	
	// Calculate horizontal distance and angle to target
	float horizDist = hypotf((float)(localX - target->posX), (float)(localZ - target->posZ));
	if (horizDist < reachExtension) {
		reachExtension = horizDist * 0.8f;  // Don't over-extend vertically close targets
	}
	
	// Calculate angle from local player to target
	float dx = (float)(target->posX - localX);
	float dz = (float)(target->posZ - localZ);
	float angle = -atan2f(dx, dz) * 180.0f / PI;
	
	// Calculate offset direction (toward player)
	float offsetX = cosf((angle + 90.0f) * PI / 180.0f);
	float offsetZ = sinf((angle + 90.0f) * PI / 180.0f);
	
	// New center position for hitbox (moved toward player)
	float newCenterX = (float)target->posX - offsetX * reachExtension;
	float newCenterZ = (float)target->posZ - offsetZ * reachExtension;
	
	// Calculate new bounding box
	float halfWidth = PLAYER_WIDTH / 2.0f;
	double newMinX = newCenterX - halfWidth;
	double newMaxX = newCenterX + halfWidth;
	double newMinZ = newCenterZ - halfWidth;
	double newMaxZ = newCenterZ + halfWidth;
	
	// Get entity's bounding box and modify it
	if (JniCache::fid_boundingBox && JniCache::cls_AxisAlignedBB) {
		jobject aabb = env->GetObjectField(target->instance, JniCache::fid_boundingBox);
		if (aabb) {
			// Save original values for restoration
			originalHitbox.minX = env->GetDoubleField(aabb, JniCache::fid_aabb_minX);
			originalHitbox.maxX = env->GetDoubleField(aabb, JniCache::fid_aabb_maxX);
			originalHitbox.minZ = env->GetDoubleField(aabb, JniCache::fid_aabb_minZ);
			originalHitbox.maxZ = env->GetDoubleField(aabb, JniCache::fid_aabb_maxZ);
			originalHitbox.entityRef = target->instance;  // Store ref for restoration
			originalHitbox.valid = true;
			
			// Set new bounding box positions (only X and Z, keep Y intact)
			if (JniCache::fid_aabb_minX) env->SetDoubleField(aabb, JniCache::fid_aabb_minX, newMinX);
			if (JniCache::fid_aabb_maxX) env->SetDoubleField(aabb, JniCache::fid_aabb_maxX, newMaxX);
			if (JniCache::fid_aabb_minZ) env->SetDoubleField(aabb, JniCache::fid_aabb_minZ, newMinZ);
			if (JniCache::fid_aabb_maxZ) env->SetDoubleField(aabb, JniCache::fid_aabb_maxZ, newMaxZ);
			
			env->DeleteLocalRef(aabb);
			
			// Track consecutive extended hits
			consecutiveExtendedHits++;
			lastHitTime = now;
		}
	}
	
	// Don't delete target->instance here - we need it for restoration next tick
	// The instance ref will be invalid next tick anyway (new player list)
}

void Reach::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::Checkbox("Randomize", &settings.randomize);
			if (settings.randomize) {
				ui::SliderFloat("Min Distance", &settings.minDistance, 3.0f, 3.5f, "%.2f");
				ui::SliderFloat("Max Distance", &settings.maxDistance, 3.0f, 3.5f, "%.2f");
			} else {
				ui::SliderFloat("Distance", &settings.distance, 3.0f, 3.5f, "%.2f");
			}
		},
		"Anti-Detection", [this]() {
			ui::SliderInt("Hit Chance", &settings.hitChance, 50, 100, "%d%%");
			ui::SliderInt("Max Consecutive", &settings.maxConsecutiveHits, 1, 5, "%d");
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
