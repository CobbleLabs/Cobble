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

AutoRefill* autoRefill = nullptr;

static void sendKey(WORD vk) {
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return;
	
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
	if (mc.isInGui()) {
		enabled = false;
		return;
	}
	
	scanForRefills();
	
	if (pendingRefills.empty()) {
		enabled = false;
		return;
	}
	
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
	if (mc.isInventoryOpen()) {
		sendKey(VK_ESCAPE);
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
			if (mc.isInventoryOpen()) {
				log_debug("AutoRefill: Inventory opened, starting refill. Pending: %d", (int)pendingRefills.size());
				stateStartTime = now;
				state = State::Refilling;
				lastMoveTime = now - std::chrono::milliseconds(settings.speed);
			} else if (elapsed > 1000) {
				log_warn("AutoRefill: Timeout waiting for inventory to open");
				finishRefill();
			}
			break;
		}
		
		case State::Refilling: {
			// Wait a bit after inventory opened
			if (elapsed < DELAY_AFTER_OPEN) return;
			
			if (!mc.isInventoryOpen()) {
				log_warn("AutoRefill: Inventory closed unexpectedly! Remaining: %d", (int)pendingRefills.size());
				finishRefill();
				return;
			}
			
			if (pendingRefills.empty()) {
				log_debug("AutoRefill: All tasks completed, closing");
				state = State::Closing;
				stateStartTime = now;
				return;
			}
			
			auto moveElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
			if (moveElapsed < settings.speed) return;
			
			RefillTask task = pendingRefills.front();
			pendingRefills.erase(pendingRefills.begin());
			
			log_debug("AutoRefill: Moving slot %d -> hotbar (remaining: %d)", task.sourceSlot, (int)pendingRefills.size());
			
			// Shift-click the source slot (button 0 = left click)
			mc.windowClick(task.sourceSlot, 0);
			lastMoveTime = now;
			break;
		}
		
		case State::Closing: {
			if (elapsed >= DELAY_BEFORE_CLOSE) {
				// Validation pass: Check if we actually finished
				scanForRefills();
				
				if (!pendingRefills.empty()) {
					log_warn("AutoRefill: Validation failed! %d items missing. Retrying...", (int)pendingRefills.size());
					state = State::Refilling;
					lastMoveTime = now;
					return;
				}
				
				log_debug("AutoRefill: Finished with verification, closing inventory");
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
	log_debug("AutoRefill: getInventorySlots returned %d slots", (int)slots.size());
	
	if (slots.empty()) {
		log_warn("AutoRefill: No slots returned!");
		return;
	}
	
	// First, collect all refillable items from main inventory (slots 9-35)
	std::vector<int> availableSources;
	for (const auto& slot : slots) {
		if (slot.slotId < 9 || slot.slotId > 35) continue;
		if (slot.itemId == 0) continue;
		
		bool isRefillable = false;
		if (settings.refillSoup && slot.itemId == RefillItems::MUSHROOM_SOUP) {
			isRefillable = true;
		}
		if (settings.refillPotions && slot.itemId == RefillItems::POTION) {
			isRefillable = true;
		}
		
		if (isRefillable) {
			availableSources.push_back(slot.slotId);
			log_debug("AutoRefill: Found refillable at slot %d (itemId=%d)", slot.slotId, slot.itemId);
		}
	}
	
	log_debug("AutoRefill: Total refillable sources: %d", (int)availableSources.size());
	
	if (availableSources.empty()) {
		log_warn("AutoRefill: No refillable items in inventory!");
		return;
	}
	
	// Find empty hotbar slots and assign sources
	int emptyHotbarCount = 0;
	size_t sourceIdx = 0;
	for (int hotbarSlot = 0; hotbarSlot <= 8 && sourceIdx < availableSources.size(); hotbarSlot++) {
		// Check if hotbar slot is empty
		bool isEmpty = true;
		for (const auto& slot : slots) {
			if (slot.slotId == hotbarSlot && slot.itemId != 0) {
				isEmpty = false;
				break;
			}
		}
		
		if (isEmpty) {
			emptyHotbarCount++;
			RefillTask task;
			task.hotbarSlot = hotbarSlot;
			task.sourceSlot = availableSources[sourceIdx];
			pendingRefills.push_back(task);
			log_debug("AutoRefill: Queue refill: source=%d -> hotbar=%d", task.sourceSlot, task.hotbarSlot);
			sourceIdx++;
		}
	}
	
	log_debug("AutoRefill: Empty hotbar slots: %d, Tasks queued: %d", emptyHotbarCount, (int)pendingRefills.size());
}

