#include "velocity.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <random>
#include <cmath>
#include <algorithm>

Velocity* velocity = nullptr;

static std::mt19937 rng(std::random_device{}());

Velocity::Velocity() : Module("Velocity")
{
	keybind = 0;
	locked = true;
}

void Velocity::onEnable()
{
	lastHurtTime = 0;
	ticksSinceHit = 0;
	wasHitThisTick = false;
	pending.active = false;
	microDelayStart = 0;
}

void Velocity::onDisable()
{
}

bool Velocity::checkChance(float chance)
{
	if (chance >= 100.0f) return true;
	if (chance <= 0.0f) return false;
	std::uniform_real_distribution<float> dist(0.0f, 100.0f);
	return dist(rng) < chance;
}

bool Velocity::checkFOV()
{
	// 360 FOV means accept all angles
	if (settings.fov >= 360.0f) return true;
	
	// Get angle to attacker - simplified: check if we're facing the direction knockback came from
	// Since we don't have direct attacker info, we use motion direction as approximation
	double motionX = player.getMotionX();
	double motionZ = player.getMotionZ();
	
	if (motionX == 0 && motionZ == 0) return true;
	
	// Calculate knockback direction angle
	float kbAngle = (float)(atan2(-motionX, motionZ) * 180.0 / 3.14159265);
	float playerYaw = player.getYaw();
	
	// Normalize angles
	while (kbAngle < 0) kbAngle += 360;
	while (playerYaw < 0) playerYaw += 360;
	
	// Calculate angle difference (knockback from front = 180 degrees from motion)
	float angleDiff = abs(kbAngle - playerYaw);
	if (angleDiff > 180) angleDiff = 360 - angleDiff;
	
	// Check if within FOV (knockback from front = ~180 diff, from back = ~0 diff)
	// FOV 180 = only reduce from front (angleDiff > 90)
	float halfFov = settings.fov / 2.0f;
	return angleDiff >= (180.0f - halfFov);
}

bool Velocity::isLookingAtPlayer()
{
	return mc.isTargetingEntity();
}

