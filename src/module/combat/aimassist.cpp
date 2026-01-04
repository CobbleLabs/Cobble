#include "aimassist.h"
#include "../../sdk/core/minecraft.h"
#include "../../jni/java.h"
#include "../../util/logger.hpp"
#include "../../util/friends_manager.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../sdk/player/player.h"
#include <array>
#include <algorithm>
#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

AimAssist* aimAssist = nullptr;

// Weapon item IDs
namespace ItemIds {
	// Swords
	constexpr int WOOD_SWORD = 268;
	constexpr int STONE_SWORD = 272;
	constexpr int IRON_SWORD = 267;
	constexpr int DIAMOND_SWORD = 276;
	constexpr int GOLD_SWORD = 283;
	
	// Axes
	constexpr int WOOD_AXE = 271;
	constexpr int STONE_AXE = 275;
	constexpr int IRON_AXE = 258;
	constexpr int DIAMOND_AXE = 279;
	constexpr int GOLD_AXE = 286;
}

// Cached JNI lookups for fast-path position reads
static jclass g_entityCls = nullptr;
static jfieldID g_fid_posX = nullptr;
static jfieldID g_fid_posY = nullptr;
static jfieldID g_fid_posZ = nullptr;

static void ensure_entity_fields(jobject sample)
{
	if (g_entityCls || !sample) return;
	jclass cls = Java::Env->GetObjectClass(sample);
	if (!cls) return;
	g_entityCls = (jclass)Java::Env->NewGlobalRef(cls);
	Java::Env->DeleteLocalRef(cls);
	if (!g_entityCls) return;
	g_fid_posX = Java::Env->GetFieldID(g_entityCls, "posX", "D");
	g_fid_posY = Java::Env->GetFieldID(g_entityCls, "posY", "D");
	g_fid_posZ = Java::Env->GetFieldID(g_entityCls, "posZ", "D");
}

static void cleanup_entity_fields()
{
	if (g_entityCls) {
		Java::Env->DeleteGlobalRef(g_entityCls);
		g_entityCls = nullptr;
	}
	g_fid_posX = g_fid_posY = g_fid_posZ = nullptr;
}

// ============================================================
// AimBoost Implementation
// ============================================================

AimBoost::AimBoost(float speed_mult_min, float speed_mult_max, float freq_min_ms, float freq_max_ms, bool continuous)
	: speed_mult_min(speed_mult_min), speed_mult_max(speed_mult_max),
	frequency_min_ms((uint32_t)freq_min_ms), frequency_max_ms((uint32_t)freq_max_ms),
	continuous(continuous)
{
	speed_mult = (speed_mult_min + speed_mult_max) / 2;
	prev_speed_mult = speed_mult;
}

float AimBoost::Delay()
{
	static std::random_device rd;
	static std::mt19937 gen(rd());
	
	auto randFloat = [&](float min, float max) -> float {
		std::uniform_real_distribution<float> dis(min, max);
		return dis(gen);
	};
	
	auto randInt = [&](int min, int max) -> int {
		std::uniform_int_distribution<int> dis(min, max);
		return dis(gen);
	};
	
	auto slerp = [](float start, float end, float t) -> float {
		t = std::clamp(t, 0.0f, 1.0f);
		t = t * t * (3.0f - 2.0f * t);
		return start + (end - start) * t;
	};

	if (timer.Elapsed<>() >= frequency_ms)
	{
		if (continuous)
		{
			prev_speed_mult = randFloat(speed_mult_min, speed_mult_max);
		}
		else
		{
			prev_speed_mult = speed_mult;
			speed_mult = randFloat(speed_mult_min, speed_mult_max);
		}

		frequency_ms = randInt(frequency_min_ms, frequency_max_ms);
		timer.Start();
	}

	return slerp(prev_speed_mult, continuous ? speed_mult : 1.f, timer.Elapsed<>() / frequency_ms);
}

// ============================================================
// AimAssist Implementation
// ============================================================

AimAssist::AimAssist() : Module("AimAssist")
{
	keybind = 0;
	std::random_device rd;
	rng.seed(rd());
}

