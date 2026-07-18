#include "autorefill.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../movement/sprint.h"
#include "../../ui/core/overlay.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

AutoRefill* autoRefill = nullptr;

namespace {

void sendLwjglKeyState(int keyCode, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = static_cast<WORD>(keyCode);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &input, sizeof(INPUT));
}

void sendLwjglKey(int keyCode) {
    sendLwjglKeyState(keyCode, true);
    sendLwjglKeyState(keyCode, false);
}

bool playerInventorySlotScreenPosition(int inventorySlot, POINT& position) {
    if (!mc.isPlayerInventoryOpen() || !Overlay::gameWindow || GetForegroundWindow() != Overlay::gameWindow)
        return false;

    JNIEnv* env = Java::Env();
    if (!env || !JniCache::fid_currentScreen)
        return false;

    jobject screen = env->GetObjectField(mc.instance, JniCache::fid_currentScreen);
    if (!screen)
        return false;

    jclass guiScreen = nullptr;
    if (!Java::AssignClass("net.minecraft.client.gui.GuiScreen", guiScreen)) {
        env->DeleteLocalRef(screen);
        return false;
    }

    jfieldID width = env->GetFieldID(guiScreen, "width", "I");
    if (!width) {
        env->ExceptionClear();
        width = env->GetFieldID(guiScreen, "field_146294_l", "I");
    }
    jfieldID height = env->GetFieldID(guiScreen, "height", "I");
    if (!height) {
        env->ExceptionClear();
        height = env->GetFieldID(guiScreen, "field_146295_m", "I");
    }

    const int guiWidth = width ? env->GetIntField(screen, width) : 0;
    const int guiHeight = height ? env->GetIntField(screen, height) : 0;
    env->DeleteLocalRef(guiScreen);
    env->DeleteLocalRef(screen);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (guiWidth <= 0 || guiHeight <= 0 || inventorySlot < 0 || inventorySlot > 35)
        return false;

    RECT client{};
    if (!GetClientRect(Overlay::gameWindow, &client))
        return false;
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0)
        return false;

    const int guiLeft = (guiWidth - 176) / 2;
    const int guiTop = (guiHeight - 166) / 2;
    const int column = inventorySlot % 9;
    const int row = inventorySlot >= 9 ? (inventorySlot - 9) / 9 : 3;
    const int slotX = guiLeft + 16 + column * 18;
    const int slotY = guiTop + (inventorySlot >= 9 ? 92 + row * 18 : 150);

    position.x = slotX * clientWidth / guiWidth;
    position.y = slotY * clientHeight / guiHeight;
    return ClientToScreen(Overlay::gameWindow, &position) != FALSE;
}

}  // namespace

AutoRefill::AutoRefill() : Module("AutoRefill") {
    keybind = 0;  // No default keybind
}

bool AutoRefill::canToggle() {
    // Block toggle when in passive mode (onInventoryOpen)
    return !settings.onInventoryOpen;
}

bool AutoRefill::isPacketRefilling() const {
    return settings.mode == 0 && state != State::Idle && state != State::RestoreSprintDown &&
           state != State::RestoreSprintUp;
}

bool AutoRefill::startLegitMove(int slotId) {
    if (!playerInventorySlotScreenPosition(slotId, mouseTarget_))
        return false;
    if (!GetCursorPos(&mouseStart_))
        return false;
    return true;
}

bool AutoRefill::updateLegitMove(long long elapsed) {
    const int duration = (std::max)(1, settings.legitMoveTime);
    const float progress = (std::min)(1.0f, static_cast<float>(elapsed) / duration);
    const int x = mouseStart_.x + static_cast<int>((mouseTarget_.x - mouseStart_.x) * progress);
    const int y = mouseStart_.y + static_cast<int>((mouseTarget_.y - mouseStart_.y) * progress);
    SetCursorPos(x, y);
    return progress >= 1.0f;
}

void AutoRefill::onEnable() {
    // Manual mode: one-shot refill driven entirely by packets (no GUI, no SendInput).
    if (mc.isInGui()) {
        enabled = false;
        return;
    }

    scanForRefills();

    if (pendingRefills.empty()) {
        enabled = false;
        return;
    }

    if (settings.mode == 0) {
        // Fast packet mode stays silent when manually triggered. The passive
        // setting is handled only after the player has opened their inventory.
        if (sprint && !sprint->enabled)
            sprint->setEnabled(true);
        mc.releaseMovementKeys();
        movementPaused_ = true;
        mc.setKeyBindState(mc.getSprintKey(), false);
        mc.sendInventoryOpenPacket();
        state = State::PacketOpening;
    } else {
        sendLwjglKey(mc.getInventoryKey());
        state = State::LegitOpening;
    }
    stateStartTime = std::chrono::steady_clock::now();
    lastMoveTime = stateStartTime - std::chrono::milliseconds(settings.speed);
}

