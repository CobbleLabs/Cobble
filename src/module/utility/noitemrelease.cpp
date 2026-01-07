#include "noitemrelease.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

NoItemRelease* noItemRelease = nullptr;

// Minecraft item IDs
namespace ItemID {
	// Swords
	constexpr int WOOD_SWORD = 268;
	constexpr int STONE_SWORD = 272;
	constexpr int IRON_SWORD = 267;
	constexpr int DIAMOND_SWORD = 276;
	constexpr int GOLD_SWORD = 283;
	
	// Food
	constexpr int APPLE = 260;
	constexpr int BREAD = 297;
	constexpr int PORK = 319;
	constexpr int BEEF = 364;
	constexpr int CHICKEN = 365;
	constexpr int FISH = 349;
	constexpr int CARROT = 391;
	constexpr int POTATO = 392;
	constexpr int CAKE = 354;
	constexpr int COOKIE = 357;
	constexpr int MELON = 360;
	constexpr int STEAK = 364;
	constexpr int COOKED_PORK = 320;
	constexpr int COOKED_CHICKEN = 366;
	constexpr int COOKED_FISH = 350;
	constexpr int BAKED_POTATO = 393;
	constexpr int GOLDEN_APPLE = 322;
	constexpr int GOLDEN_CARROT = 396;
	constexpr int MUSHROOM_STEW = 282;
	
	// Potions
	constexpr int POTION = 373;
	
	// Blocks (commonly used)
	constexpr int STONE = 1;
	constexpr int COBBLESTONE = 4;
	constexpr int WOOD = 5;
	constexpr int SANDSTONE = 24;
	constexpr int WOOL = 35;
	
	// Bow & Fishing Rod (excluded)
	constexpr int BOW = 261;
	constexpr int FISHING_ROD = 346;
}

NoItemRelease::NoItemRelease() : Module("NoItemRelease")
{
	keybind = 0;
	wasUsingItem = false;
}

void NoItemRelease::onEnable()
{
	wasUsingItem = false;
}

void NoItemRelease::onDisable()
{
	wasUsingItem = false;
}

bool NoItemRelease::isConsumable(int itemId)
{
	using namespace ItemID;
	
	// Food
	if (itemId == APPLE || itemId == BREAD || itemId == PORK || itemId == BEEF ||
		itemId == CHICKEN || itemId == FISH || itemId == CARROT || itemId == POTATO ||
		itemId == CAKE || itemId == COOKIE || itemId == MELON || itemId == STEAK ||
		itemId == COOKED_PORK || itemId == COOKED_CHICKEN || itemId == COOKED_FISH ||
		itemId == BAKED_POTATO || itemId == GOLDEN_APPLE || itemId == GOLDEN_CARROT ||
		itemId == MUSHROOM_STEW) {
		return true;
	}
	
	// Potions
	if (itemId == POTION) {
		return true;
	}
	
	// Blocks (ID range 1-255 typically)
	// Simple heuristic: items 1-255 are mostly blocks
	if (itemId >= 1 && itemId <= 255) {
		// Exclude bow and fishing rod
		if (itemId != BOW && itemId != FISHING_ROD) {
			return true;
		}
	}
	
	return false;
}

bool NoItemRelease::isSword(int itemId)
{
	using namespace ItemID;
	return (itemId == WOOD_SWORD || itemId == STONE_SWORD || itemId == IRON_SWORD ||
			itemId == DIAMOND_SWORD || itemId == GOLD_SWORD);
}

bool NoItemRelease::isBowOrRod(int itemId)
{
	using namespace ItemID;
	return (itemId == BOW || itemId == FISHING_ROD);
}

bool NoItemRelease::shouldBlockRelease()
{
	int heldItemId = player.getHeldItemId();
	if (heldItemId == -1) return false; // Empty hand
	
	// Never block bow or fishing rod
	if (isBowOrRod(heldItemId)) return false;
	
	switch (settings.mode) {
		case ItemReleaseMode::CONSUMABLE:
			return isConsumable(heldItemId);
		
		case ItemReleaseMode::SWORD:
			return isSword(heldItemId);
		
		case ItemReleaseMode::ALL:
			return true; // All items except bow/rod
		
		default:
			return false;
	}
}

void NoItemRelease::onUpdate()
{
	if (!enabled) {
		wasUsingItem = false;
		return;
	}
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;
	
	// Get actual mouse state
	bool actualRightClick = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	
	// Check what item we're holding
	bool shouldKeepUsing = shouldBlockRelease();
	
	// Simple state tracking for packet blocker:
	// - When user presses right-click with valid item -> set flag
	// - Packet interceptor will block RELEASE_USE_ITEM packets while flag is set
	// - User can release right-click and move freely (client-side)
	// - Server never receives release packet, continues item usage
	
	if (actualRightClick && shouldKeepUsing) {
		// User is actively using a valid item
		wasUsingItem = true;
	}
	else if (!actualRightClick && shouldKeepUsing) {
		// User released right-click, but we should block the release packet
		// Keep the flag set so packet blocker knows to cancel RELEASE_USE_ITEM
		// Don't simulate any input - let user move freely
		wasUsingItem = true;
	}
	else {
		// Reset if we're not holding a valid item anymore
		wasUsingItem = false;
	}
}

void NoItemRelease::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			
			const char* modeNames[] = { "Consumable", "Sword", "All" };
			int currentMode = (int)settings.mode;
			if (ImGui::Combo("Mode", &currentMode, modeNames, IM_ARRAYSIZE(modeNames))) {
				settings.mode = (ItemReleaseMode)currentMode;
			}
		},
		"Info", [this]() {
			ImGui::TextWrapped("Prevents item release packet after using items.");
			ImGui::Spacing();
			ImGui::TextWrapped("Consumable: Food, potions, blocks");
			ImGui::TextWrapped("Sword: Swords only (blocking)");
			ImGui::TextWrapped("All: All items (except bow/fishing rod)");
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Note:");
			ImGui::TextWrapped("Allows you to release right-click while server thinks you're still using the item.");
		}
	);
}