void AimAssist::onEnable()
{
	log_ok("AimAssist enabled");
	hasLockedTarget = false;
	pitchdiffSpeed = 0.f;
	yawdiffSpeed = 0.f;
	cursorAccumY = 0.0;
	targetHasHitbox = false;
	targetHitboxMinX = targetHitboxMinY = targetHitboxMinZ = 0.0f;
	targetHitboxMaxX = targetHitboxMaxY = targetHitboxMaxZ = 0.0f;
	prevFrameHadTarget = false;
	targetRampTimer.Start();
	clickRampTimer.Start();
	lastMouseDown = false;
	clickStartedThisFrame = false;
}

void AimAssist::onDisable()
{
	log_ok("AimAssist disabled");
	hasLockedTarget = false;

	if (cachedTarget) {
		Java::Env->DeleteGlobalRef(cachedTarget);
		cachedTarget = nullptr;
	}
	cleanup_entity_fields();
	cursorAccumY = 0.0;
	pitchdiffSpeed = yawdiffSpeed = 0.f;
	targetHasHitbox = false;
	targetHitboxMinX = targetHitboxMinY = targetHitboxMinZ = 0.0f;
	targetHitboxMaxX = targetHitboxMaxY = targetHitboxMaxZ = 0.0f;
	reactionTimeTimer.Start();
	lastMouseDown = false;
	clickStartedThisFrame = false;
}

float AimAssist::wrapTo180(float value)
{
	float res = std::fmod(value + 180.f, 360.f);
	if (res < 0)
		res += 360.f;
	return res - 180.f;
}

std::pair<float, float> AimAssist::getAngles(double x1, double y1, double z1, double x2, double y2, double z2)
{
	float d_x = (float)(x2 - x1);
	float d_y = (float)(y2 - y1);
	float d_z = (float)(z2 - z1);

	constexpr float PI = 3.14159265358979323846f;
	float hypothenuse = std::sqrt(d_x * d_x + d_z * d_z);
	float yaw = std::atan2(d_z, d_x) * 180.f / PI - 90.f;
	float pitch = -std::atan2(d_y, hypothenuse) * 180.f / PI;

	return std::make_pair(yaw, pitch);
}

float AimAssist::randFloat(float min, float max)
{
	std::uniform_real_distribution<float> dis(min, max);
	return dis(rng);
}

int AimAssist::randInt(int min, int max)
{
	std::uniform_int_distribution<int> dis(min, max);
	return dis(rng);
}

float AimAssist::slerp(float start, float end, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	t = t * t * (3.0f - 2.0f * t);
	return start + (end - start) * t;
}

