#include "autorefill.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/widgets/widgets.hpp"

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
	keybind = 0;  // No default keybind
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
				stateStartTime = now;
				state = State::Refilling;
				lastMoveTime = now - std::chrono::milliseconds(settings.speed);
			} else if (elapsed > 1000) {
				finishRefill();
			}
			break;
		}
		
		case State::Refilling: {
			if (elapsed < DELAY_AFTER_OPEN) return;
			
			if (!mc.isInventoryOpen()) {
				finishRefill();
				return;
			}
			
			if (pendingRefills.empty()) {
				state = State::Closing;
				stateStartTime = now;
				return;
			}
			
			auto moveElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
			if (moveElapsed < settings.speed) return;
			
			RefillTask task = pendingRefills.front();
			pendingRefills.erase(pendingRefills.begin());
			
			mc.windowClick(task.sourceSlot, 0);
			lastMoveTime = now;
			break;
		}
		
		case State::Closing: {
			if (elapsed >= DELAY_BEFORE_CLOSE) {
				// Validation: retry if items are still missing
				scanForRefills();
				
				if (!pendingRefills.empty()) {
					state = State::Refilling;
					lastMoveTime = now;
					return;
				}
				
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
	
	// Collect all refillable items from main inventory (slots 9-35)
	std::vector<int> availableSources;
	for (const auto& slot : slots) {
		if (slot.slotId < 9 || slot.slotId > 35) continue;
		if (slot.itemId == 0) continue;
		
		bool isRefillable = false;
		if (settings.refillSoup && slot.itemId == RefillItems::MUSHROOM_SOUP) {
			isRefillable = true;
		}
		// Only refill Instant Health 2 splash potions (damage value 16421)
		if (settings.refillPotions && slot.itemId == RefillItems::POTION && slot.damage == 16421) {
			isRefillable = true;
		}
		
		if (isRefillable) {
			availableSources.push_back(slot.slotId);
		}
	}
	
	if (availableSources.empty()) return;
	
	// Find empty hotbar slots and assign sources
	size_t sourceIdx = 0;
	for (int hotbarSlot = 0; hotbarSlot <= 8 && sourceIdx < availableSources.size(); hotbarSlot++) {
		bool isEmpty = true;
		for (const auto& slot : slots) {
			if (slot.slotId == hotbarSlot && slot.itemId != 0) {
				isEmpty = false;
				break;
			}
		}
		
		if (isEmpty) {
			RefillTask task;
			task.hotbarSlot = hotbarSlot;
			task.sourceSlot = availableSources[sourceIdx];
			pendingRefills.push_back(task);
			sourceIdx++;
		}
	}
}

void AutoRefill::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::Binder("Keybind", &keybind);
			ImGui::Spacing();
			int speed = settings.speed;
			if (ui::SliderInt("Speed (ms)", &speed, 50, 200, "%d"))
				settings.speed = speed;
		},
		"Filters", [this]() {
			ui::Checkbox("Refill Soup", &settings.refillSoup);
			ui::Checkbox("Refill Potions", &settings.refillPotions);
		}
	);
}
