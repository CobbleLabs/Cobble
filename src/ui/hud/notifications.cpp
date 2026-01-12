#include "notifications.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"
#include "../core/overlay.h"
#include "../widgets/widgets.hpp"

#include <algorithm>
#include <cmath>

void NotificationManager::add(const std::string& moduleName, bool isEnabled) {
    std::lock_guard<std::mutex> lock(mutex);

    // Remove any existing notification for the same module
    notifications.erase(std::remove_if(notifications.begin(), notifications.end(),
                                       [&](const Notification& n) { return n.text == moduleName && !n.isSystem; }),
                        notifications.end());

    notifications.emplace_back(moduleName, isEnabled, NOTIFICATION_DURATION, false);
}

void NotificationManager::addSystem(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex);
    notifications.emplace_back(message, true, NOTIFICATION_DURATION * 1.5f, true);
}

void NotificationManager::update() {
    std::lock_guard<std::mutex> lock(mutex);

    notifications.erase(
        std::remove_if(notifications.begin(), notifications.end(), [](const Notification& n) { return n.isExpired(); }),
        notifications.end());
}

void NotificationManager::render() {
    if (!enabled)
        return;

    std::lock_guard<std::mutex> lock(mutex);

    if (notifications.empty())
        return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw)
        return;

    // Use Bold 22px font (index 2) rendered at 20px
    ImFont* notifFont = font(2);

    float screenHeight = static_cast<float>(RenderData::screenHeight);
    float screenWidth = static_cast<float>(RenderData::screenWidth);

    // Use global ui::s() for consistent scaling
    float fontSize = 20.0f;
    float height = ui::s(25.0f);
    float margin = ui::s(9.0f);
    float spacing = ui::s(3.0f);
    float padding = ui::s(13.0f);
    float rounding = ui::s(4.5f);
    float barWidth = ui::s(3.5f);
    float progressHeight = ui::s(2.0f);

    // Accent color from theme
    ImU32 accentColor =
        IM_COL32((int)(g_SchemeColor[0] * 255), (int)(g_SchemeColor[1] * 255), (int)(g_SchemeColor[2] * 255), 255);

    float startY = screenHeight - margin - height;

    int index = 0;
    for (auto& notif : notifications) {
        float progress = notif.getProgress();
        if (progress >= 1.0f)
            continue;

        auto easeOutCubic = [](float t) { return 1.0f - powf(1.0f - t, 3.0f); };
        auto easeInCubic = [](float t) { return t * t * t; };

        float slideProgress;
        float alpha;

        if (progress < 0.15f) {
            slideProgress = easeOutCubic(progress / 0.15f);
            alpha = slideProgress;
        } else if (progress > 0.75f) {
            float outProgress = (progress - 0.75f) / 0.25f;
            slideProgress = 1.0f - easeInCubic(outProgress);
            alpha = slideProgress;
        } else {
            slideProgress = 1.0f;
            alpha = 1.0f;
        }

        std::string displayText;
        const char* statusText = "";

        if (notif.isSystem) {
            displayText = notif.text;
        } else {
            statusText = notif.enabled ? " ON" : " OFF";
            displayText = notif.text + statusText;
        }

        // Calculate text size with actual font
        ImVec2 textSize = notifFont ? notifFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, displayText.c_str())
                                    : ImGui::CalcTextSize(displayText.c_str());

        float notifWidth = textSize.x + padding * 2.0f + barWidth;

        float xOffset = (1.0f - slideProgress) * (notifWidth + margin);
        float x = screenWidth - margin - notifWidth + xOffset;
        float y = startY - index * (height + spacing);

        int alphaInt = static_cast<int>(255 * alpha);
        int bgAlphaInt = static_cast<int>(220 * alpha);

        // Background
        ImU32 bgColor = IM_COL32(16, 16, 16, bgAlphaInt);

        // Status colors
        ImU32 moduleTextColor = IM_COL32(255, 255, 255, alphaInt);
        ImU32 statusColor;
        ImU32 barColor;

        if (notif.isSystem) {
            // Use accent color (orange) for system notifications
            statusColor = IM_COL32((int)(g_SchemeColor[0] * 255), (int)(g_SchemeColor[1] * 255),
                                   (int)(g_SchemeColor[2] * 255), alphaInt);
            barColor = statusColor;
        } else {
            statusColor = notif.enabled ? IM_COL32(100, 255, 100, alphaInt) : IM_COL32(255, 100, 100, alphaInt);
            barColor = statusColor;
        }

        ImVec2 rectMin(x, y);
        ImVec2 rectMax(x + notifWidth, y + height);

        // Background with rounded corners
        draw->AddRectFilled(rectMin, rectMax, bgColor, rounding, ImDrawFlags_RoundCornersLeft);

        // Accent/status bar on right edge
        draw->AddRectFilled(ImVec2(rectMax.x - barWidth, rectMin.y), rectMax, barColor);

        // Text
        float textX = x + padding;
        float textY = y + (height - textSize.y) / 2.0f;

        ImVec2 moduleTextSize = notifFont ? notifFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, notif.text.c_str())
                                          : ImGui::CalcTextSize(notif.text.c_str());

        if (notifFont) {
            draw->AddText(notifFont, fontSize, ImVec2(textX, textY), moduleTextColor, notif.text.c_str());
            if (!notif.isSystem) {
                draw->AddText(notifFont, fontSize, ImVec2(textX + moduleTextSize.x, textY), statusColor, statusText);
            }
        } else {
            draw->AddText(ImVec2(textX, textY), moduleTextColor, notif.text.c_str());
            if (!notif.isSystem) {
                draw->AddText(ImVec2(textX + moduleTextSize.x, textY), statusColor, statusText);
            }
        }

        // Progress bar at bottom (smaller offset = closer to bottom edge)
        float progressBarY = y + height - progressHeight - ui::s(1.5f);
        float progressBarWidth = notifWidth - padding * 2.0f - barWidth;
        float progressX = x + padding;

        ImU32 progressBgColor = IM_COL32(30, 30, 30, static_cast<int>(150 * alpha));
        draw->AddRectFilled(ImVec2(progressX, progressBarY),
                            ImVec2(progressX + progressBarWidth, progressBarY + progressHeight), progressBgColor,
                            ui::s(1.0f));

        float remainingProgress = 1.0f - progress;
        draw->AddRectFilled(ImVec2(progressX, progressBarY),
                            ImVec2(progressX + progressBarWidth * remainingProgress, progressBarY + progressHeight),
                            barColor, ui::s(1.0f));

        index++;
    }
}