void AimAssist::onUpdate()
{
	clickStartedThisFrame = false;
	if (!player.isValid()) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;

	bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	if (!settings.alwaysAim && !mouseDown)
	{
		return;
	}

	if (mouseDown && !lastMouseDown) {
		clickStartedThisFrame = true;
		clickRampTimer.Start();
		targetRampTimer.Start();
		yawdiffSpeed = 0.f;
		pitchdiffSpeed = 0.f;
	}

	if (settings.breakBlocks && mouseDown)
	{
		if (mc.isTargetingBlock())
		{
			return;
		}
	}

	// Weapon-only check
	if (settings.weaponOnly && !isHoldingWeapon())
	{
		return;
	}

	float localYaw = player.getYaw();
	float localPitch = player.getPitch();
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();
	targetChangedThisFrame = false;

	bool getNewTarget = true;

	// Check if locked target is still valid
	if (settings.lockAim && hasLockedTarget)
	{
		float yawToTarget = getAngles(localX, localY, localZ, lockedTargetX, lockedTargetY, lockedTargetZ).first;
		float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));
		double dist = std::sqrt(
			(lockedTargetX - localX) * (lockedTargetX - localX) +
			(lockedTargetY - localY) * (lockedTargetY - localY) +
			(lockedTargetZ - localZ) * (lockedTargetZ - localZ)
		);

		if (yawDiff <= settings.fov && dist <= settings.distance)
		{
			getNewTarget = false;
		}
	}

	double targetX = lockedTargetX;
	double targetY = lockedTargetY;
	double targetZ = lockedTargetZ;

	hasLockedTarget = false;

	// ============================================================
	// HYBRID LOOP LOGIC
	// 1. If we have a cached GlobalRef target (cachedTarget), update its position FAST (every frame).
	// 2. If it is valid and in range: use it.
	// 3. If invalid or timer expired: run findTarget (slow scan).
	// ============================================================

	bool validCache = false;
	bool forceScan = false;

	if (cachedTarget) {
		if (Java::Env->IsSameObject(cachedTarget, NULL)) {
			Java::Env->DeleteGlobalRef(cachedTarget);
			cachedTarget = nullptr;
		} else {
			ensure_entity_fields(cachedTarget);

			// Update position from cached target (Fast JNI)
			if (!g_fid_posX || !g_fid_posY || !g_fid_posZ) {
				Java::Env->ExceptionClear();
				Java::Env->DeleteGlobalRef(cachedTarget);
				cachedTarget = nullptr;
				forceScan = true;
			} else {
				double tX = Java::Env->GetDoubleField(cachedTarget, g_fid_posX);
				double tY = Java::Env->GetDoubleField(cachedTarget, g_fid_posY);
				double tZ = Java::Env->GetDoubleField(cachedTarget, g_fid_posZ);

				if (Java::Env->ExceptionCheck()) {
					Java::Env->ExceptionClear();
					Java::Env->DeleteGlobalRef(cachedTarget);
					cachedTarget = nullptr;
					forceScan = true;
				} else {
					// Validate distance/etc slightly? (Optional fast check)
					// For now trust it until slow scan overrides it.
					targetX = tX;
					targetY = tY;
					targetZ = tZ;
					validCache = true;
					hasLockedTarget = true;
				}
			}
		}
	}

	// Run slow scan if needed (or if we have no target)
	// getNewTarget variable from above logic helps decide if we WANT a new target.
	// findTarget now handles the 25ms rate limiting internally.
	
	if (forceScan) {
		validCache = false;
	}

	if (!validCache || getNewTarget) 
	{
		// findTarget will return true ONLY if it updated the target (slow scan or cache hit inside it).
		// But since we moved cache logic here, findTarget should mostly be for SCANNING.
		// Let's rely on findTarget for the heavy lifting still, but use the persistence we added here.
		
		if (findTarget(targetX, targetY, targetZ)) {
			hasLockedTarget = true;
			if (!prevFrameHadTarget || targetChangedThisFrame) {
				targetRampTimer.Start(); // ramp up speed when acquiring a target
				yawdiffSpeed = 0.f;
				pitchdiffSpeed = 0.f;
			}
		}
	}
	
	if (!hasLockedTarget) {
		prevFrameHadTarget = false;
		lastMouseDown = mouseDown;
		return;
	}

	lockedTargetX = targetX;
	lockedTargetY = targetY;
	lockedTargetZ = targetZ;

	double aimTargetX = targetX;
	double aimTargetY = targetY;
	double aimTargetZ = targetZ;

	if (!targetHasHitbox && Java::Version == MinecraftVersion::MC_1_7_10)
	{
		aimTargetY += 1.6;
	}

	double aimX, aimY, aimZ;
	if (!getAimPoint(aimTargetX, aimTargetY, aimTargetZ, aimX, aimY, aimZ))
	{
		return;
	}

	auto [yawTarget, pitchTarget] = getAngles(localX, localY, localZ, aimX, aimY, aimZ);

	float yawDiff = wrapTo180(-(localYaw - yawTarget));
	float pitchDiff = wrapTo180(-(localPitch - pitchTarget));

	float speedSetting = std::min(settings.speed, 120.0f);
	float speed = speedSetting * 3.0f;
	cachedMouseSensitivity = mc.getMouseSensitivity();

	applyAimRand(yawDiff, pitchDiff, speed);
	prevFrameHadTarget = hasLockedTarget;
	lastMouseDown = mouseDown;
}

