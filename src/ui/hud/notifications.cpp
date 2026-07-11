#include "notifications.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"
#include "../widgets/icons.hpp"
#include "../widgets/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace {
float clamp01(float value) {
    return (std::clamp)(value, 0.0f, 1.0f);
}

float easeOutCubic(float value) {
    const float inverse = 1.0f - clamp01(value);
    return 1.0f - inverse * inverse * inverse;
}

float easeInCubic(float value) {
    value = clamp01(value);
    return value * value * value;
}

bool startsWithInsensitive(const std::string& value, const char* prefix) {
    const std::size_t length = std::char_traits<char>::length(prefix);
    if (value.size() < length)
        return false;
    for (std::size_t i = 0; i < length; ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

NotificationType classifySystemMessage(const std::string& message) {
    if (startsWithInsensitive(message, "error") || startsWithInsensitive(message, "cannot") ||
        message.find("failed") != std::string::npos || message.find("missing") != std::string::npos)
        return NotificationType::Error;
    if (startsWithInsensitive(message, "saved") || startsWithInsensitive(message, "loaded") ||
        startsWithInsensitive(message, "deleted") || startsWithInsensitive(message, "added"))
        return NotificationType::Success;
    return NotificationType::Info;
}

// A single leading glyph per type (FontAwesome, merged into font slot 0).
const char* iconFor(NotificationType type) {
    switch (type) {
        case NotificationType::Enabled:
            return ICON_FA_BOLT;
        case NotificationType::Disabled:
            return ICON_FA_POWER_OFF;
        case NotificationType::Success:
            return ICON_FA_CHECK;
        case NotificationType::Error:
            return ICON_FA_TIMES;
        default:
            return ICON_FA_BOLT;
    }
}

ImVec4 colorFor(NotificationType type) {
    switch (type) {
        case NotificationType::Enabled:
        case NotificationType::Success:
            return {0.25f, 0.82f, 0.55f, 1.0f};
        case NotificationType::Disabled:
            return {0.96f, 0.42f, 0.37f, 1.0f};
        case NotificationType::Error:
            return {0.96f, 0.35f, 0.32f, 1.0f};
        default:
            return {g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], 1.0f};
    }
}

}  // namespace

Notification::Notification(std::uint64_t id, std::string text, NotificationType type, float duration)
    : id(id), text(std::move(text)), type(type), startTime(std::chrono::steady_clock::now()), duration(duration) {}

float Notification::elapsed(std::chrono::steady_clock::time_point now) const {
    return std::chrono::duration<float>(now - startTime).count();
}

float Notification::progress(std::chrono::steady_clock::time_point now) const {
    return duration > 0.0f ? clamp01(elapsed(now) / duration) : 1.0f;
}

bool Notification::isExpired(std::chrono::steady_clock::time_point now) const {
    return elapsed(now) >= duration;
}

void NotificationManager::add(const std::string& moduleName, bool isEnabled) {
    if (moduleName.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex);
    notifications.erase(std::remove_if(notifications.begin(), notifications.end(),
                                       [&](const Notification& notification) {
                                           return notification.text == moduleName &&
                                                  (notification.type == NotificationType::Enabled ||
                                                   notification.type == NotificationType::Disabled);
                                       }),
                        notifications.end());
    notifications.emplace_back(nextId++, moduleName,
                               isEnabled ? NotificationType::Enabled : NotificationType::Disabled,
                               NOTIFICATION_DURATION);
    if (notifications.size() > MAX_VISIBLE_NOTIFICATIONS)
        notifications.erase(notifications.begin(), notifications.end() - MAX_VISIBLE_NOTIFICATIONS);
}

void NotificationManager::addSystem(const std::string& message) {
    if (message.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex);
    notifications.emplace_back(nextId++, message, classifySystemMessage(message), NOTIFICATION_DURATION + 0.8f);
    if (notifications.size() > MAX_VISIBLE_NOTIFICATIONS)
        notifications.erase(notifications.begin(), notifications.end() - MAX_VISIBLE_NOTIFICATIONS);
}

void NotificationManager::update() {
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    notifications.erase(std::remove_if(notifications.begin(), notifications.end(),
                                       [&](const Notification& notification) {
                                           return notification.isExpired(now);
                                       }),
                        notifications.end());
}

