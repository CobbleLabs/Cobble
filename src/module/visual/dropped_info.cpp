#include "dropped_info.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../ui/menu/menu.h"

#include <vector>

DroppedInfo* droppedInfo = nullptr;

DroppedInfo::DroppedInfo() : Module("DroppedInfo") {
    keybind = 0;
    enabled = true;  // Enabled by default
    renders = true;  // Must be true for onRender to be called
}

void DroppedInfo::onEnable() {
    std::lock_guard<std::mutex> lock(mutex);
    potionCount = 0;
}

void DroppedInfo::onUpdate() {
    if (!enabled)
        return;
    if (!Java::Env() || !Java::Initialized)
        return;

    // Scan items
    std::vector<ItemEntityInfo> items = mc.getItemEntityList();

    int total = 0;
    for (const auto& item : items) {
        // Potion ID is 373
        if (item.itemId != 373)
            continue;

        // Check for Splash Health II Potions ONLY
        // 16421: Splash Health II
        // 16453: Splash Health II (Alt)
        int dmg = item.damage;
        if (dmg == 16421 || dmg == 16453) {
            total += item.stackSize;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        potionCount = total;
    }
}

void DroppedInfo::onRender() {
    bool open = Menu::isOpen;
    if (!enabled && !open)
        return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw)
        return;

    int count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        count = potionCount;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Dropped Pots: %d", count);

    // Always render text if enabled, or if menu is open (for positioning)
    if (!enabled && open) {
        snprintf(buf, sizeof(buf), "Dropped Pots: 0");  // Example text
    } else if (!enabled) {
        return;
    }

    float scale = settings.scale;
    ImGui::SetWindowFontScale(scale);

    ImVec2 textSize = ImGui::CalcTextSize(buf);
    float x = settings.x;
    float y = settings.y;
    float padding = 5.0f * scale;

    // Draggable logic: initiation requires menu open, drag continues with menu hidden
    ImVec2 mousePos = ImGui::GetMousePos();

    if (open && !dragging) {
        bool hover = mousePos.x >= x - padding && mousePos.x <= x + textSize.x + padding && mousePos.y >= y - padding &&
                     mousePos.y <= y + textSize.y + padding;

        if (hover) {
            draw->AddRect(ImVec2(x - padding, y - padding), ImVec2(x + textSize.x + padding, y + textSize.y + padding),
                          IM_COL32(255, 255, 255, 255));

            if (ImGui::IsMouseClicked(0)) {
                dragging = true;
                dragOffsetX = mousePos.x - x;
                dragOffsetY = mousePos.y - y;
                Menu::hidden = true;  // Hide menu visually (don't close it)
            }
        }
    }

    if (dragging) {
        if (ImGui::IsMouseDown(0)) {
            settings.x = mousePos.x - dragOffsetX;
            settings.y = mousePos.y - dragOffsetY;
            x = settings.x;
            y = settings.y;
        } else {
            dragging = false;
            Menu::hidden = false;  // Show menu again
        }
    }

    if (settings.background) {
        draw->AddRectFilled(ImVec2(x - padding, y - padding),
                            ImVec2(x + textSize.x + padding, y + textSize.y + padding), IM_COL32(0, 0, 0, 160), 4.0f);
    }

    draw->AddText(ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 255), buf);
    draw->AddText(ImVec2(x, y), IM_COL32(255, 85, 85, 255), buf);  // Red for Health

    ImGui::SetWindowFontScale(1.0f);
}

void DroppedInfo::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();
            ui::SliderFloat("Scale", &settings.scale, 0.5f, 3.0f, "%.1f");
            ui::SliderFloat("X", &settings.x, 0.0f, 1000.0f, "%.0f");
            ui::SliderFloat("Y", &settings.y, 0.0f, 1000.0f, "%.0f");
            ImGui::Spacing();
            if (ImGui::Button("Reset Position")) {
                settings.x = 10.0f;
                settings.y = 10.0f;
                settings.scale = 1.0f;
            }
        },
        "Display",
        [this]() { ui::Checkbox("Background", &settings.background); });
}