bool AimAssist::findTarget(double& targetX, double& targetY, double& targetZ)
{
	// ============================================================
	// HYBRID TRACKING SYSTEM
	// 1. Fast Path (Every Frame): Update position of locked target directly (JNI)
	// 2. Slow Path (Scan Interval): Scan full player list for new/better targets
	// ============================================================

	static Timer scanTimer;

	// FAST PATH: If we have a cached target, update its position & validate
	if (cachedTarget && scanTimer.Elapsed<>() < 50) // Rescan every 50ms
	{
		// Check validity (basic)
		if (Java::Env->IsSameObject(cachedTarget, NULL)) {
			Java::Env->DeleteGlobalRef(cachedTarget); // Clean up invalid ref
			cachedTarget = nullptr; // Invalid
			targetHasHitbox = false;
			prevFrameHadTarget = false;
		}
		else {
			ensure_entity_fields(cachedTarget);

			if (!g_fid_posX || !g_fid_posY || !g_fid_posZ) {
				Java::Env->ExceptionClear();
				Java::Env->DeleteGlobalRef(cachedTarget);
				cachedTarget = nullptr;
				targetHasHitbox = false;
				return false;
			}
			
			// Update position
			double cX = Java::Env->GetDoubleField(cachedTarget, g_fid_posX);
			double cY = Java::Env->GetDoubleField(cachedTarget, g_fid_posY);
			double cZ = Java::Env->GetDoubleField(cachedTarget, g_fid_posZ);

			if (Java::Env->ExceptionCheck()) {
				Java::Env->ExceptionClear();
				Java::Env->DeleteGlobalRef(cachedTarget);
				cachedTarget = nullptr;
				targetHasHitbox = false;
				prevFrameHadTarget = false;
				return false;
			}

			// Basic distance check to see if we should drop it immediately?
			// For now, assume stickiness for 50ms is fine.
			
			// If angle error is large, force slow scan to re-acquire smoothly
			float localYaw = player.getYaw();
			double localX = player.getPosX();
			double localY = player.getPosY();
			double localZ = player.getPosZ();
			float yawToTarget = getAngles(localX, localY, localZ, cX, cY, cZ).first;
			float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));
			if (yawDiff > 25.f) {
				return false; // force slow scan/ramp
			}

			targetX = cX;
			targetY = cY;
			targetZ = cZ;
			return true;
		}
	}
	
	// SLOW PATH GUARD: If we didn't take the fast path, checks if we are allowed to scan
	if (scanTimer.Elapsed<>() < 50) {
		return false; // Skip scan (rate limited)
	}

	// SLOW PATH: Full Scan
	scanTimer.Start();

	auto players = mc.getPlayerList();

	if (players.empty()) {
		if (cachedTarget) { Java::Env->DeleteGlobalRef(cachedTarget); cachedTarget = nullptr; }
		targetHasHitbox = false;
		return false; 
	}

	float localYaw = player.getYaw();
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();

	float lowestHealth = FLT_MAX;
	float minimalAngleDiff = (float)settings.fov / 2.f;
	float closestDistance = FLT_MAX;
	bool found = false;
	
	// Helper to keep track of best player instance
	jobject bestPlayerObj = nullptr;
	PlayerInfo closestPlayer;

	for (auto& p : players)
	{
		double dx = p.posX - localX;
		double dy = p.posY - localY;
		double dz = p.posZ - localZ;
		float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);

		if (dist > settings.distance || dist < 0.2f) continue;
		if (p.isInvisible && !settings.invisibles) continue;

		// Friends/Teams filter - skip friendly players
		if (friendsManager && friendsManager->applyToCombat) {
			// Get local player's team color for comparison (cached per scan)
			static int localTeamColor = -1;
			static Timer colorRefreshTimer;
			if (colorRefreshTimer.Elapsed<>() > 1000) { // Refresh every second
				localTeamColor = mc.getLocalPlayerTeamColor();
				colorRefreshTimer.Start();
			}
			
			ChatColor playerColor = static_cast<ChatColor>(p.teamColor);
			ChatColor localColor = static_cast<ChatColor>(localTeamColor);
			
			// Strip color codes from name for friend matching (fallback if SDK doesn't clean it)
			std::string cleanPlayerName = FriendsManager::stripColorCodes(p.name);
			
			bool isFriend = friendsManager->isFriend(cleanPlayerName);
			bool isTeammate = friendsManager->isTeammate(cleanPlayerName, playerColor, localColor);
			
			if (isFriend || isTeammate) continue;
		}

		float yawToTarget = getAngles(localX, localY, localZ, p.posX, p.posY, p.posZ).first;
		float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));

		if ((int)yawDiff > settings.fov / 2) continue;

		if (settings.targetMode == TargetMode::DISTANCE)
		{
			if (dist < closestDistance)
			{
				closestDistance = dist;
				closestPlayer = p;
				found = true;
				bestPlayerObj = p.instance;
			}
		}
		else if (settings.targetMode == TargetMode::HEALTH)
		{
			if (p.health < lowestHealth)
			{
				lowestHealth = p.health;
				closestPlayer = p;
				found = true;
				bestPlayerObj = p.instance;
			}
		}
		else // TargetMode::FOV
		{
			if (yawDiff < minimalAngleDiff)
			{
				minimalAngleDiff = yawDiff;
				closestPlayer = p;
				found = true;
				bestPlayerObj = p.instance;
			}
		}
	}

	if (found) {
		targetX = closestPlayer.posX;
		targetY = closestPlayer.posY;
		targetZ = closestPlayer.posZ;
		targetHitboxMinX = closestPlayer.hitboxMinX;
		targetHitboxMinY = closestPlayer.hitboxMinY;
		targetHitboxMinZ = closestPlayer.hitboxMinZ;
		targetHitboxMaxX = closestPlayer.hitboxMaxX;
		targetHitboxMaxY = closestPlayer.hitboxMaxY;
		targetHitboxMaxZ = closestPlayer.hitboxMaxZ;

		float hitboxWidth = targetHitboxMaxX - targetHitboxMinX;
		float hitboxHeight = targetHitboxMaxY - targetHitboxMinY;
		targetHasHitbox = (hitboxWidth > 0.01f) && (hitboxHeight > 0.01f);
	} else {
		targetHasHitbox = false;
	}

	// Update Cached Target
	if (found) {
		if (cachedTarget) {
			// Check if it's the SAME object to avoid delete/new overhead?
			// IsSameObject is cheap.
			if (!Java::Env->IsSameObject(cachedTarget, bestPlayerObj)) {
				Java::Env->DeleteGlobalRef(cachedTarget);
				cachedTarget = Java::Env->NewGlobalRef(bestPlayerObj);
				targetChangedThisFrame = true;
			}
		} else {
			cachedTarget = Java::Env->NewGlobalRef(bestPlayerObj);
			targetChangedThisFrame = true;
		}
	} else {
		if (cachedTarget) {
			Java::Env->DeleteGlobalRef(cachedTarget);
			cachedTarget = nullptr;
		}
	}
	
	return found;
}

