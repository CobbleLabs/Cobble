#include "hitselect.h"
#include "../../sdk/core/minecraft.h"
#include "../../util/logger.hpp"
#include "../../ui/menu/menu.h"
#include "../../ui/core/overlay.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

HitSelect* hitSelect = nullptr;

// Weapon item IDs (shared with AutoClicker)
namespace HitSelectItems {
	constexpr int WOOD_SWORD = 268;
	constexpr int STONE_SWORD = 272;
	constexpr int IRON_SWORD = 267;
	constexpr int DIAMOND_SWORD = 276;
	constexpr int GOLD_SWORD = 283;
	
	constexpr int WOOD_AXE = 271;
	constexpr int STONE_AXE = 275;
	constexpr int IRON_AXE = 258;
	constexpr int DIAMOND_AXE = 279;
	constexpr int GOLD_AXE = 286;
}

HitSelect::HitSelect() : Module("HitSelect")
{
	keybind = 0;
	
	std::random_device rd;
	rng.seed(rd());
}

void HitSelect::onEnable()
{
	lastAllowedAttack = std::chrono::steady_clock::now();
	lastBlockedClick = std::chrono::steady_clock::now();
	cachedTargetHurtTime = 0;
	hasTarget = false;
}

void HitSelect::onDisable()
{
	// Nothing to clean up
}

void HitSelect::onUpdate()
{
	if (!enabled) return;
	
	// Update cached target state each tick
	Minecraft mc;
	hasTarget = mc.isTargetingEntity();
	
	if (hasTarget) {
		cachedTargetHurtTime = mc.getTargetedEntityHurtTime();
	} else {
		cachedTargetHurtTime = 0;
	}
}

bool HitSelect::shouldAllowClick()
{
	if (!enabled) return true;
	
	// Check weapon condition
	if (settings.weaponOnly && !isHoldingWeapon()) {
		return true; // Let non-weapon clicks through
	}
	
	auto now = std::chrono::steady_clock::now();
	
	// PAUSE MODE: Only allow one attack per pauseDuration
	if (settings.mode == Mode::Pause) {
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - lastAllowedAttack).count();
		
		if (elapsed >= settings.pauseDurationMs) {
			lastAllowedAttack = now;
			return true;
		}
		
		// Click blocked - do fake swing if enabled
		if (settings.fakeSwing) {
			doFakeSwing();
		}
		return false;
	}
	
	// BURST MODE: Only attack when target can take damage (hurtTime == 0)
	if (settings.mode == Mode::Burst) {
		// Read target state in real-time for accurate timing
		Minecraft mc;
		bool targetingEntity = mc.isTargetingEntity();
		int targetHurtTime = targetingEntity ? mc.getTargetedEntityHurtTime() : 0;
		
		// DEBUG: Log every check to see what's happening
		static int logThrottle = 0;
		if (++logThrottle >= 20) { // Log every ~20 clicks to avoid spam
			log_debug("[HitSelect] targeting=%d hurtTime=%d", targetingEntity ? 1 : 0, targetHurtTime);
			logThrottle = 0;
		}
		
		// Not targeting entity - use missed swings rate
		if (!targetingEntity) {
			// Attacking air - check missedSwingsRate
			if (checkCancelRate(settings.missedSwingsRate)) {
				if (settings.fakeSwing) {
					doFakeSwing();
				}
				return false;
			}
			return true;
		}
		
		// Targeting entity - check if they can take damage
		// hurtTime > 0 means they have i-frames and can't take damage
		if (targetHurtTime > 0) {
			log_debug("[HitSelect] BLOCKED: hurtTime=%d", targetHurtTime);
			// Target has i-frames, cancel the click
			if (checkCancelRate(settings.cancelRate)) {
				if (settings.fakeSwing) {
					doFakeSwing();
				}
				return false;
			}
		} else {
			log_debug("[HitSelect] ALLOWED: hurtTime=0");
		}
		
		// Target can take damage (hurtTime == 0) or RNG passed
		lastAllowedAttack = now;
		return true;
	}
	
	return true;
}

bool HitSelect::isTargetingEntity()
{
	return hasTarget;
}

bool HitSelect::isHoldingWeapon()
{
	int id = getHeldItemId();
	if (id == -1) return false;
	
	// Swords
	if (id == HitSelectItems::WOOD_SWORD || id == HitSelectItems::STONE_SWORD ||
		id == HitSelectItems::IRON_SWORD || id == HitSelectItems::DIAMOND_SWORD ||
		id == HitSelectItems::GOLD_SWORD)
		return true;

	// Axes
	if (id == HitSelectItems::WOOD_AXE || id == HitSelectItems::STONE_AXE ||
		id == HitSelectItems::IRON_AXE || id == HitSelectItems::DIAMOND_AXE ||
		id == HitSelectItems::GOLD_AXE)
		return true;

	return false;
}

int HitSelect::getHeldItemId()
{
	return player.getHeldItemId();
}

bool HitSelect::checkCancelRate(float rate)
{
	if (rate >= 100.0f) return true;
	if (rate <= 0.0f) return false;
	
	std::uniform_real_distribution<float> dist(0.0f, 100.0f);
	return dist(rng) < rate;
}

void HitSelect::doFakeSwing()
{
	// Send a quick click that the game interprets as a swing animation
	// but since we're blocking the "real" attack, no damage is dealt
	// This only works visually on client side
	
	// Note: True fake swing would require JNI call to swingItem()
	// For now, we skip this as it would require SDK extension
	// The visual effect is that the player "hesitates" which can look suspicious
	// A future improvement would be to add swingItem() to SDK
	
	// Alternative: Just let the blocked state show (no animation)
	// This is actually more natural looking for hit-select style play
}

void HitSelect::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			
			const char* modeItems[] = { "Pause", "Burst" };
			int modeIdx = static_cast<int>(settings.mode);
			if (ui::Combo("Mode", &modeIdx, modeItems, 2)) {
				settings.mode = static_cast<Mode>(modeIdx);
			}
			
			ImGui::Spacing();
			
			if (settings.mode == Mode::Pause) {
				ui::SliderInt("Pause (ms)", &settings.pauseDurationMs, 100, 1000, "%d ms");
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("Time between attacks");
				ImGui::PopStyleColor();
			}
			else if (settings.mode == Mode::Burst) {
				ui::SliderFloat("Cancel Rate", &settings.cancelRate, 0.0f, 100.0f, "%.0f%%");
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("Chance to block hits during i-frames");
				ImGui::PopStyleColor();
				
				ImGui::Spacing();
				
				ui::SliderFloat("Missed Swings", &settings.missedSwingsRate, 0.0f, 100.0f, "%.0f%%");
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("Cancel rate for swings at air");
				ImGui::PopStyleColor();
			}
		},
		"Options", [this]() {
			ui::Checkbox("Fake Swing", &settings.fakeSwing);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Show swing animation when blocked");
			ImGui::PopStyleColor();
			
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			
			ui::Checkbox("Weapon Only", &settings.weaponOnly);
			ui::Checkbox("Target Player Only", &settings.targetingPlayerOnly);
		}
	);
}

std::string HitSelect::getDisplayInfo()
{
	return settings.mode == Mode::Pause ? "Pause" : "Burst";
}
