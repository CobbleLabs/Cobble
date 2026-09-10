#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <Windows.h>
#include "../../sdk/core/minecraft.h"
#include "../module.h"

namespace RefillItems {
constexpr int MUSHROOM_SOUP = 282;
constexpr int BOWL = 281;
constexpr int POTION = 373;
}  // namespace RefillItems

class AutoRefill : public Module {
public:
    AutoRefill();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRenderMenu() override;

    void onOutgoingPacket(JNIEnv* env, jobject packet);
    bool hasPendingClick() const;
    bool isRunning() const;
    bool isPacketRefilling() const;
    std::string getDisplayInfo() override;
    bool canToggle() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        bool refillSoup = true;
        bool refillPotions = true;
        int speed = 80;
        int mode = 0;
        int legitMoveTime = 120;
        bool onInventoryOpen = false;
        bool automatic = false;
    } settings;

private:
    enum class State {
        Idle,
        PacketOpening,
        Refilling,
        Closing,
        LegitOpening,
        LegitMoveSource,
        LegitAwaitClick,
        RestoreSprintDown,
        RestoreSprintUp
    };

    enum class RunFlow {
        Packet,
        Legit
    };

    State state = State::Idle;
    RunFlow activeFlow_ = RunFlow::Packet;
    bool openedSilently_ = false;
    bool passiveRun_ = false;
    std::chrono::steady_clock::time_point stateStartTime;
    std::chrono::steady_clock::time_point lastMoveTime;
    std::chrono::steady_clock::time_point lastAutomaticScan_;

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
    bool sprintTemporarilyEnabled_ = false;
    int sprintKeyCode_ = 0;
    bool wasInventoryOpen = false;

    std::atomic<int> pendingClickCommand_{-1};
    std::mutex pendingClickMutex_;
    jclass c03Class_ = nullptr;
    bool c03Resolved_ = false;

    void scanForRefills();
    void scanForRefills(const std::vector<SlotInfo>& slots);
    void startPacketFlow();
    bool hasClickWork() const;
    void clearQueuedClick();
    void restoreInputState();
    void finishRefill();
    bool startLegitMove(int slotId);
    bool updateLegitMove(long long elapsed);
};

extern AutoRefill* autoRefill;
