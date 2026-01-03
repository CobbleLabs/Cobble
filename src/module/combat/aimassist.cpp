#include "aimassist.h"
#include "../../sdk/minecraft.h"
#include "../../jni/java.h"
#include "../../util/logger.hpp"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../sdk/player.h"
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
	cursorAccumX = 0.0;
	cursorAccumY = 0.0;
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
	cursorAccumX = cursorAccumY = 0.0;
	pitchdiffSpeed = yawdiffSpeed = 0.f;
	reactionTimeTimer.Start();
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
	if (!player.isValid()) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;

	if (!settings.alwaysAim && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
	{
		return;
	}

	if (settings.breakBlocks && (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
	{
		if (mc.isTargetingBlock())
		{
			return;
		}
	}

	float localYaw = player.getYaw();
	float localPitch = player.getPitch();
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();

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

	if (cachedTarget) {
		if (Java::Env->IsSameObject(cachedTarget, NULL)) {
			Java::Env->DeleteGlobalRef(cachedTarget);
			cachedTarget = nullptr;
		} else {
			ensure_entity_fields(cachedTarget);

			// Update position from cached target (Fast JNI)
			if (!g_fid_posX || !g_fid_posY || !g_fid_posZ) {
				Java::Env->ExceptionClear();
				return;
			}

			double tX = Java::Env->GetDoubleField(cachedTarget, g_fid_posX);
			double tY = Java::Env->GetDoubleField(cachedTarget, g_fid_posY);
			double tZ = Java::Env->GetDoubleField(cachedTarget, g_fid_posZ);

			if (Java::Env->ExceptionCheck()) {
				Java::Env->ExceptionClear();
				return;
			}
			
			// Validate distance/etc slightly? (Optional fast check)
			// For now trust it until slow scan overrides it.
			
			targetX = tX;
			targetY = tY;
			targetZ = tZ;
			validCache = true;
			hasLockedTarget = true;
		}
	}

	// Run slow scan if needed (or if we have no target)
	// getNewTarget variable from above logic helps decide if we WANT a new target.
	// findTarget now handles the 25ms rate limiting internally.
	
	if (!validCache || getNewTarget) 
	{
		// findTarget will return true ONLY if it updated the target (slow scan or cache hit inside it).
		// But since we moved cache logic here, findTarget should mostly be for SCANNING.
		// Let's rely on findTarget for the heavy lifting still, but use the persistence we added here.
		
		if (findTarget(targetX, targetY, targetZ)) {
			hasLockedTarget = true;
		}
	}
	
	if (!hasLockedTarget) return;

	lockedTargetX = targetX;
	lockedTargetY = targetY;
	lockedTargetZ = targetZ;

	if (Java::Version == MinecraftVersion::MC_1_7_10)
	{
		targetY += 1.6;
	}

	double aimX, aimY, aimZ;
	if (!getAimPoint(targetX, targetY, targetZ, aimX, aimY, aimZ))
	{
		return;
	}

	auto [yawTarget, pitchTarget] = getAngles(localX, localY, localZ, aimX, aimY, aimZ);

	float yawDiff = wrapTo180(-(localYaw - yawTarget));
	float pitchDiff = wrapTo180(-(localPitch - pitchTarget));

	float speed = settings.speed * 3;
	cachedMouseSensitivity = mc.getMouseSensitivity();

	applyAimRand(yawDiff, pitchDiff, speed);
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
		}
		else {
			ensure_entity_fields(cachedTarget);

			if (!g_fid_posX || !g_fid_posY || !g_fid_posZ) {
				Java::Env->ExceptionClear();
				return false;
			}
			
			// Update position
			double cX = Java::Env->GetDoubleField(cachedTarget, g_fid_posX);
			double cY = Java::Env->GetDoubleField(cachedTarget, g_fid_posY);
			double cZ = Java::Env->GetDoubleField(cachedTarget, g_fid_posZ);

			if (Java::Env->ExceptionCheck()) {
				Java::Env->ExceptionClear();
				return false;
			}

			// Basic distance check to see if we should drop it immediately?
			// For now, assume stickiness for 50ms is fine.
			
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

	// Profiling getPlayerList (JNI heavy)
	static int   pl_calls = 0;
	static float pl_maxMs = 0.0f;
	static DWORD pl_lastPrint = GetTickCount();
	static LARGE_INTEGER qpf = []{ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();

	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);

	auto players = mc.getPlayerList();

	QueryPerformanceCounter(&t1);
	float ms = float((t1.QuadPart - t0.QuadPart) * 1000.0 / double(qpf.QuadPart));
	
	pl_calls++;
	if (ms > pl_maxMs) pl_maxMs = ms;

	DWORD now = GetTickCount();
	if (now - pl_lastPrint >= 1000) {
		log_debug("getPlayerList: calls=%d max=%.3fms count=%d cache=%d", 
			pl_calls, pl_maxMs, (int)players.size(), (cachedTarget != nullptr));
		pl_calls = 0;
		pl_maxMs = 0.0f;
		pl_lastPrint = now;
	}

	if (players.empty()) {
		if (cachedTarget) { Java::Env->DeleteGlobalRef(cachedTarget); cachedTarget = nullptr; }
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
	}

	// Update Cached Target
	if (found) {
		if (cachedTarget) {
			// Check if it's the SAME object to avoid delete/new overhead?
			// IsSameObject is cheap.
			if (!Java::Env->IsSameObject(cachedTarget, bestPlayerObj)) {
				Java::Env->DeleteGlobalRef(cachedTarget);
				cachedTarget = Java::Env->NewGlobalRef(bestPlayerObj);
			}
		} else {
			cachedTarget = Java::Env->NewGlobalRef(bestPlayerObj);
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

	yawDiff += randFloat(-0.2f, 0.2f);
	pitchDiff += randFloat(-0.2f, 0.2f);
	float baseSpeed = speed;
	float alignment = std::clamp(std::max(absYawDiff, absPitchDiff) / 90.0f, 0.0f, 1.0f);
	float dynamicSpeed = std::lerp(baseSpeed * 0.35f, baseSpeed, alignment);

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

	float smoothingStrength = std::clamp(0.08f + alignment * 0.18f + (settings.speed / 400.0f) * 0.1f, 0.08f, 0.3f);
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
			int travelDistance = 2;
			pitchupdatems = randInt(pitchUpdateMsMin, pitchUpdateMsMax);

			if (absYawDiff > 30.f)
				travelDistance = 2;
			else
				travelDistance = 1;

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

	static double cursorAccumX = 0.0;
	static double cursorAccumY = 0.0;

	cursorAccumX += yawdiffSpeed;
	cursorAccumY += pitchdiffSpeed + pitchrandsmooth;

	int moveX = (int)std::round(cursorAccumX);
	int moveY = (int)std::round(cursorAccumY);

	cursorAccumX -= moveX;
	cursorAccumY -= moveY;

	if (settings.horizontalOnly)
	{
		moveY = 0;
		cursorAccumY = 0.0;
	}

	if (moveX == 0 && moveY == 0) return;

	// --- debug counters (no spam) ---
	static int   rot_calls = 0;
	static float rot_maxMs = 0.0f;
	static DWORD rot_lastPrint = GetTickCount();
	static int   rot_lastMoveX = 0;
	static int   rot_lastMoveY = 0;

	static LARGE_INTEGER qpf = []{
		LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f;
	}();

	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);

	float sensBase = cachedMouseSensitivity * 0.6f + 0.2f;
	float sensMultiplier = sensBase * sensBase * sensBase * 8.0f;

	constexpr float kMouseGain = 0.15f;
	float yawDelta = (float)moveX * sensMultiplier * kMouseGain;
	float pitchDelta = (float)moveY * sensMultiplier * kMouseGain;

	// Clamp per-frame rotation to keep motion buttery even on spikes
	const float maxYawStep = 4.0f;
	const float maxPitchStep = 3.0f;
	yawDelta = std::clamp(yawDelta, -maxYawStep, maxYawStep);
	pitchDelta = std::clamp(pitchDelta, -maxPitchStep, maxPitchStep);

	if (moveX != 0) {
		float currentYaw = player.getYaw();
		player.setYaw(currentYaw + yawDelta);
	}

	if (moveY != 0) {
		float currentPitch = player.getPitch();
		float targetPitch = std::clamp(currentPitch + pitchDelta, -90.0f, 90.0f);
		player.setPitch(targetPitch);
	}

	QueryPerformanceCounter(&t1);

	float ms = float((t1.QuadPart - t0.QuadPart) * 1000.0 / double(qpf.QuadPart));
	rot_calls++;
	if (ms > rot_maxMs) rot_maxMs = ms;
	rot_lastMoveX = moveX;
	rot_lastMoveY = moveY;

	// Print 1 vez por segundo, no por llamada
	DWORD now = GetTickCount();
	if (now - rot_lastPrint >= 1000) {
		log_debug("AimRotate: calls=%d max=%.3fms lastMove=(%d,%d)", rot_calls, rot_maxMs, rot_lastMoveX, rot_lastMoveY);
		rot_calls = 0;
		rot_maxMs = 0.0f;
		rot_lastPrint = now;
	}
}

bool AimAssist::getAimPoint(double targetX, double targetY, double targetZ, double& aimX, double& aimY, double& aimZ)
{
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();
	float localYaw = player.getYaw();

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
			ui::SliderFloat("Speed", &settings.speed, 1.0f, 150.0f, "%.0f");
			ui::SliderInt("FOV", &settings.fov, 10, 360, "%d");
			ui::SliderFloat("Distance", &settings.distance, 3.0f, 8.0f, "%.1f");
			ui::SliderInt("Reaction Time", &settings.reactionTime, 0, 200, "%d ms");
		},
		"Options", [this, &currentMode]() {
			ui::Checkbox("Always Aim", &settings.alwaysAim);
			ui::Checkbox("Horizontal Only", &settings.horizontalOnly);
			ui::Checkbox("Lock Aim", &settings.lockAim);
			ui::Checkbox("Break Blocks", &settings.breakBlocks);
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