void AutoRefill::onDisable() {
    if (sprintKeyHeld_) {
        sendLwjglKeyState(mc.getSprintKey(), false);
        sprintKeyHeld_ = false;
    }
    if (movementPaused_) {
        mc.restoreMovementKeys();
        movementPaused_ = false;
    }
    state = State::Idle;
    pendingRefills.clear();
    hasActiveTask_ = false;
    pendingClickSlot_ = -1;
}

void AutoRefill::finishRefill() {
    if (movementPaused_) {
        mc.restoreMovementKeys();
        movementPaused_ = false;
    }
    state = State::Idle;
    pendingRefills.clear();
    hasActiveTask_ = false;
    pendingClickSlot_ = -1;

    // Passive mode stays enabled to catch the next inventory open.
    if (!settings.onInventoryOpen) {
        enabled = false;
    }

}

void AutoRefill::onUpdate() {
    if (!player.isValid())
        return;

    // Passive mode: refill when the user opens their inventory GUI.
    if (settings.onInventoryOpen && state == State::Idle) {
        bool inventoryOpen = mc.isInventoryOpen();

        if (inventoryOpen && !wasInventoryOpen) {
            scanForRefills();
            if (!pendingRefills.empty()) {
                state = settings.mode == 1 ? State::LegitMoveSource : State::Refilling;
                stateStartTime = std::chrono::steady_clock::now();
                lastMoveTime = stateStartTime - std::chrono::milliseconds(settings.speed);
            }
        }

        wasInventoryOpen = inventoryOpen;
    }

    if (state == State::Idle)
        return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStartTime).count();

    switch (state) {
        case State::Idle:
            break;

        case State::PacketOpening: {
            // Briefly settle movement before the first inventory action, then
            // restore the user's held keys and refill at the configured speed.
            if (elapsed < 75)
                return;
            if (movementPaused_) {
                mc.restoreMovementKeys();
                movementPaused_ = false;
            }
            state = State::Refilling;
            stateStartTime = now;
            lastMoveTime = now - std::chrono::milliseconds(settings.speed);
            break;
        }

        case State::Refilling: {
            if (elapsed < settings.speed)
                return;

            // Wait until the previous queued click has actually been flushed (fired
            // pre-flying in onOutgoingPacket) before moving to the next item.
            if (pendingClickSlot_ >= 0)
                return;

            if (pendingRefills.empty()) {
                state = State::Closing;
                stateStartTime = now;
                return;
            }

            auto moveElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
            if (moveElapsed < settings.speed)
                return;

            RefillTask task = pendingRefills.front();
            pendingRefills.erase(pendingRefills.begin());

            // Queue the click; it is sent from onOutgoingPacket right before the next
            // flying packet so it lands BEFORE movement in the tick (no Grim Post).
            pendingClickSlot_ = task.sourceSlot;
            lastMoveTime = now;
            break;
        }

        case State::LegitOpening: {
            if (mc.isPlayerInventoryOpen()) {
                state = settings.mode == 1 ? State::LegitMoveSource : State::Refilling;
                stateStartTime = now;
                lastMoveTime = now - std::chrono::milliseconds(settings.speed);
            } else if (elapsed > 1000) {
                finishRefill();
            }
            break;
        }

        case State::LegitMoveSource: {
            if (!mc.isPlayerInventoryOpen()) {
                finishRefill();
                return;
            }

            if (!hasActiveTask_) {
                const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
                if (wait < settings.speed)
                    return;
                if (pendingRefills.empty()) {
                    state = State::Closing;
                    stateStartTime = now;
                    return;
                }

                activeTask_ = pendingRefills.front();
                pendingRefills.erase(pendingRefills.begin());
                hasActiveTask_ = true;
                if (!startLegitMove(activeTask_.sourceSlot)) {
                    finishRefill();
                    return;
                }
                stateStartTime = now;
                return;
            }

            if (!updateLegitMove(elapsed))
                return;
            // Queue the click for the outgoing packet gate. It will be sent
            // immediately before the next movement packet, preserving vanilla
            // packet order while the cursor animation remains visible.
            pendingClickSlot_ = activeTask_.sourceSlot;
            state = State::LegitAwaitClick;
            stateStartTime = now;
            break;
        }

        case State::LegitAwaitClick: {
            if (!mc.isPlayerInventoryOpen() || !hasActiveTask_) {
                finishRefill();
                return;
            }
            // onOutgoingPacket clears this after sending it before C03.
            if (pendingClickSlot_ >= 0)
                return;

            hasActiveTask_ = false;
            lastMoveTime = now;
            state = State::LegitMoveSource;
            stateStartTime = now;
            break;
        }

        case State::RestoreSprintDown: {
            // Let the close packet leave first, then keep the key down long
            // enough for LWJGL and Toggle Sprint to observe a real key press.
            if (elapsed < 50)
                return;
            const int sprintKey = mc.getSprintKey();
            mc.setKeyBindState(sprintKey, true);
            sendLwjglKeyState(sprintKey, true);
            sprintKeyHeld_ = true;
            state = State::RestoreSprintUp;
            stateStartTime = now;
            break;
        }

        case State::RestoreSprintUp: {
            if (elapsed < 100)
                return;
            const int sprintKey = mc.getSprintKey();
            mc.setKeyBindState(sprintKey, false);
            sendLwjglKeyState(sprintKey, false);
            sprintKeyHeld_ = false;
            finishRefill();
            break;
        }

        case State::Closing: {
            if (elapsed < settings.speed)
                return;

            // Validation: re-scan and retry if items are still missing.
            scanForRefills();
            if (!pendingRefills.empty()) {
                state = settings.mode == 1 ? State::LegitMoveSource : State::Refilling;
                stateStartTime = now;
                lastMoveTime = now - std::chrono::milliseconds(settings.speed);
                return;
            }

            // In passive mode the player owns the inventory. Packet mode is
            // silent; Legit closes with the same real key used to open it.
            if (!settings.onInventoryOpen) {
                if (settings.mode == 0)
                    mc.sendInventoryClosePacket();
                else
                    sendLwjglKey(mc.getInventoryKey());
            }
            if (settings.mode == 0 && !settings.onInventoryOpen) {
                state = State::RestoreSprintDown;
                stateStartTime = now;
                break;
            }
            finishRefill();
            break;
        }
    }
}

