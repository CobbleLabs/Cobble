#include "autorefill.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../movement/sprint.h"
#include "../../ui/core/overlay.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

AutoRefill* autoRefill = nullptr;

namespace {

constexpr int CLICK_COMMAND_EMPTY = -1;
constexpr int CLICK_COMMAND_CLAIMED = -2;
constexpr int CLICK_COMMAND_SILENT = 1 << 12;

int packClickCommand(int sourceSlot, int hotbarSlot, bool silent) {
    return (sourceSlot << 4) | hotbarSlot | (silent ? CLICK_COMMAND_SILENT : 0);
}

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
    keybind = 0;
}

bool AutoRefill::canToggle() {
    return !settings.automatic && !settings.onInventoryOpen;
}

bool AutoRefill::hasPendingClick() const {
    return pendingClickCommand_.load(std::memory_order_acquire) >= 0;
}

bool AutoRefill::hasClickWork() const {
    return pendingClickCommand_.load(std::memory_order_acquire) != CLICK_COMMAND_EMPTY;
}

bool AutoRefill::isRunning() const {
    return state != State::Idle;
}

bool AutoRefill::isPacketRefilling() const {
    return activeFlow_ == RunFlow::Packet && state != State::Idle && state != State::RestoreSprintDown &&
           state != State::RestoreSprintUp;
}

bool AutoRefill::startLegitMove(int slotId) {
    if (!playerInventorySlotScreenPosition(slotId, mouseTarget_))
        return false;
    return GetCursorPos(&mouseStart_) != FALSE;
}

bool AutoRefill::updateLegitMove(long long elapsed) {
    const int duration = (std::max)(1, settings.legitMoveTime);
    const float progress = (std::min)(1.0f, static_cast<float>(elapsed) / duration);
    const int x = mouseStart_.x + static_cast<int>((mouseTarget_.x - mouseStart_.x) * progress);
    const int y = mouseStart_.y + static_cast<int>((mouseTarget_.y - mouseStart_.y) * progress);
    SetCursorPos(x, y);
    return progress >= 1.0f;
}

void AutoRefill::startPacketFlow() {
    activeFlow_ = RunFlow::Packet;
    openedSilently_ = true;
    passiveRun_ = false;
    sprintTemporarilyEnabled_ = sprint && !sprint->enabled;
    if (sprintTemporarilyEnabled_) {
        sprint->enabled = true;
        sprint->onEnable();
    }
    mc.releaseMovementKeys();
    movementPaused_ = true;
    sprintKeyCode_ = mc.getSprintKey();
    mc.setKeyBindState(sprintKeyCode_, false);
    mc.sendInventoryOpenPacket();
    state = State::PacketOpening;
    stateStartTime = std::chrono::steady_clock::now();
    lastMoveTime = stateStartTime - std::chrono::milliseconds(settings.speed);
}

void AutoRefill::onEnable() {
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
        startPacketFlow();
    } else {
        activeFlow_ = RunFlow::Legit;
        openedSilently_ = false;
        passiveRun_ = false;
        sendLwjglKey(mc.getInventoryKey());
        state = State::LegitOpening;
        stateStartTime = std::chrono::steady_clock::now();
        lastMoveTime = stateStartTime - std::chrono::milliseconds(settings.speed);
    }
}

void AutoRefill::clearQueuedClick() {
    std::lock_guard<std::mutex> lock(pendingClickMutex_);
    pendingClickCommand_.store(CLICK_COMMAND_EMPTY, std::memory_order_release);
}

void AutoRefill::restoreInputState() {
    if (sprintKeyHeld_ && sprintKeyCode_ > 0) {
        sendLwjglKeyState(sprintKeyCode_, false);
        sprintKeyHeld_ = false;
    }
    if (movementPaused_) {
        mc.restoreMovementKeys();
        movementPaused_ = false;
    }
    if (sprintTemporarilyEnabled_) {
        if (sprint && sprint->enabled) {
            sprint->onDisable();
            sprint->enabled = false;
        }
        sprintTemporarilyEnabled_ = false;
    }
    sprintKeyCode_ = 0;
}

void AutoRefill::onDisable() {
    restoreInputState();
    state = State::Idle;
    pendingRefills.clear();
    hasActiveTask_ = false;
    clearQueuedClick();
    openedSilently_ = false;
    passiveRun_ = false;
    lastAutomaticScan_ = std::chrono::steady_clock::now();
}

void AutoRefill::onWorldChange() {
    clearQueuedClick();
    restoreInputState();
    state = State::Idle;
    enabled = false;
    pendingRefills.clear();
    hasActiveTask_ = false;
    wasInventoryOpen = false;
    openedSilently_ = false;
    passiveRun_ = false;
    lastAutomaticScan_ = {};
}

void AutoRefill::finishRefill() {
    restoreInputState();
    state = State::Idle;
    pendingRefills.clear();
    hasActiveTask_ = false;
    clearQueuedClick();
    if (!passiveRun_)
        enabled = false;
    openedSilently_ = false;
    passiveRun_ = false;
    lastAutomaticScan_ = std::chrono::steady_clock::now();
}