void AimAssist::applyAimRand(float yawDiff, float pitchDiff, float speed)
{
	float absYawDiff = std::abs(yawDiff);
	float absPitchDiff = std::abs(pitchDiff);

	if (absYawDiff < 1.f && absPitchDiff < 1.f)
	{
		return;
	}

	float maxDiff = std::max(absYawDiff, absPitchDiff);
	float jitterRange = (maxDiff < 10.f) ? 0.02f : (maxDiff < 30.f ? 0.05f : 0.08f);
	if (maxDiff < 3.f) jitterRange = 0.0f; // no jitter when already aligned
	yawDiff += randFloat(-jitterRange, jitterRange);
	pitchDiff += randFloat(-jitterRange, jitterRange);
	float baseSpeed = speed;
	float alignment = std::clamp(std::max(absYawDiff, absPitchDiff) / 90.0f, 0.0f, 1.0f);
	float dynamicSpeed = std::lerp(baseSpeed * 0.35f, baseSpeed, alignment);

	float clampedSpeedSetting = std::min(settings.speed, 120.0f);
	float speedNorm = std::clamp(clampedSpeedSetting / 120.0f, 0.0f, 1.0f);
	// Ramp respects both time since acquisition and current angular error (larger error = slower ramp)
	float timeRamp = std::clamp(targetRampTimer.Elapsed<>() / targetRampDurationMs, 0.25f, 1.0f);
	float errorRamp = std::clamp(1.0f - (maxDiff / 90.0f), 0.25f, 1.0f);
	float clickRamp = settings.alwaysAim ? 1.0f : std::clamp(clickRampTimer.Elapsed<>() / clickRampDurationMs, 0.3f, 1.0f);
	float ramp = std::min({ timeRamp, errorRamp, clickRamp });
	dynamicSpeed *= ramp;
	if (clampedSpeedSetting >= 120.0f) {
		dynamicSpeed *= 0.8f; // soften high-speed settings to avoid flicks
	} else if (maxDiff < 20.f) {
		dynamicSpeed *= 0.85f; // slow down near micro-adjust to prevent rebound
	}

	static bool once = false;
	static float targetYawSpeed = 0.0f;
	static float targetPitchSpeed = 0.0f;
	if (!once || reactionTimeTimer.Elapsed<>() > settings.reactionTime)
	{
		float pitchBoosts = pitchBoost.Delay() * pitchBoost2.Delay();
		float yawBoosts = yawBoost.Delay() * yawBoost2.Delay();

		if (!settings.horizontalOnly)
		{
			float desiredPitchSpeed = pitchDiff / (1000.f / (dynamicSpeed * pitchBoosts));
			if (std::abs(desiredPitchSpeed) < 0.03f)
				desiredPitchSpeed = desiredPitchSpeed < 0 ? -0.03f : 0.03f;
			targetPitchSpeed = desiredPitchSpeed;
		}
		else
		{
			targetPitchSpeed = 0.f;
		}

		float desiredYawSpeed = yawDiff / (1000.f / (dynamicSpeed * yawBoosts));
		if (std::abs(desiredYawSpeed) < 0.03f)
			desiredYawSpeed = desiredYawSpeed < 0 ? -0.03f : 0.03f;
		targetYawSpeed = desiredYawSpeed;

		once = true;
		reactionTimeTimer.Start();
	}

	float smoothingStrength = std::clamp(0.08f + alignment * 0.18f + (clampedSpeedSetting / 400.0f) * 0.1f, 0.08f, 0.3f);
	yawdiffSpeed = std::lerp(yawdiffSpeed, targetYawSpeed, smoothingStrength);
	pitchdiffSpeed = std::lerp(pitchdiffSpeed, targetPitchSpeed, smoothingStrength);

	static Timer pitchRandTimer;
	static int pitchrand = randInt(-1, 1);

	const int pitchUpdateMsMinLow = 100;
	const int pitchUpdateMsMaxLow = 300;
	const int pitchUpdateMsMinHigh = 400;
	const int pitchUpdateMsMaxHigh = 600;
	static int pitchUpdateMsMin = 100;
	static int pitchUpdateMsMax = 600;

	static int pitchupdatems = randInt(pitchUpdateMsMin, pitchUpdateMsMax);
	static float pitchrandsmooth = 0;
	static int pitchrandbegin = 0;

	if (absYawDiff > absPitchDiff && absYawDiff > 5.f)
	{
		if ((int)pitchRandTimer.Elapsed<>() > pitchupdatems)
		{
			pitchrandbegin = pitchrand;

			float t = (std::clamp(absYawDiff, 10.f, 50.f) - 9.99f) / 40.f;
			pitchUpdateMsMin = (int)std::lerp((float)pitchUpdateMsMinLow, (float)pitchUpdateMsMinHigh, t);
			pitchUpdateMsMax = (int)std::lerp((float)pitchUpdateMsMaxLow, (float)pitchUpdateMsMaxHigh, t);
			int travelDistance = 1;
			pitchupdatems = randInt(pitchUpdateMsMin, pitchUpdateMsMax);

			if (pitchDiff > 0)
				pitchrand = randInt(1, travelDistance);
			else
				pitchrand = randInt(-travelDistance, -1);

			pitchRandTimer.Start();
		}

		pitchrandsmooth = slerp((float)pitchrandbegin, (float)pitchrand, pitchRandTimer.Elapsed<>() / pitchupdatems);
	}
	else
	{
		constexpr float partialTick = 1.0f;
		pitchrandsmooth = std::lerp(pitchrandsmooth, 0.f, std::clamp(partialTick * 0.1f, 0.f, 1.f));
	}

	float jitterFade = std::clamp(std::max(absYawDiff, absPitchDiff) / 45.0f, 0.0f, 1.0f);
	pitchrandsmooth *= jitterFade;

	cursorAccumY += pitchdiffSpeed + pitchrandsmooth;

	int moveY = 0;
	if (std::abs(cursorAccumY) >= 1.0) {
		moveY = (int)std::round(cursorAccumY);
		cursorAccumY -= moveY;
	}

	if (settings.horizontalOnly)
	{
		moveY = 0;
		cursorAccumY = 0.0;
	}

	// Ensure low in-game sensitivity doesn't overly throttle aim speed.
	float minSens = std::lerp(0.15f, 0.45f, speedNorm);
	float effectiveSens = std::max(cachedMouseSensitivity, minSens);
	float sensBase = effectiveSens * 0.6f + 0.2f;
	float sensMultiplier = sensBase * sensBase * sensBase * 8.0f;

	constexpr float kMouseGain = 0.15f;
	float yawDelta = yawdiffSpeed * sensMultiplier * kMouseGain;
	float pitchDelta = (float)moveY * sensMultiplier * kMouseGain;

	// Guard against overshoot: clamp step to fraction of remaining error
	auto clampStepToError = [](float delta, float remaining, float frac) -> float {
		float sign = delta >= 0.f ? 1.f : -1.f;
		float mag = std::abs(delta);
		float maxStep = std::max(0.2f, std::abs(remaining) * frac);
		mag = std::min(mag, maxStep);
		mag = std::min(mag, std::abs(remaining));
		return sign * mag;
	};

	float firstFracYaw = 0.55f;
	float firstFracPitch = 0.6f;
	if (targetChangedThisFrame || maxDiff > 20.f) {
		firstFracYaw = 0.4f;
		firstFracPitch = 0.55f;
	}
	yawDelta = clampStepToError(yawDelta, yawDiff, firstFracYaw);
	pitchDelta = clampStepToError(pitchDelta, pitchDiff, firstFracPitch);

	// Clamp per-frame rotation to keep motion buttery even on spikes
	float yawStepBase = std::lerp(2.5f, 6.5f, speedNorm);
	float pitchStepBase = std::lerp(1.8f, 5.5f, speedNorm);
	if (speedNorm > 0.8f) {
		yawStepBase *= 0.85f;
		pitchStepBase *= 0.85f;
	}
	const float maxYawStep = yawStepBase;
	const float maxPitchStep = pitchStepBase;
	yawDelta = std::clamp(yawDelta, -maxYawStep, maxYawStep);
	pitchDelta = std::clamp(pitchDelta, -maxPitchStep, maxPitchStep);

	bool applyYaw = std::abs(yawDelta) > 0.0001f;
	if (!applyYaw && moveY == 0) return;

	if (applyYaw) {
		float currentYaw = player.getYaw();
		float newYaw = currentYaw + yawDelta;
		player.setYaw(newYaw);
		player.setPrevYaw(newYaw);
	}

	if (moveY != 0) {
		float currentPitch = player.getPitch();
		float targetPitch = std::clamp(currentPitch + pitchDelta, -90.0f, 90.0f);
		player.setPitch(targetPitch);
		player.setPrevPitch(targetPitch);
	}
}

