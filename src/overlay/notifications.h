#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <mutex>

struct Notification
{
	std::string text;
	bool enabled;  // true = module enabled, false = module disabled
	bool isSystem; // true = system message (no ON/OFF suffix)
	std::chrono::steady_clock::time_point startTime;
	float duration;  // Seconds to display
	
	Notification(const std::string& text, bool enabled, float duration = 2.0f, bool isSystem = false)
		: text(text), enabled(enabled), duration(duration), isSystem(isSystem)
	{
		startTime = std::chrono::steady_clock::now();
	}
	
	// Returns 0-1 progress (0 = just started, 1 = expired)
	float getProgress() const
	{
		auto now = std::chrono::steady_clock::now();
		float elapsed = std::chrono::duration<float>(now - startTime).count();
		return elapsed / duration;
	}
	
	bool isExpired() const
	{
		return getProgress() >= 1.0f;
	}
};

class NotificationManager
{
public:
	static void add(const std::string& moduleName, bool enabled);
	static void addSystem(const std::string& message);  // System notifications (green text, no ON/OFF)
	static void render();
	static void update();  // Remove expired notifications
	
private:
	static std::vector<Notification> notifications;
	static std::mutex mutex;
	
	static constexpr float NOTIFICATION_DURATION = 2.0f;
	static constexpr float NOTIFICATION_HEIGHT = 36.0f;
	static constexpr float NOTIFICATION_MARGIN = 15.0f;
	static constexpr float NOTIFICATION_SPACING = 6.0f;
	static constexpr float NOTIFICATION_PADDING = 16.0f;
};
