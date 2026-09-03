#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>
#include <Windows.h>
#include "../../sdk/core/minecraft.h"
#include "../module.h"

// Item IDs for refillable items
namespace RefillItems {
constexpr int MUSHROOM_SOUP = 282;
constexpr int BOWL = 281;  // Leftover after eating soup — hotbar slot is not "empty"
constexpr int POTION = 373;
}  // namespace RefillItems

// Named timing bundles. The slowest one is "Careful" rather than "Legit" so it
// never reads as the Legit *mode* below — that is a different axis entirely.
enum class AutoRefillPreset { Instant = 0, Fast = 1, Human = 2, Careful = 3, Custom = 4 };

// AutoRefill - One-shot action module
// Press keybind -> reaction pause -> opens inventory -> refills -> closes
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

        // Base delay between item moves (ms). Every use of it is jittered.
        int speed = 80;

        // 0 = packet refill, 1 = visible mouse movement and clicks
        int mode = 0;
        int legitMoveTime = 130;

        // On Inventory Open: trigger refill when user opens inventory manually
        bool onInventoryOpen = false;

        // --- Humanization ---
        int preset = static_cast<int>(AutoRefillPreset::Human);
        int reactionMs = 190;      // pause between the trigger and the first action
        int jitterPercent = 35;    // spread applied to every delay
        bool fatigue = true;       // gaps grow slightly over a long refill
        int hesitateChance = 12;   // chance of an extra pause / cursor detour per move
        bool humanPath = true;     // curved, eased, overshooting cursor path
        bool nearestFirst = true;  // grab the closest stack next, like a real hand
    } settings;

    void applyPreset(AutoRefillPreset preset);

private:
    enum class State {
        Idle,
        Reacting,  // Human delay between the trigger and the first action
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
    int64_t stateDelay_ = 0;  // jittered wait for the current state
    int64_t nextGapMs_ = 0;   // jittered wait before the next item move

    struct RefillTask {
        int hotbarSlot;
        int sourceSlot;
    };
    std::vector<RefillTask> pendingRefills;
    RefillTask activeTask_{};
    bool hasActiveTask_ = false;
    bool manualTrigger_ = false;  // false = passive (the player opened the inventory)
    int movesDone_ = 0;
    int lastSourceSlot_ = -1;

    POINT mouseStart_{};
    POINT mouseTarget_{};
    // Shape of one ballistic cursor move, redrawn for every move.
    int64_t pathDuration_ = 130;
    int64_t pathSettle_ = 0;       // hand resting on the slot before the click
    float bowFrac_ = 0.0f;         // sideways arc, as a fraction of the distance
    float overshootFrac_ = 0.0f;   // sails past the slot, then corrects back
    float tremorAmp_ = 0.0f;
    float tremorPhase_ = 0.0f;
    float tremorFreq_ = 8.0f;
    int decoyStage_ = 0;  // 1 = currently drifting to a decoy slot, not the real one

    bool movementPaused_ = false;
    bool sprintKeyHeld_ = false;

    // Track inventory state for onInventoryOpen mode
    bool wasInventoryOpen = false;

    // Pre-flying flush: onUpdate queues source+target here instead of clicking
    // inline; onOutgoingPacket fires it the instant a flying packet is sent.
    // We swap (mode 2) rather than shift-click so a hotbar slot holding a bowl
    // gets the soup directly — shift-click has no empty slot to land in.
    // Written from the module thread, read from the Netty thread: the hotbar is
    // published first so a slot that reads as pending always has its target set.
    std::atomic<int> pendingClickSlot_{-1};
    std::atomic<int> pendingClickHotbar_{-1};
    jclass c03Class_ = nullptr;  // C03PacketPlayer (flying) — lazily resolved GlobalRef
    bool c03Resolved_ = false;

    mutable std::mt19937_64 rng_;

    int64_t jitter(int baseMs) const;
    int64_t jitter(int baseMs, int spreadPercent) const;
    bool roll(int percent) const;
    float fatigueScale() const;
    int64_t nextGap() const;
    void enterState(State next, int64_t delayMs);
    bool takeNextTask(RefillTask& out);

    void scanForRefills();
    void finishRefill();
    bool startLegitMove(int slotId);
    bool updateLegitMove(long long elapsed);
};

extern AutoRefill* autoRefill;
