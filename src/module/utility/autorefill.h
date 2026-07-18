#pragma once

#include <chrono>
#include <vector>
#include <Windows.h>
#include "../../sdk/core/minecraft.h"
#include "../module.h"

// Item IDs for refillable items
namespace RefillItems {
constexpr int MUSHROOM_SOUP = 282;
constexpr int POTION = 373;
}  // namespace RefillItems

// AutoRefill - One-shot action module
// Press keybind -> opens inventory -> refills -> closes
class AutoRefill : public Module {
public:
    AutoRefill();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;

    // Fires any queued refill window-click right before an outgoing flying packet,
    // so the C0E click reaches the server BEFORE the tick's movement packet (the
    // vanilla order). Sending it after the flying packet is what trips Grim's Post
    // check, so we always flush pre-flying instead of clicking from onUpdate.
    void onOutgoingPacket(JNIEnv* env, jobject packet);
    bool isPacketRefilling() const;
    std::string getDisplayInfo() override;
    bool canToggle() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        // Items to refill
        bool refillSoup = true;
        bool refillPotions = true;

        // Speed: delay between item moves (ms)
        int speed = 80;

        // 0 = packet refill, 1 = visible mouse movement and clicks
        int mode = 0;
        int legitMoveTime = 120;

        // On Inventory Open: trigger refill when user opens inventory manually
        bool onInventoryOpen = false;
    } settings;

private:
    enum class State {
        Idle,
        PacketOpening,
        Refilling,  // Moving items
        Closing,    // Waiting before closing
        LegitOpening,
        LegitMoveSource,
        LegitAwaitClick,
        RestoreSprintDown,
        RestoreSprintUp
    };

    State state = State::Idle;
    std::chrono::steady_clock::time_point stateStartTime;
    std::chrono::steady_clock::time_point lastMoveTime;

    struct RefillTask {
        int hotbarSlot;
        int sourceSlot;
    };
    std::vector<RefillTask> pendingRefills;
    RefillTask activeTask_{};
    bool hasActiveTask_ = false;

    POINT mouseStart_{};
    POINT mouseTarget_{};
    bool movementPaused_ = false;
    bool sprintKeyHeld_ = false;

    // Track inventory state for onInventoryOpen mode
    bool wasInventoryOpen = false;

    // Pre-flying flush: onUpdate queues the source slot here instead of clicking
    // inline; onOutgoingPacket fires it the instant a flying packet is sent.
    int pendingClickSlot_ = -1;
    jclass c03Class_ = nullptr;  // C03PacketPlayer (flying) — lazily resolved GlobalRef
    bool c03Resolved_ = false;

    void scanForRefills();
    void finishRefill();
    bool startLegitMove(int slotId);
    bool updateLegitMove(long long elapsed);
};

extern AutoRefill* autoRefill;