void AutoRefill::onUpdate() {
    if (!player.isValid())
        return;

    const auto now = std::chrono::steady_clock::now();

    if (settings.automatic && state == State::Idle && !mc.isInGui()) {
        const auto sinceLastScan =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAutomaticScan_).count();
        if (sinceLastScan >= 500) {
            lastAutomaticScan_ = now;
            const auto slots = mc.getInventorySlots();
            bool hasEligibleHotbarItem = false;
            for (const auto& slot : slots) {
                if (slot.slotId < 0 || slot.slotId > 8)
                    continue;
                if ((settings.refillSoup && slot.itemId == RefillItems::MUSHROOM_SOUP) ||
                    (settings.refillPotions && slot.itemId == RefillItems::POTION && slot.damage == 16421)) {
                    hasEligibleHotbarItem = true;
                    break;
                }
            }
            if (!hasEligibleHotbarItem) {
                scanForRefills(slots);
                if (!pendingRefills.empty())
                    startPacketFlow();
            }
        }
    }

    if (settings.onInventoryOpen && state == State::Idle) {
        const bool inventoryOpen = mc.isInventoryOpen();
        if (inventoryOpen && !wasInventoryOpen) {
            scanForRefills();
            if (!pendingRefills.empty()) {
                activeFlow_ = settings.mode == 1 ? RunFlow::Legit : RunFlow::Packet;
                openedSilently_ = false;
                passiveRun_ = true;
                state = activeFlow_ == RunFlow::Legit ? State::LegitMoveSource : State::Refilling;
                stateStartTime = now;
                lastMoveTime = now - std::chrono::milliseconds(settings.speed);
            }
        }
        wasInventoryOpen = inventoryOpen;
    }

    if (openedSilently_ && mc.isInGui()) {
        enabled = false;
        onDisable();
        return;
    }

    if (state == State::Idle)
        return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateStartTime).count();

    switch (state) {
        case State::Idle:
            break;

        case State::PacketOpening:
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

        case State::Refilling: {
            if (elapsed < settings.speed)
                return;
            if (hasClickWork())
                return;
            if (pendingRefills.empty()) {
                state = State::Closing;
                stateStartTime = now;
                return;
            }

            const auto moveElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
            if (moveElapsed < settings.speed)
                return;

            const RefillTask task = pendingRefills.front();
            pendingRefills.erase(pendingRefills.begin());
            pendingClickCommand_.store(packClickCommand(task.sourceSlot, task.hotbarSlot, openedSilently_),
                                       std::memory_order_release);
            lastMoveTime = now;
            break;
        }

        case State::LegitOpening:
            if (mc.isPlayerInventoryOpen()) {
                state = State::LegitMoveSource;
                stateStartTime = now;
                lastMoveTime = now - std::chrono::milliseconds(settings.speed);
            } else if (elapsed > 1000) {
                finishRefill();
            }
            break;

        case State::LegitMoveSource:
            if (!mc.isPlayerInventoryOpen()) {
                finishRefill();
                return;
            }
            if (!hasActiveTask_) {
                const auto wait =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
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
            pendingClickCommand_.store(
                packClickCommand(activeTask_.sourceSlot, activeTask_.hotbarSlot, openedSilently_),
                std::memory_order_release);
            state = State::LegitAwaitClick;
            stateStartTime = now;
            break;

        case State::LegitAwaitClick:
            if (!mc.isPlayerInventoryOpen() || !hasActiveTask_) {
                finishRefill();
                return;
            }
            if (hasClickWork())
                return;
            hasActiveTask_ = false;
            lastMoveTime = now;
            state = State::LegitMoveSource;
            stateStartTime = now;
            break;

        case State::RestoreSprintDown: {
            if (elapsed < 50)
                return;
            const int sprintKey = sprintKeyCode_ > 0 ? sprintKeyCode_ : mc.getSprintKey();
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
            const int sprintKey = sprintKeyCode_ > 0 ? sprintKeyCode_ : mc.getSprintKey();
            mc.setKeyBindState(sprintKey, false);
            sendLwjglKeyState(sprintKey, false);
            sprintKeyHeld_ = false;
            finishRefill();
            break;
        }

        case State::Closing:
            if (elapsed < settings.speed)
                return;
            if (openedSilently_)
                mc.sendInventoryClosePacket();
            else if (!passiveRun_ && activeFlow_ == RunFlow::Legit)
                sendLwjglKey(mc.getInventoryKey());
            if (activeFlow_ == RunFlow::Packet && !passiveRun_) {
                state = State::RestoreSprintDown;
                stateStartTime = now;
                break;
            }
            finishRefill();
            break;
    }
}

