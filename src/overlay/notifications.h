#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <mutex>

struct Notification
{
	std::string text;
	bool enabled;  // true = module enabled, false = module disabled
	std::chrono::steady_clock::time_point startTime;
	float duration;  // Seconds to display
	
	Notification(const std::string& text, bool enabled, float duration = 2.0f)
		: text(text), enabled(enabled), duration(duration)
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
	static void render();
	static void update();  // Remove expired notifications
	
private:
	static std::vector<Notification> notifications;
	static std::mutex mutex;
	
	static constexpr float NOTIFICATION_DURATION = 2.0f;  // Seconds
	static constexpr float NOTIFICATION_WIDTH = 160.0f;
	static constexpr float NOTIFICATION_HEIGHT = 32.0f;
	static constexpr float NOTIFICATION_MARGIN = 10.0f;
	static constexpr float NOTIFICATION_SPACING = 5.0f;
};