bool AimAssist::isHoldingWeapon()
{
	int id = player.getHeldItemId();
	if (id == -1) return false;

	// Swords
	if (id == ItemIds::WOOD_SWORD || id == ItemIds::STONE_SWORD ||
		id == ItemIds::IRON_SWORD || id == ItemIds::DIAMOND_SWORD ||
		id == ItemIds::GOLD_SWORD)
		return true;

	// Axes
	if (id == ItemIds::WOOD_AXE || id == ItemIds::STONE_AXE ||
		id == ItemIds::IRON_AXE || id == ItemIds::DIAMOND_AXE ||
		id == ItemIds::GOLD_AXE)
		return true;

	return false;
}

bool AimAssist::getAimPoint(double targetX, double targetY, double targetZ, double& aimX, double& aimY, double& aimZ)
{
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();
	float localYaw = player.getYaw();

	// Prefer precise hitbox data from PlayerInfo when available
	if (targetHasHitbox)
	{
		double minX = targetHitboxMinX;
		double maxX = targetHitboxMaxX;
		double minY = targetHitboxMinY;
		double maxY = targetHitboxMaxY;
		double minZ = targetHitboxMinZ;
		double maxZ = targetHitboxMaxZ;

		if (settings.aimAtClosestPoint)
		{
			aimX = std::clamp(localX, minX, maxX);
			aimY = std::clamp(localY, minY, maxY);
			aimZ = std::clamp(localZ, minZ, maxZ);
			return true;
		}

		aimX = (minX + maxX) * 0.5;
		aimZ = (minZ + maxZ) * 0.5;
		double height = maxY - minY;
		aimY = height > 0.01 ? minY + height * 0.5 : targetY;
		return true;
	}

	constexpr double HALF_WIDTH = 0.3;

	if (settings.aimAtClosestPoint)
	{
		double minX = targetX - HALF_WIDTH;
		double maxX = targetX + HALF_WIDTH;
		double minY = targetY - 1.6;
		double maxY = targetY + 0.2;
		double minZ = targetZ - HALF_WIDTH;
		double maxZ = targetZ + HALF_WIDTH;

		aimX = std::clamp(localX, minX, maxX);
		aimY = std::clamp(localY, minY, maxY);
		aimZ = std::clamp(localZ, minZ, maxZ);
		return true;
	}
	else
	{
		double corners[4][3] = {
			{ targetX - HALF_WIDTH, targetY - 1.6, targetZ + HALF_WIDTH },
			{ targetX - HALF_WIDTH, targetY - 1.6, targetZ - HALF_WIDTH },
			{ targetX + HALF_WIDTH, targetY + 0.2, targetZ - HALF_WIDTH },
			{ targetX + HALF_WIDTH, targetY + 0.2, targetZ + HALF_WIDTH },
		};

		std::array<float, 4> yawDiffs;
		for (int i = 0; i < 4; i++)
		{
			float yawToCorner = getAngles(localX, localY, localZ, 
				corners[i][0], corners[i][1], corners[i][2]).first;
			yawDiffs[i] = wrapTo180(-(localYaw - yawToCorner));
		}

		float yawDiffToPos = wrapTo180(-(localYaw - getAngles(localX, localY, localZ, targetX, targetY, targetZ).first));

		if (yawDiffToPos < 0)
		{
			float maxVal = yawDiffs[0];
			for (int i = 1; i < 4; i++) if (yawDiffs[i] > maxVal) maxVal = yawDiffs[i];
			if (maxVal > 0)
			{
				return false;
			}
		}
		else
		{
			float minVal = yawDiffs[0];
			for (int i = 1; i < 4; i++) if (yawDiffs[i] < minVal) minVal = yawDiffs[i];
			if (minVal < 0)
			{
				return false;
			}
		}

		aimX = targetX;
		aimY = targetY;
		aimZ = targetZ;
		return true;
	}
}