void AutoRefill::onOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!env || !packet)
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
    if (!env->IsInstanceOf(packet, c03Class_))
        return;

    std::lock_guard<std::mutex> lock(pendingClickMutex_);
    int command = pendingClickCommand_.load(std::memory_order_acquire);
    if (command < 0 || !pendingClickCommand_.compare_exchange_strong(
                           command, CLICK_COMMAND_CLAIMED, std::memory_order_acq_rel,
                           std::memory_order_acquire))
        return;

    const bool silent = (command & CLICK_COMMAND_SILENT) != 0;
    const int sourceSlot = (command >> 4) & 0xFF;
    const int hotbarSlot = command & 0xF;
    if ((!silent || !mc.isInGui()) && sourceSlot >= 9 && sourceSlot <= 35 &&
        hotbarSlot >= 0 && hotbarSlot <= 8) {
        mc.windowClickSwap(sourceSlot, hotbarSlot);
    }
    pendingClickCommand_.store(CLICK_COMMAND_EMPTY, std::memory_order_release);
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

void AutoRefill::scanForRefills() {
    scanForRefills(mc.getInventorySlots());
}

void AutoRefill::scanForRefills(const std::vector<SlotInfo>& slots) {
    pendingRefills.clear();
    if (slots.empty())
        return;

    std::vector<int> soupSources;
    std::vector<int> potionSources;
    for (const auto& slot : slots) {
        if (slot.slotId < 9 || slot.slotId > 35)
            continue;
        if (settings.refillSoup && slot.itemId == RefillItems::MUSHROOM_SOUP) {
            soupSources.push_back(slot.slotId);
        } else if (settings.refillPotions && slot.itemId == RefillItems::POTION && slot.damage == 16421) {
            potionSources.push_back(slot.slotId);
        }
    }

    if (soupSources.empty() && potionSources.empty())
        return;

    int hotbar[9] = {};
    for (const auto& slot : slots) {
        if (slot.slotId >= 0 && slot.slotId <= 8)
            hotbar[slot.slotId] = slot.itemId;
    }

    size_t soupIndex = 0;
    size_t potionIndex = 0;
    for (int hotbarSlot = 0; hotbarSlot <= 8; ++hotbarSlot) {
        const int existing = hotbar[hotbarSlot];
        int source = -1;
        if (settings.refillSoup && (existing == 0 || existing == RefillItems::BOWL) &&
            soupIndex < soupSources.size()) {
            source = soupSources[soupIndex++];
        } else if (settings.refillPotions && existing == 0 && potionIndex < potionSources.size()) {
            source = potionSources[potionIndex++];
        }

        if (source >= 0)
            pendingRefills.push_back({hotbarSlot, source});
    }
}

void AutoRefill::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::Binder("Keybind", &keybind);
            ImGui::Spacing();
            const bool wasAutomatic = settings.automatic;
            ui::Checkbox("Automatic", &settings.automatic);
            if (settings.automatic && !wasAutomatic) {
                settings.onInventoryOpen = false;
                settings.mode = 0;
                enabled = false;
            }
            ImGui::Spacing();
            static const char* modes[] = {"Packet", "Legit"};
            ui::Combo("Mode", &settings.mode, modes, IM_ARRAYSIZE(modes));
            if (settings.automatic)
                settings.mode = 0;
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
            const bool wasOnInventoryOpen = settings.onInventoryOpen;
            ui::Checkbox("On Inventory Open", &settings.onInventoryOpen);
            if (settings.onInventoryOpen && !wasOnInventoryOpen)
                settings.automatic = false;
        },
        "Filters",
        [this]() {
            ui::Checkbox("Refill Soup", &settings.refillSoup);
            ui::Checkbox("Refill Potions", &settings.refillPotions);
        });
}

std::string AutoRefill::getDisplayInfo() {
    if (settings.automatic)
        return "Automatic";
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
    node["automatic"] = settings.automatic;
}

void AutoRefill::loadSettings(const json::Value& node) {
    settings.refillSoup = node["refillSoup"].as_bool(settings.refillSoup);
    settings.refillPotions = node["refillPotions"].as_bool(settings.refillPotions);
    settings.speed = (std::clamp)(node["speed"].as_int(settings.speed), 0, 200);
    settings.mode = (std::clamp)(node["mode"].as_int(settings.mode), 0, 1);
    settings.legitMoveTime = (std::clamp)(node["legitMoveTime"].as_int(settings.legitMoveTime), 40, 500);
    settings.onInventoryOpen = node["onInventoryOpen"].as_bool(settings.onInventoryOpen);
    settings.automatic = node["automatic"].as_bool(settings.automatic);
    const bool shouldEnable = node["enabled"].as_bool(enabled);
    if (settings.automatic) {
        settings.mode = 0;
        settings.onInventoryOpen = false;
    }
    if (settings.automatic || settings.onInventoryOpen) {
        if (enabled || isRunning())
            onDisable();
        enabled = false;
    } else if (shouldEnable && !enabled) {
        enabled = true;
        onEnable();
    }
}
