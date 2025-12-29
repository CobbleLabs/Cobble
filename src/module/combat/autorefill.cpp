#include "autorefill.h"
#include "../../sdk/minecraft.h"
#include "../../util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

AutoRefill autoRefill;

// Simple keypress simulation
static void sendKey(WORD vk) {
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return;
	
	// Use SendInput for more reliable key simulation
	INPUT inputs[2] = {};
	
	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = vk;
	
	inputs[1].type = INPUT_KEYBOARD;
	inputs[1].ki.wVk = vk;
	inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
	
	SendInput(2, inputs, sizeof(INPUT));
}

AutoRefill::AutoRefill() : Module("AutoRefill")
{
	keybind = 'G';
}

void AutoRefill::onEnable()
{
	// Don't start if already in GUI
	if (mc.isInGui()) {
		enabled = false;
		return;
	}
	
	// Scan what needs refilling
	scanForRefills();
	
	if (pendingRefills.empty()) {
		enabled = false;
		return;
	}
	
	// Open inventory with E key
	sendKey('E');
	
	state = State::Waiting;
	stateStartTime = std::chrono::steady_clock::now();
}

void AutoRefill::onDisable()
{
	state = State::Idle;
	pendingRefills.clear();
}

void AutoRefill::finishRefill()
{
	// Close inventory if still open
	if (mc.isInventoryOpen()) {
		sendKey(VK_ESCAPE);  // ESC is more reliable than E to close
	}
	
	state = State::Idle;
	pendingRefills.clear();
	enabled = false;
}

void AutoRefill::onUpdate()
{
	if (!player.isValid()) return;
	if (state == State::Idle) return;
	
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStartTime).count();
	
	switch (state) {
		case State::Idle:
			break;
			
		case State::Waiting: {
			// Wait for inventory to open (max 1 second)
			if (mc.isInventoryOpen()) {
				state = State::Opening;
				stateStartTime = now;
			} else if (elapsed > 1000) {
				// Timeout
				finishRefill();
			}
			break;
		}
		
		case State::Opening: {
			// Wait delay after inventory opened
			if (elapsed >= settings.delayAfterOpen) {
				state = State::Refilling;
				lastMoveTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(settings.speed);
			}
			break;
		}
		
		case State::Refilling: {
			// Check inventory still open
			if (!mc.isInventoryOpen()) {
				finishRefill();
				return;
			}
			
			if (pendingRefills.empty()) {
				state = State::Closing;
				stateStartTime = now;
				return;
			}
			
			// Check timing between moves
			auto moveElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
			
			int delay = settings.speed;
			if (settings.smartSpeed && pendingRefills.size() > 0) {
				delay = calculateSmartDelay(pendingRefills.front().sourceSlot, pendingRefills.front().hotbarSlot);
			}
			
			if (moveElapsed < delay) return;
			
			// Do one move
			RefillTask task = pendingRefills.front();
			pendingRefills.erase(pendingRefills.begin());
			
			// Shift-click the source slot
			mc.windowClick(task.sourceSlot, settings.simulateButton);
			
			lastMoveTime = now;
			break;
		}
		
		case State::Closing: {
			if (elapsed >= settings.delayBeforeClose) {
				finishRefill();
			}
			break;
		}
	}
}

void AutoRefill::scanForRefills()
{
	pendingRefills.clear();
	
	auto slots = mc.getInventorySlots();
	if (slots.empty()) return;
	
	// Find empty hotbar slots
	for (int hotbarSlot = 0; hotbarSlot <= 8; hotbarSlot++) {
		int currentItemId = 0;
		for (const auto& slot : slots) {
			if (slot.slotId == hotbarSlot) {
				currentItemId = slot.itemId;
				break;
			}
		}
		
		// If empty, look for replacement
		if (currentItemId == 0) {
			int sourceSlot = -1;
			int itemId = 0;
			
			// Try soup first
			if (settings.refillSoup) {
				sourceSlot = findReplacementSlot(RefillItems::MUSHROOM_SOUP, false);
				if (sourceSlot != -1) itemId = RefillItems::MUSHROOM_SOUP;
			}
			
			// Then potions
			if (sourceSlot == -1 && settings.refillPotions) {
				sourceSlot = findReplacementSlot(RefillItems::POTION, !settings.useNonHealthPotions);
				if (sourceSlot != -1) itemId = RefillItems::POTION;
			}
			
			if (sourceSlot != -1) {
				RefillTask task;
				task.hotbarSlot = hotbarSlot;
				task.sourceSlot = sourceSlot;
				task.itemId = itemId;
				pendingRefills.push_back(task);
			}
		}
	}
}

bool AutoRefill::isRefillableItem(int itemId, bool isHealthPotion)
{
	if (itemId == RefillItems::MUSHROOM_SOUP && settings.refillSoup) return true;
	if (itemId == RefillItems::POTION && settings.refillPotions) {
		if (!settings.useNonHealthPotions && !isHealthPotion) return false;
		return true;
	}
	return false;
}

int AutoRefill::findReplacementSlot(int itemId, bool requireHealthPotion)
{
	auto slots = mc.getInventorySlots();
	
	for (const auto& slot : slots) {
		if (slot.slotId < 9 || slot.slotId > 35) continue;
		if (slot.itemId != itemId) continue;
		if (requireHealthPotion && !slot.isHealthPotion) continue;
		return slot.slotId;
	}
	
	return -1;
}

int AutoRefill::calculateSmartDelay(int fromSlot, int toSlot)
{
	int fromRow = (fromSlot < 9) ? 0 : ((fromSlot - 9) / 9 + 1);
	int toRow = (toSlot < 9) ? 0 : ((toSlot - 9) / 9 + 1);
	int fromCol = (fromSlot < 9) ? fromSlot : ((fromSlot - 9) % 9);
	int toCol = (toSlot < 9) ? toSlot : ((toSlot - 9) % 9);
	
	int dist = abs(fromRow - toRow) + abs(fromCol - toCol);
	return settings.speed + dist * 15;
}