void AutoRefill::onOutgoingPacket(JNIEnv* env, jobject packet) {
    if (pendingClickSlot_ < 0 || !env || !packet)
        return;

    if (!c03Resolved_) {
        c03Resolved_ = true;
        jclass local = nullptr;
        if (Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", local)) {
            c03Class_ = static_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (!c03Class_)
        return;

    if (env->ExceptionCheck())
        env->ExceptionClear();
    // Only flush right before a flying (movement) packet — C03PacketPlayer covers
    // C03/C04/C05/C06. Firing the window-click here queues its C0E synchronously,
    // so it reaches the wire ahead of this flying packet: vanilla ordering.
    if (!env->IsInstanceOf(packet, c03Class_))
        return;

    const int slot = pendingClickSlot_;
    pendingClickSlot_ = -1;
    mc.windowClick(slot, 0);  // sends the real C0E click packet on window 0
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void AutoRefill::scanForRefills() {
    pendingRefills.clear();

    auto slots = mc.getInventorySlots();
    if (slots.empty())
        return;

    // Collect all refillable items from main inventory (slots 9-35)
    std::vector<int> availableSources;
    for (const auto& slot : slots) {
        if (slot.slotId < 9 || slot.slotId > 35)
            continue;
        if (slot.itemId == 0)
            continue;

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

    if (availableSources.empty())
        return;

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

void AutoRefill::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::Binder("Keybind", &keybind);
            ImGui::Spacing();
            static const char* modes[] = {"Packet", "Legit"};
            ui::Combo("Mode", &settings.mode, modes, IM_ARRAYSIZE(modes));
            if (settings.mode == 1) {
                int moveTime = settings.legitMoveTime;
                if (ui::SliderInt("Mouse Move (ms)", &moveTime, 40, 500, "%d"))
                    settings.legitMoveTime = moveTime;
            }
            ImGui::Spacing();
            int speed = settings.speed;
            if (ui::SliderInt("Speed (ms)", &speed, 0, 200, "%d"))
                settings.speed = speed;
            ImGui::Spacing();
            ui::Checkbox("On Inventory Open", &settings.onInventoryOpen);
        },
        "Filters",
        [this]() {
            ui::Checkbox("Refill Soup", &settings.refillSoup);
            ui::Checkbox("Refill Potions", &settings.refillPotions);
        });
}

std::string AutoRefill::getDisplayInfo() {
    if (settings.mode == 1)
        return "Legit";
    return settings.onInventoryOpen ? "Inv Open" : "Manual";
}



void AutoRefill::saveSettings(json::Value& node) const {
    node["refillSoup"] = settings.refillSoup;
    node["refillPotions"] = settings.refillPotions;
    node["speed"] = settings.speed;
    node["mode"] = settings.mode;
    node["legitMoveTime"] = settings.legitMoveTime;
    node["onInventoryOpen"] = settings.onInventoryOpen;
}

void AutoRefill::loadSettings(const json::Value& node) {
    settings.refillSoup = node["refillSoup"].as_bool(settings.refillSoup);
    settings.refillPotions = node["refillPotions"].as_bool(settings.refillPotions);
    settings.speed = node["speed"].as_int(settings.speed);
    settings.mode = (std::max)(0, (std::min)(1, node["mode"].as_int(settings.mode)));
    settings.legitMoveTime = (std::max)(40, node["legitMoveTime"].as_int(settings.legitMoveTime));
    settings.onInventoryOpen = node["onInventoryOpen"].as_bool(settings.onInventoryOpen);
}
