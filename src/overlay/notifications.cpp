#include "notifications.h"
#include "overlay.h"
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
			[&](const Notification& n) { return n.text == moduleName && !n.isSystem; }),
		notifications.end()
	);
	
	// Add new notification
	notifications.emplace_back(moduleName, enabled, NOTIFICATION_DURATION, false);
}

void NotificationManager::addSystem(const std::string& message)
{
	std::lock_guard<std::mutex> lock(mutex);
	
	// Add system notification (no duplicate check - can show multiple)
	notifications.emplace_back(message, true, NOTIFICATION_DURATION * 1.5f, true);
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
	if (!enabled) return;
	
	std::lock_guard<std::mutex> lock(mutex);
	
	if (notifications.empty()) return;
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	// Use Inter font for notifications
	if (Overlay::notificationFont) {
		ImGui::PushFont(Overlay::notificationFont);
	}
	
	// Get current screen dimensions (responsive to resize/F11)
	float screenWidth = static_cast<float>(RenderData::screenWidth);
	float screenHeight = static_cast<float>(RenderData::screenHeight);
	
	// Calculate starting position (bottom-right corner)
	// Stack notifications from bottom up
	float startY = screenHeight - NOTIFICATION_MARGIN - NOTIFICATION_HEIGHT;
	
	int index = 0;
	for (auto& notif : notifications)
	{
		float progress = notif.getProgress();
		if (progress >= 1.0f) continue;
		
		// Smooth easing function for animation
		auto easeOutCubic = [](float t) { return 1.0f - powf(1.0f - t, 3.0f); };
		auto easeInCubic = [](float t) { return t * t * t; };
		
		// Animation: slide in from right, slide out to right
		float slideProgress;
		float alpha;
		
		if (progress < 0.15f) {
			// Slide in (first 15% of duration)
			slideProgress = easeOutCubic(progress / 0.15f);
			alpha = slideProgress;
		} else if (progress > 0.75f) {
			// Slide out (last 25% of duration)
			float outProgress = (progress - 0.75f) / 0.25f;
			slideProgress = 1.0f - easeInCubic(outProgress);
			alpha = slideProgress;
		} else {
			slideProgress = 1.0f;
			alpha = 1.0f;
		}
		
		// Calculate notification width based on text
		std::string displayText;
		const char* statusText = "";
		
		if (notif.isSystem) {
			displayText = notif.text;
		} else {
			statusText = notif.enabled ? " ON" : " OFF";
			displayText = notif.text + statusText;
		}
		
		ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
		float notifWidth = textSize.x + NOTIFICATION_PADDING * 2.0f;
		
		// Calculate positions (right side of screen)
		float xOffset = (1.0f - slideProgress) * (notifWidth + NOTIFICATION_MARGIN);
		float x = screenWidth - NOTIFICATION_MARGIN - notifWidth + xOffset;
		float y = startY - index * (NOTIFICATION_HEIGHT + NOTIFICATION_SPACING);
		
		// Clamp alpha
		int alphaInt = static_cast<int>(255 * alpha);
		int bgAlphaInt = static_cast<int>(200 * alpha);
		
		// Black background always
		ImU32 bgColor = IM_COL32(15, 15, 15, bgAlphaInt);
		ImU32 borderColor = notif.isSystem 
			? IM_COL32(100, 80, 180, alphaInt)  // Purple border for system
			: IM_COL32(40, 40, 40, alphaInt);
		
		// Text color based on type
		ImU32 moduleTextColor;
		ImU32 statusColor;
		
		if (notif.isSystem) {
			// System notifications: magenta/purple text
			moduleTextColor = IM_COL32(200, 150, 255, alphaInt);
			statusColor = moduleTextColor;
		} else {
			// Module notifications: white name + green/red status
			moduleTextColor = IM_COL32(255, 255, 255, alphaInt);
			statusColor = notif.enabled 
				? IM_COL32(100, 255, 100, alphaInt)   // Green for ON
				: IM_COL32(255, 100, 100, alphaInt);  // Red for OFF
		}
		
		// Draw background with rounded corners
		ImVec2 rectMin(x, y);
		ImVec2 rectMax(x + notifWidth, y + NOTIFICATION_HEIGHT);
		draw->AddRectFilled(rectMin, rectMax, bgColor, 4.0f);
		draw->AddRect(rectMin, rectMax, borderColor, 4.0f, 0, 1.0f);
		
		// Draw text
		float textX = x + NOTIFICATION_PADDING;
		float textY = y + (NOTIFICATION_HEIGHT - textSize.y) / 2.0f;
		
		ImVec2 moduleTextSize = ImGui::CalcTextSize(notif.text.c_str());
		draw->AddText(ImVec2(textX, textY), moduleTextColor, notif.text.c_str());
		
		// Draw status text (only for module notifications)
		if (!notif.isSystem) {
			float statusX = textX + moduleTextSize.x;
			draw->AddText(ImVec2(statusX, textY), statusColor, statusText);
		}
		
		// Draw thin progress bar at bottom
		float progressBarHeight = 2.0f;
		float progressBarY = y + NOTIFICATION_HEIGHT - progressBarHeight - 3.0f;
		float progressBarWidth = notifWidth - NOTIFICATION_PADDING * 2.0f;
		float progressX = x + NOTIFICATION_PADDING;
		
		// Progress bar background
		ImU32 progressBgColor = IM_COL32(30, 30, 30, static_cast<int>(150 * alpha));
		draw->AddRectFilled(
			ImVec2(progressX, progressBarY),
			ImVec2(progressX + progressBarWidth, progressBarY + progressBarHeight),
			progressBgColor, 1.0f
		);
		
		// Progress bar fill (decreasing as time passes)
		float remainingProgress = 1.0f - progress;
		ImU32 progressColor;
		if (notif.isSystem) {
			progressColor = IM_COL32(180, 130, 255, static_cast<int>(200 * alpha));
		} else {
			progressColor = notif.enabled
				? IM_COL32(100, 255, 100, static_cast<int>(200 * alpha))
				: IM_COL32(255, 100, 100, static_cast<int>(200 * alpha));
		}
		draw->AddRectFilled(
			ImVec2(progressX, progressBarY),
			ImVec2(progressX + progressBarWidth * remainingProgress, progressBarY + progressBarHeight),
			progressColor, 1.0f
		);
		
		index++;
	}
	
	// Restore font
	if (Overlay::notificationFont) {
		ImGui::PopFont();
	}
}