bool Velocity::isHoldingWeapon()
{
	int itemId = player.getHeldItemId();
	// Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
	// Axes: 271, 275, 258, 279, 286
	return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
			itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

bool Velocity::checkConditions()
{
	// Check all enabled conditions
	if (settings.onlyLookingAtPlayer && !isLookingAtPlayer()) return false;
	
	if (settings.onlyMousePressed) {
		if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return false;
	}
	
	if (settings.onlyMovingForward) {
		if (!(GetAsyncKeyState('W') & 0x8000)) return false;
	}
	
	if (settings.onlyHoldingWeapon && !isHoldingWeapon()) return false;
	
	if (settings.onlySprinting && !player.isSprinting()) return false;
	
	return true;
}

void Velocity::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;

	int currentHurtTime = player.getHurtTime();
	
	// Detect hit (hurtTime goes from 0 to max on hit)
	bool justGotHit = currentHurtTime > 0 && currentHurtTime > lastHurtTime;
	
	if (justGotHit) {
		ticksSinceHit = 0;
		wasHitThisTick = true;
	} else if (wasHitThisTick) {
		ticksSinceHit++;
		if (currentHurtTime == 0) {
			wasHitThisTick = false;
		}
	}
	
	// Only process on the exact hit frame (+ delay)
	bool shouldProcess = justGotHit && (settings.reduceDelay == 0);
	if (!shouldProcess && wasHitThisTick && ticksSinceHit == settings.reduceDelay) {
		shouldProcess = true;
	}
	
	if (shouldProcess) {
		// Check FOV filter
		if (!checkFOV()) {
			lastHurtTime = currentHurtTime;
			return;
		}
		
		// Check conditions
		if (!checkConditions()) {
			lastHurtTime = currentHurtTime;
			return;
		}
		
		// Apply based on mode
		switch (settings.mode) {
			case Mode::Regular: {
				// Check chance
				if (!checkChance(settings.chance)) break;
				
				// Randomize percentages with variance
				std::uniform_real_distribution<float> varDist(-settings.percentageVariance, settings.percentageVariance);
				float hPercent = settings.horizontal + varDist(rng);
				float vPercent = settings.vertical + varDist(rng);
				
				// Clamp to valid range
				hPercent = std::max(0.0f, std::min(100.0f, hPercent));
				vPercent = std::max(0.0f, std::min(100.0f, vPercent));
				
				// Micro-delay variance
				if (settings.microDelayMs > 0) {
					std::uniform_int_distribution<int> delayDist(0, settings.microDelayMs);
					microDelayTarget = delayDist(rng);
					microDelayStart = GetTickCount();
				} else {
					microDelayTarget = 0;
				}
				
				// Check if we should apply now or wait for micro-delay
				DWORD elapsed = (microDelayTarget > 0) ? (GetTickCount() - microDelayStart) : 999;
				if (elapsed < (DWORD)microDelayTarget) {
					// Wait for micro-delay
					break;
				}
				
				// Gradual application (spread across multiple ticks)
				if (settings.tickSpread > 0) {
					pending.active = true;
					pending.totalTicks = settings.tickSpread + 1; // +1 for current tick
					pending.ticksRemaining = settings.tickSpread;
					
					// Calculate target motion after reduction
					double motionX = player.getMotionX();
					double motionY = player.getMotionY();
					double motionZ = player.getMotionZ();
					
					// Target = keep only hPercent% of original velocity
					pending.targetMotionX = motionX * (hPercent / 100.0);
					pending.targetMotionY = motionY * (vPercent / 100.0);
					pending.targetMotionZ = motionZ * (hPercent / 100.0);
					
					// Apply reduction gradually - move towards target by 1/totalTicks each tick
					double newX = motionX - ((motionX - pending.targetMotionX) / pending.totalTicks);
					double newY = motionY - ((motionY - pending.targetMotionY) / pending.totalTicks);
					double newZ = motionZ - ((motionZ - pending.targetMotionZ) / pending.totalTicks);
					
					player.setMotionX(newX);
					player.setMotionY(newY);
					player.setMotionZ(newZ);
				} else {
					// Instant application (no spread)
					pending.active = false; // Reset any pending gradual state
					
					if (hPercent < 100.0f) {
						double motionX = player.getMotionX();
						double motionZ = player.getMotionZ();
						player.setMotionX(motionX * (hPercent / 100.0));
						player.setMotionZ(motionZ * (hPercent / 100.0));
					}
					
					if (vPercent < 100.0f) {
						double motionY = player.getMotionY();
						player.setMotionY(motionY * (vPercent / 100.0));
					}
				}
				break;
			}
			
			case Mode::Jump: {
				// Check chance
				if (!checkChance(settings.jumpChance)) break;
				
				// Check sprint condition for jump mode
				if (settings.onlySprinting && !player.isSprinting()) break;
				
				// Jump immediately to negate knockback
				if (player.isOnGround()) {
					player.jump();
				}
				break;
			}
			
			case Mode::Reduce: {
				// Attack slowdown abuse - reduce motion by attacking
				if (!checkChance(settings.chance)) break;
				
				// Randomize percentages with variance (same as Regular)
				std::uniform_real_distribution<float> varDist(-settings.percentageVariance, settings.percentageVariance);
				float hPercent = settings.horizontal + varDist(rng);
				float vPercent = settings.vertical + varDist(rng);
				
				hPercent = std::max(0.0f, std::min(100.0f, hPercent));
				vPercent = std::max(0.0f, std::min(100.0f, vPercent));
				
				if (hPercent < 100.0f) {
					double motionX = player.getMotionX();
					double motionZ = player.getMotionZ();
					player.setMotionX(motionX * (hPercent / 100.0));
					player.setMotionZ(motionZ * (hPercent / 100.0));
				}
				
				if (vPercent < 100.0f) {
					double motionY = player.getMotionY();
					player.setMotionY(motionY * (vPercent / 100.0));
				}
				break;
			}
			
			case Mode::Ignore: {
				// Cancel velocity completely without losing speed
				if (!checkChance(settings.chance)) break;
				
				// Horizontal: set to 0 (ignore knockback)
				player.setMotionX(0);
				player.setMotionZ(0);
				
				// Vertical based on setting
				switch (settings.verticalMode) {
					case VerticalMode::Never:
						// Don't modify vertical velocity
						break;
					case VerticalMode::OnlyInAir:
						if (!player.isOnGround()) {
							player.setMotionY(0);
						}
						break;
					case VerticalMode::Always:
						player.setMotionY(0);
						break;
				}
				break;
			}
		}
	}
	
	// Continue gradual application if active
	if (pending.active && pending.ticksRemaining > 0) {
		double currentX = player.getMotionX();
		double currentY = player.getMotionY();
		double currentZ = player.getMotionZ();
		
		double xDiff = (currentX - pending.targetMotionX) / (pending.ticksRemaining + 1);
		double yDiff = (currentY - pending.targetMotionY) / (pending.ticksRemaining + 1);
		double zDiff = (currentZ - pending.targetMotionZ) / (pending.ticksRemaining + 1);
		
		player.setMotionX(currentX - xDiff);
		player.setMotionY(currentY - yDiff);
		player.setMotionZ(currentZ - zDiff);
		
		pending.ticksRemaining--;
		
		if (pending.ticksRemaining == 0) {
			pending.active = false;
		}
	}

	lastHurtTime = currentHurtTime;
}

