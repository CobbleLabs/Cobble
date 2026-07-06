#pragma once
//
// Cobble Spectate — client manager (Phase 2A).
//
// Owns the client side of the spectate relay: it advertises presence + privacy,
// captures the host's live state into a SpectateSnapshot and uploads it while
// being watched, and (as a spectator) pulls + decodes a target's frames into a
// latest-snapshot other code can render. All network calls run on detached
// worker threads (WinHTTP blocks); the JNI-touching capture runs synchronously
// inside tick() on the main client thread.
//
// UI (privacy toggles, browser, spectate overlay, "Spectators: #" HUD) and the
// full 3D render live elsewhere; this manager is the engine they drive.

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "spectate_snapshot.hpp"

class SpectateManager {
public:
    static SpectateManager& get();

    // ─── Privacy (set from the settings UI; defaults off) ──────────────────
    void setAllowSpectate(bool v) { allowSpectate_ = v; }
    void setAllowConfigView(bool v) { allowConfigView_ = v; }
    bool allowSpectate() const { return allowSpectate_; }
    bool allowConfigView() const { return allowConfigView_; }

    // ─── Browse list (spectatable players) ─────────────────────────────────
    struct BrowseEntry {
        std::string username;
        std::string activity;
        int spectatorCount = 0;
        bool isSpectator = false;  // this player is themselves spectating someone
    };
    struct BrowseState {
        enum Status { Idle, Loading, Ready, Error };
        Status status = Idle;
        std::vector<BrowseEntry> entries;
        std::string message;
    };
    BrowseState getBrowseState() const {
        std::lock_guard<std::mutex> lk(browseMutex_);
        return browseState_;
    }
    void refreshListAsync();

    // ─── Spectating (spectator side) ───────────────────────────────────────
    void startSpectatingAsync(const std::string& target);
    void stopSpectating();
    bool isSpectating() const { return spectating_; }
    std::string currentTarget() const {
        std::lock_guard<std::mutex> lk(targetMutex_);
        return target_;
    }
    bool targetAllowsConfigView() const { return targetAllowsConfigView_; }

    // Latest decoded frame from the current target. Returns false if none yet.
    bool getLatestSnapshot(spectate::SpectateSnapshot& out) const {
        std::lock_guard<std::mutex> lk(snapMutex_);
        if (!haveSnapshot_)
            return false;
        out = latestSnapshot_;
        return true;
    }

    // ─── Host side ─────────────────────────────────────────────────────────
    int spectatorCount() const { return spectatorCount_; }

    // Total decoded frames received while spectating (liveness indicator).
    uint32_t framesReceived() const { return framesReceived_; }

    // Called once per client tick (from ModuleManager). Handles presence
    // heartbeats, host frame uploads (only while watched), and spectator frame
    // fetches — all rate-limited internally.
    void tick();

private:
    SpectateManager() = default;
    SpectateManager(const SpectateManager&) = delete;
    SpectateManager& operator=(const SpectateManager&) = delete;

    // Build a snapshot from live SDK state (must run on the JNI/client thread).
    spectate::SpectateSnapshot captureSnapshot();

    // Detached network workers.
    void heartbeatPresenceAsync(const std::string& activity);
    void uploadFrameAsync(std::vector<uint8_t> blob);
    void fetchFrameAsync(const std::string& target);

    std::atomic<bool> allowSpectate_{false};
    std::atomic<bool> allowConfigView_{false};
    std::atomic<int> spectatorCount_{0};
    std::atomic<uint32_t> framesReceived_{0};
    std::atomic<bool> spectating_{false};
    std::atomic<bool> targetAllowsConfigView_{false};

    std::string target_;
    mutable std::mutex targetMutex_;

    BrowseState browseState_;
    mutable std::mutex browseMutex_;

    spectate::SpectateSnapshot latestSnapshot_;
    bool haveSnapshot_ = false;
    mutable std::mutex snapMutex_;

    using clock = std::chrono::steady_clock;
    clock::time_point lastPresence_{};
    clock::time_point lastUpload_{};
    clock::time_point lastFetch_{};

    // Throttled world capture (host FPS protection): blocks captured ~1 Hz and
    // cached; every frame reuses the cache. Origin is the host floor at capture.
    std::vector<spectate::SpectateBlock> cachedBlocks_;
    int32_t blockOriginX_ = 0, blockOriginY_ = 0, blockOriginZ_ = 0;
    clock::time_point lastBlockCapture_{};
    // Guards against overlapping in-flight uploads/fetches (one at a time).
    std::atomic<bool> uploadInFlight_{false};
    std::atomic<bool> fetchInFlight_{false};
};
