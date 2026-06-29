#include "notifications.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"
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

const char* titleFor(NotificationType type) {
    switch (type) {
        case NotificationType::Enabled:
        case NotificationType::Disabled:
            return "Module updated";
        case NotificationType::Success:
            return "Success";
        case NotificationType::Error:
            return "Action failed";
        default:
            return "Cobble";
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

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float screenWidth = display.x > 1.0f ? display.x : static_cast<float>(RenderData::screenWidth);
    const float screenHeight = display.y > 1.0f ? display.y : static_cast<float>(RenderData::screenHeight);
    const float margin = ui::s(14.0f);
    const float spacing = ui::s(6.0f);
    const float width = ui::s(260.0f);
    const float height = ui::s(50.0f);
    const float rounding = ui::s(5.0f);
    const float padding = ui::s(10.0f);
    const float titleSize = ui::s(12.5f);
    const float messageSize = ui::s(10.5f);
    const float enterDuration = 0.18f;
    const float exitDuration = 0.26f;
    const auto now = std::chrono::steady_clock::now();
    ImFont* notificationFont = font(0);

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

        const float x = screenWidth - margin - width + (1.0f - visibility) * ui::s(28.0f);
        const float y = screenHeight - margin - height -
                        static_cast<float>(visibleIndex) * (height + spacing);
        const ImVec2 rectMin{x, y};
        const ImVec2 rectMax{x + width, y + height};
        const int alpha = static_cast<int>(255.0f * visibility);
        const ImVec4 typeColor = colorFor(notification.type);
        const ImU32 accent = IM_COL32(static_cast<int>(typeColor.x * 255.0f),
                                     static_cast<int>(typeColor.y * 255.0f),
                                     static_cast<int>(typeColor.z * 255.0f), alpha);

        draw->AddRectFilled(rectMin + ImVec2(ui::s(2.0f), ui::s(4.0f)),
                            rectMax + ImVec2(ui::s(2.0f), ui::s(4.0f)),
                            IM_COL32(0, 0, 0, static_cast<int>(90.0f * visibility)), rounding);
        draw->AddRectFilled(rectMin, rectMax, IM_COL32(18, 19, 22, static_cast<int>(244.0f * visibility)),
                            rounding);
        draw->AddRect(rectMin, rectMax, IM_COL32(255, 255, 255, static_cast<int>(20.0f * visibility)),
                      rounding, 0, ui::s(1.0f));

        const float textX = rectMin.x + padding;
        const float titleY = rectMin.y + ui::s(7.0f);
        const float messageY = rectMin.y + ui::s(24.0f);
        const char* title = titleFor(notification.type);
        std::string message = notification.text;
        if (notification.type == NotificationType::Enabled)
            message += " enabled";
        else if (notification.type == NotificationType::Disabled)
            message += " disabled";

        if (notificationFont) {
            draw->AddText(notificationFont, titleSize, {textX, titleY}, IM_COL32(245, 246, 248, alpha), title);
            draw->AddText(notificationFont, messageSize, {textX, messageY}, IM_COL32(166, 170, 178, alpha),
                          message.c_str(), nullptr, rectMax.x - padding - textX);
        } else {
            draw->AddText({textX, titleY}, IM_COL32(245, 246, 248, alpha), title);
            draw->AddText({textX, messageY}, IM_COL32(166, 170, 178, alpha), message.c_str());
        }

        const float barInset = ui::s(1.0f);
        const float barHeight = ui::s(2.0f);
        const float barY = rectMax.y - ui::s(3.0f);
        const float trackWidth = width - barInset * 2.0f;
        const float barWidth = trackWidth * (1.0f - notification.progress(now));
        draw->AddRectFilled({rectMin.x + barInset, barY},
                            {rectMax.x - barInset, barY + barHeight},
                            IM_COL32(255, 255, 255, static_cast<int>(20.0f * visibility)), ui::s(1.0f));
        if (barWidth > 0.0f) {
            draw->AddRectFilled({rectMin.x + barInset, barY},
                                {rectMin.x + barInset + barWidth, barY + barHeight}, accent, ui::s(1.0f));
        }
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
