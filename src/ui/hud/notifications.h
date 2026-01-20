#pragma once

// ============================================================================
// Notifications - Toast notification system
// ============================================================================

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

struct Notification {
    std::string text;
    bool enabled;
    bool isSystem = false;
    std::chrono::steady_clock::time_point startTime;
    float duration = 2.0f;

    Notification() = default;

    Notification(const std::string& text, bool enabled, float duration = 2.0f, bool isSystem = false)
        : text(text), enabled(enabled), duration(duration), isSystem(isSystem) {
        startTime = std::chrono::steady_clock::now();
    }

    float getProgress() const {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - startTime).count();
        return elapsed / duration;
    }

    bool isExpired() const { return getProgress() >= 1.0f; }
};

namespace NotificationManager {
inline bool enabled = true;
inline std::vector<Notification> notifications;
inline std::mutex mutex;

void add(const std::string& moduleName, bool enabled);
void addSystem(const std::string& message);
void update();
void render();
bool hasActive();

// Timing
inline constexpr float NOTIFICATION_DURATION = 2.0f;

// Base values (will be scaled by resolution)
inline constexpr float BASE_HEIGHT = 28.0f;
inline constexpr float BASE_MARGIN = 10.0f;
inline constexpr float BASE_SPACING = 4.0f;
inline constexpr float BASE_PADDING = 12.0f;
}  // namespace NotificationManager