void AimAssist::onRenderMenu()
{
	static const char* targetModes[] = { "Lowest Health", "Closest to Player", "Closest to Crosshair" };
	int currentMode = static_cast<int>(settings.targetMode);

	ui::two_column(
		"Settings", [this, &currentMode]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::SliderFloat("Speed", &settings.speed, 1.0f, 120.0f, "%.0f");
			ui::SliderInt("FOV", &settings.fov, 10, 360, "%d");
			ui::SliderFloat("Distance", &settings.distance, 3.0f, 8.0f, "%.1f");
			ui::SliderInt("Reaction Time", &settings.reactionTime, 0, 200, "%d ms");
		},
		"Options", [this, &currentMode]() {
			ui::Checkbox("Always Aim", &settings.alwaysAim);
			ui::Checkbox("Horizontal Only", &settings.horizontalOnly);
			ui::Checkbox("Lock Aim", &settings.lockAim);
			ui::Checkbox("Break Blocks", &settings.breakBlocks);
			ui::Checkbox("Weapon Only", &settings.weaponOnly);
			ui::Checkbox("Aim Closest Point", &settings.aimAtClosestPoint);
			ui::Checkbox("Target Invisibles", &settings.invisibles);
			
			ImGui::Spacing();
			if (ImGui::Combo("Target Mode", &currentMode, targetModes, IM_ARRAYSIZE(targetModes)))
			{
				settings.targetMode = static_cast<TargetMode>(currentMode);
			}
		}
	);
}