void NotificationManager::render() {
    if (!enabled)
        return;

    std::lock_guard<std::mutex> lock(mutex);

    if (notifications.empty())
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    ImFont* uiFont = font(0);  // Inter Bold + FontAwesome icons merged into this slot
    if (!uiFont)
        return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float screenWidth = display.x > 1.0f ? display.x : static_cast<float>(RenderData::screenWidth);
    const float screenHeight = display.y > 1.0f ? display.y : static_cast<float>(RenderData::screenHeight);

    // Minimal toast: no card. A type-coloured icon + one line of text, underlined
    // by a thin accent, sliding in from the right edge and stacking upward. A soft
    // shadow keeps it legible over bright scenes without a boxy background.
    const float margin = ui::s(14.0f);
    const float rowHeight = ui::s(23.0f);
    const float rowGap = ui::s(7.0f);
    const float textSize = ui::s(14.0f);
    const float iconSize = ui::s(12.0f);
    const float iconGap = ui::s(8.0f);
    const float slideDist = ui::s(26.0f);
    const float enterDuration = 0.18f;
    const float exitDuration = 0.26f;
    const auto now = std::chrono::steady_clock::now();

    std::size_t visibleIndex = 0;
    for (auto iterator = notifications.rbegin(); iterator != notifications.rend(); ++iterator) {
        const Notification& notification = *iterator;
        const float elapsed = notification.elapsed(now);
        const float remaining = notification.duration - elapsed;
        if (remaining <= 0.0f)
            continue;

        float visibility = easeOutCubic(elapsed / enterDuration);
        if (remaining < exitDuration)
            visibility *= 1.0f - easeInCubic(1.0f - remaining / exitDuration);
        visibility = clamp01(visibility);
        const int alpha = static_cast<int>(255.0f * visibility);
        if (alpha <= 0)
            continue;

        // Single-line label.
        std::string label = notification.text;
        if (notification.type == NotificationType::Enabled)
            label += " enabled";
        else if (notification.type == NotificationType::Disabled)
            label += " disabled";

        const char* icon = iconFor(notification.type);
        const ImVec2 iconMetrics = uiFont->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        const ImVec2 textMetrics = uiFont->CalcTextSizeA(textSize, FLT_MAX, 0.0f, label.c_str());
        const float contentWidth = iconMetrics.x + iconGap + textMetrics.x;

        const float slide = (1.0f - visibility) * slideDist;
        const float rightX = screenWidth - margin + slide;
        const float leftX = rightX - contentWidth;
        const float rowTop =
            screenHeight - margin - rowHeight - static_cast<float>(visibleIndex) * (rowHeight + rowGap);
        const float centerY = rowTop + rowHeight * 0.5f;

        const ImVec4 typeColor = colorFor(notification.type);
        const ImU32 accent = IM_COL32(static_cast<int>(typeColor.x * 255.0f),
                                      static_cast<int>(typeColor.y * 255.0f),
                                      static_cast<int>(typeColor.z * 255.0f), alpha);
        const ImU32 textCol = IM_COL32(238, 240, 244, alpha);
        const ImU32 shadowCol = IM_COL32(0, 0, 0, static_cast<int>(150.0f * visibility));

        const float iconX = leftX;
        const float iconY = centerY - iconMetrics.y * 0.5f;
        const float textX = leftX + iconMetrics.x + iconGap;
        const float textY = centerY - textMetrics.y * 0.5f;

        // Soft shadow first (readability without a card).
        draw->AddText(uiFont, iconSize, ImVec2(iconX + 1.0f, iconY + 1.0f), shadowCol, icon);
        draw->AddText(uiFont, textSize, ImVec2(textX + 1.0f, textY + 1.0f), shadowCol, label.c_str());

        // Icon in the type colour, label in near-white.
        draw->AddText(uiFont, iconSize, ImVec2(iconX, iconY), accent, icon);
        draw->AddText(uiFont, textSize, ImVec2(textX, textY), textCol, label.c_str());

        // Thin accent underline spanning the content.
        const float underlineH = ui::s(1.5f);
        const float underlineY = textY + textMetrics.y + ui::s(2.0f);
        draw->AddRectFilled(ImVec2(leftX, underlineY), ImVec2(rightX, underlineY + underlineH), accent,
                            underlineH * 0.5f);

        ++visibleIndex;
    }
}

bool NotificationManager::hasActive() {
    if (!enabled)
        return false;

    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    for (const auto& notification : notifications) {
        if (!notification.isExpired(now))
            return true;
    }
    return false;
}