float Velocity::getAttackerAngle()
{
	// This would require tracking the last attacker position
	// For now, return 0 (approximated in checkFOV using motion)
	return 0.0f;
}

void Velocity::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			
			// Mode selector
			const char* modes[] = { "Regular", "Jump", "Reduce", "Ignore" };
			int modeIdx = static_cast<int>(settings.mode);
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
			if (ImGui::BeginCombo("Mode", modes[modeIdx], ImGuiComboFlags_NoArrowButton)) {
				for (int i = 0; i < 4; i++) {
					bool selected = (modeIdx == i);
					if (ImGui::Selectable(modes[i], selected))
						settings.mode = static_cast<Mode>(i);
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			
			ImGui::Spacing();
			
			// Mode-specific settings
			if (settings.mode == Mode::Regular || settings.mode == Mode::Reduce) {
				ui::SliderFloat("Horizontal", &settings.horizontal, 0.0f, 100.0f, "%.0f%%");
				ui::SliderFloat("Vertical", &settings.vertical, 0.0f, 100.0f, "%.0f%%");
				ui::SliderFloat("Chance", &settings.chance, 0.0f, 100.0f, "%.0f%%");
				if (settings.mode == Mode::Reduce) {
					ui::Checkbox("Double Clicks", &settings.allowDoubleClicks);
				}
			}
			else if (settings.mode == Mode::Jump) {
				ui::SliderFloat("Jump Chance", &settings.jumpChance, 0.0f, 100.0f, "%.0f%%");
			}
			else if (settings.mode == Mode::Ignore) {
				ui::SliderFloat("Chance", &settings.chance, 0.0f, 100.0f, "%.0f%%");
				const char* vertModes[] = { "Never", "Only Air", "Always" };
				int vertIdx = static_cast<int>(settings.verticalMode);
				ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
				if (ImGui::BeginCombo("Vertical", vertModes[vertIdx], ImGuiComboFlags_NoArrowButton)) {
					for (int i = 0; i < 3; i++) {
						bool selected = (vertIdx == i);
						if (ImGui::Selectable(vertModes[i], selected))
							settings.verticalMode = static_cast<VerticalMode>(i);
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			
			ui::SliderFloat("FOV", &settings.fov, 0.0f, 360.0f, "%.0f");
			
			int delay = settings.reduceDelay;
			if (ui::SliderInt("Delay", &delay, 0, 5, "%d ticks"))
				settings.reduceDelay = delay;
		},
		"Settings", [this]() {
			// Anti-detection (only for Regular/Reduce modes)
			if (settings.mode == Mode::Regular || settings.mode == Mode::Reduce) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
				ImGui::Text("Anti-Detection");
				ImGui::PopStyleColor();
				ImGui::Spacing();
				
				ui::SliderFloat("Variance", &settings.percentageVariance, 0.0f, 10.0f, "%.1f%%");
				
				int spread = settings.tickSpread;
				if (ui::SliderInt("Tick Spread", &spread, 0, 3, "%d"))
					settings.tickSpread = spread;
				
				int microDelay = settings.microDelayMs;
				if (ui::SliderInt("Micro Delay", &microDelay, 0, 50, "%dms"))
					settings.microDelayMs = microDelay;
				
				ImGui::Spacing();
			}
			
			// Conditions
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
			ImGui::Text("Conditions");
			ImGui::PopStyleColor();
			ImGui::Spacing();
			
			ui::Checkbox("Looking at Player", &settings.onlyLookingAtPlayer);
			ui::Checkbox("Mouse Pressed", &settings.onlyMousePressed);
			ui::Checkbox("Moving Forward", &settings.onlyMovingForward);
			ui::Checkbox("Holding Weapon", &settings.onlyHoldingWeapon);
			if (settings.mode == Mode::Jump) {
				ui::Checkbox("Only Sprinting", &settings.onlySprinting);
			}
		}
	);
}
