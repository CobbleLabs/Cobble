#include "notifications.h"
#include "../sdk/render_data.h"
#include "../../ext/imgui/imgui.h"

std::vector<Notification> NotificationManager::notifications;
std::mutex NotificationManager::mutex;

void NotificationManager::add(const std::string& moduleName, bool enabled)
{
	std::lock_guard<std::mutex> lock(mutex);
	
	// Remove any existing notification for the same module
	notifications.erase(
		std::remove_if(notifications.begin(), notifications.end(),
			[&](const Notification& n) { return n.text == moduleName; }),
		notifications.end()
	);
	
	// Add new notification
	notifications.emplace_back(moduleName, enabled, NOTIFICATION_DURATION);
}

void NotificationManager::update()
{
	std::lock_guard<std::mutex> lock(mutex);
	
	// Remove expired notifications
	notifications.erase(
		std::remove_if(notifications.begin(), notifications.end(),
			[](const Notification& n) { return n.isExpired(); }),
		notifications.end()
	);
}

void NotificationManager::render()
{
	std::lock_guard<std::mutex> lock(mutex);
	
	if (notifications.empty()) return;
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	float screenHeight = static_cast<float>(RenderData::screenHeight);
	float startY = screenHeight / 2.0f - (notifications.size() * (NOTIFICATION_HEIGHT + NOTIFICATION_SPACING)) / 2.0f;
	
	int index = 0;
	for (auto& notif : notifications)
	{
		float progress = notif.getProgress();
		if (progress >= 1.0f) continue;
		
		// Animation: slide in from left, slide out to left
		float slideProgress;
		float alpha;
		
		if (progress < 0.1f) {
			// Slide in (first 10% of duration)
			slideProgress = progress / 0.1f;
			alpha = slideProgress;
		} else if (progress > 0.8f) {
			// Slide out (last 20% of duration)
			slideProgress = 1.0f - ((progress - 0.8f) / 0.2f);
			alpha = slideProgress;
		} else {
			slideProgress = 1.0f;
			alpha = 1.0f;
		}
		
		// Calculate positions
		float xOffset = (1.0f - slideProgress) * -NOTIFICATION_WIDTH;
		float x = NOTIFICATION_MARGIN + xOffset;
		float y = startY + index * (NOTIFICATION_HEIGHT + NOTIFICATION_SPACING);
		
		// Background color based on enabled/disabled
		ImU32 bgColor = notif.enabled 
			? IM_COL32(40, 120, 40, static_cast<int>(180 * alpha))   // Green for enabled
			: IM_COL32(120, 40, 40, static_cast<int>(180 * alpha));  // Red for disabled
		
		ImU32 borderColor = notif.enabled
			? IM_COL32(60, 180, 60, static_cast<int>(255 * alpha))
			: IM_COL32(180, 60, 60, static_cast<int>(255 * alpha));
		
		ImU32 textColor = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));
		ImU32 progressBgColor = IM_COL32(0, 0, 0, static_cast<int>(100 * alpha));
		ImU32 progressColor = IM_COL32(255, 255, 255, static_cast<int>(200 * alpha));
		
		// Draw background with rounded corners
		ImVec2 rectMin(x, y);
		ImVec2 rectMax(x + NOTIFICATION_WIDTH, y + NOTIFICATION_HEIGHT);
		draw->AddRectFilled(rectMin, rectMax, bgColor, 6.0f);
		draw->AddRect(rectMin, rectMax, borderColor, 6.0f, 0, 1.5f);
		
		// Draw text
		const char* statusText = notif.enabled ? "ON" : "OFF";
		std::string displayText = notif.text + " " + statusText;
		
		ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
		float textX = x + (NOTIFICATION_WIDTH - textSize.x) / 2.0f;
		float textY = y + (NOTIFICATION_HEIGHT - textSize.y - 4.0f) / 2.0f;
		draw->AddText(ImVec2(textX, textY), textColor, displayText.c_str());
		
		// Draw progress bar at bottom
		float progressBarHeight = 3.0f;
		float progressBarY = y + NOTIFICATION_HEIGHT - progressBarHeight - 2.0f;
		float progressBarWidth = NOTIFICATION_WIDTH - 8.0f;
		float progressX = x + 4.0f;
		
		// Progress bar background
		draw->AddRectFilled(
			ImVec2(progressX, progressBarY),
			ImVec2(progressX + progressBarWidth, progressBarY + progressBarHeight),
			progressBgColor, 2.0f
		);
		
		// Progress bar fill (decreasing as time passes)
		float remainingProgress = 1.0f - progress;
		draw->AddRectFilled(
			ImVec2(progressX, progressBarY),
			ImVec2(progressX + progressBarWidth * remainingProgress, progressBarY + progressBarHeight),
			progressColor, 2.0f
		);
		
		index++;
	}
}
