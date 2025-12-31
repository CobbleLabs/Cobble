#pragma once

// ============================================================================
// Notifications - Toast notification system
// ============================================================================

#include <string>
#include <vector>
#include <chrono>
#include <mutex>

struct Notification
{
	std::string text;
	bool enabled;
	bool isSystem = false;
	std::chrono::steady_clock::time_point startTime;
	float duration = 2.0f;
	
	Notification() = default;
	
	Notification(const std::string& text, bool enabled, float duration = 2.0f, bool isSystem = false)
		: text(text), enabled(enabled), duration(duration), isSystem(isSystem)
	{
		startTime = std::chrono::steady_clock::now();
	}
	
	float getProgress() const
	{
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
	
	inline constexpr float NOTIFICATION_DURATION = 2.0f;
	inline constexpr float NOTIFICATION_HEIGHT = 36.0f;
	inline constexpr float NOTIFICATION_MARGIN = 15.0f;
	inline constexpr float NOTIFICATION_SPACING = 6.0f;
	inline constexpr float NOTIFICATION_PADDING = 16.0f;
}
