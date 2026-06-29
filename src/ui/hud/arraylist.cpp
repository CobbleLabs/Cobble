#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "arraylist.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../util/logger.hpp"
#include "../../module/module_manager.h"
#include "../../module/utility/autorefill.h"
#include "../../module/utility/armorswitch.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../sdk/player/render_data.h"
#include "../../util/team/friends_manager.h"
#include "../core/overlay.h"
#include "../menu/menu.h"
#include "../widgets/widgets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

// RGB struct for HSB conversion
struct RGB {
    float r, g, b;
};

// HSB to RGB conversion
static RGB FromHSB(float h, float s, float b) {
    float c = b * s;
    float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = b - c;

    float r, g, bl;
    if (h < 1.0f / 6.0f) {
        r = c;
        g = x;
        bl = 0;
    } else if (h < 2.0f / 6.0f) {
        r = x;
        g = c;
        bl = 0;
    } else if (h < 3.0f / 6.0f) {
        r = 0;
        g = c;
        bl = x;
    } else if (h < 4.0f / 6.0f) {
        r = 0;
        g = x;
        bl = c;
    } else if (h < 5.0f / 6.0f) {
        r = x;
        g = 0;
        bl = c;
    } else {
        r = c;
        g = 0;
        bl = x;
    }

    return RGB{(r + m) * 255.0f, (g + m) * 255.0f, (bl + m) * 255.0f};
}

static ImU32 GetRainbowColor(int moduleIndex, float saturation, long long timeMs) {
    // Use modulo to keep the number small and avoid precision issues
    // 8000ms = 8 second full rainbow cycle
    float cycleMs = 8000.0f;
    float timeInCycle = std::fmod((float)(timeMs % 8000), cycleMs);

    // Each module offset for flowing effect (100ms offset per module)
    float moduleOffset = (moduleIndex * 100.0f);

    // Calculate hue (0-1 range)
    float hue = std::fmod((timeInCycle + moduleOffset) / cycleMs, 1.0f);

    // HSB to RGB
    RGB rgb = FromHSB(hue, saturation, 0.9f);

    return IM_COL32((int)rgb.r, (int)rgb.g, (int)rgb.b, 255);
}

void ArrayListRenderer::render() {
    // If a move/drag is in progress but the list can't draw (disabled, no player,
    // or no active modules), cancel it so the menu doesn't stay hidden forever.
    auto cancelMove = []() {
        if (placing || dragging) {
            placing = false;
            dragging = false;
            Menu::hidden = false;
        }
    };

    if (!enabled) {
        cancelMove();
        return;
    }
    // Render while the menu is open too, so the block can be grabbed and dragged
    // for positioning (same as the DroppedInfo overlay).
    if (!player.isValid()) {
        cancelMove();
        return;  // Hide in main menu
    }

    // Build list of display entries (name + info)
    struct DisplayEntry {
        std::string name;
        std::string info;      // Empty if no info
        std::string infoText;  // Preformatted " info"
        ImVec2 nameSize;
        ImVec2 infoSize;
        float totalWidth;
    };
    std::vector<DisplayEntry> entries;

    // Add enabled modules with their display info
    for (auto* mod : moduleManager->getModules()) {
        if (mod && mod->enabled) {
            DisplayEntry entry;
            entry.name = mod->name;
            if (showInfo) {
                entry.info = mod->getDisplayInfo();
            }
            entries.push_back(entry);
        }
    }

    // Special case: Show AutoRefill when onInventoryOpen is enabled (passive mode)
    if (autoRefill && !autoRefill->enabled && autoRefill->settings.onInventoryOpen) {
        DisplayEntry entry;
        entry.name = autoRefill->name;
        if (showInfo) {
            entry.info = autoRefill->getDisplayInfo();
        }
        entries.push_back(entry);
    }

    // Special case: Show ArmorSwitch when onInventoryOpen is enabled (passive mode)
    if (armorSwitch && !armorSwitch->enabled && armorSwitch->settings.onInventoryOpen) {
        DisplayEntry entry;
        entry.name = armorSwitch->name;
        if (showInfo) {
            entry.info = armorSwitch->getDisplayInfo();
        }
        entries.push_back(entry);
    }

    if (friendsManager && friendsManager->teams.enabled) {
        DisplayEntry e;
        e.name = "Teams";
        e.info = "";
        entries.push_back(e);
    }

    if (entries.empty()) {
        cancelMove();
        return;
    }

    // Use Bold 22px font
    ImFont* arrayFont = font(2);
    if (!arrayFont)
        return;

    // Base font size with scale applied (24px base)
    float fontSize = 24.0f * scale;

    for (auto& entry : entries) {
        entry.nameSize = arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, entry.name.c_str());
        entry.infoText = entry.info.empty() ? "" : " " + entry.info;
        entry.infoSize = entry.info.empty() ? ImVec2(0, 0)
                                            : arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, entry.infoText.c_str());
        entry.totalWidth = entry.nameSize.x + entry.infoSize.x;
    }

    // Sort by total text width (sizes already calculated)
    if (sortByLength) {
        std::sort(entries.begin(), entries.end(),
                  [](const DisplayEntry& a, const DisplayEntry& b) { return a.totalWidth > b.totalWidth; });
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw)
        return;

    float screenWidth = static_cast<float>(RenderData::screenWidth);
    float screenHeight = static_cast<float>(RenderData::screenHeight);

    // Styling values scaled
    float padding = 10.0f * scale;
    float barWidth = 3.0f * scale;

    // `anchor` now only decides row alignment (left/right) and the default
    // corner the block starts at before the user drags it elsewhere.
    bool anchorLeft = (anchor == 1 || anchor == 3);    // TopLeft or BottomLeft
    bool anchorBottom = (anchor == 2 || anchor == 3);  // BottomRight or BottomLeft

    // Whole-block dimensions (width = widest row, height = sum of rows).
    float edgePadding = colorBar ? barWidth + 5.0f * scale : 3.0f * scale;
    float blockWidth = 0.0f;
    float blockHeight = 0.0f;
    for (const auto& entry : entries) {
        float itemWidth = entry.totalWidth + padding + edgePadding;
        if (itemWidth > blockWidth)
            blockWidth = itemWidth;
        blockHeight += entry.nameSize.y;
    }

    // First time (or after a reset) place the block at the chosen corner.
    if (posX < 0.0f || posY < 0.0f) {
        posX = anchorLeft ? 0.0f : (screenWidth - blockWidth);
        posY = anchorBottom ? (screenHeight - yOffset - blockHeight) : yOffset;
    }

    // Move-to-position. Two ways, both use Menu::hidden so the menu stays "open"
    // (cursor works) while only its visuals hide:
    //   * placing  -> started from the menu button; block sticks to the cursor
    //                 and a single click drops it.
    //   * dragging -> grab the block directly (hover + hold) and drop it.
    ImVec2 mousePos = ImGui::GetMousePos();

    if (placing) {
        posX = mousePos.x - blockWidth * 0.5f;  // center the block on the cursor
        posY = mousePos.y - blockHeight * 0.5f;
        draw->AddRect(ImVec2(posX - 2.0f, posY - 2.0f), ImVec2(posX + blockWidth + 2.0f, posY + blockHeight + 2.0f),
                      IM_COL32(120, 200, 255, 255), 4.0f * scale, 0, 1.5f);
        if (ImGui::IsMouseClicked(0)) {
            placing = false;
            Menu::hidden = false;
            log_ok("ArrayList placed at (%.0f, %.0f)", posX, posY);
            // NOTE: don't save config here -- this runs on the render thread.
            // The position is persisted when the menu closes (main thread).
        }
    } else {
        bool hover = mousePos.x >= posX && mousePos.x <= posX + blockWidth && mousePos.y >= posY &&
                     mousePos.y <= posY + blockHeight;

        if (Menu::isOpen && !dragging && hover) {
            draw->AddRect(ImVec2(posX - 2.0f, posY - 2.0f), ImVec2(posX + blockWidth + 2.0f, posY + blockHeight + 2.0f),
                          IM_COL32(120, 200, 255, 255), 4.0f * scale, 0, 1.5f);
            if (ImGui::IsMouseClicked(0)) {
                dragging = true;
                dragOffsetX = mousePos.x - posX;
                dragOffsetY = mousePos.y - posY;
                Menu::hidden = true;  // hide the menu visuals, but keep it "open" (cursor stays free)
                log_ok("ArrayList drag start");
            }
        }

        if (dragging) {
            if (ImGui::IsMouseDown(0)) {
                posX = mousePos.x - dragOffsetX;
                posY = mousePos.y - dragOffsetY;
            } else {
                dragging = false;
                Menu::hidden = false;  // show the menu again
                log_ok("ArrayList dropped at (%.0f, %.0f)", posX, posY);
                // NOTE: saved on menu close (main thread), not here (render thread).
            }
        }
    }

    // Keep the block fully on-screen (parenthesized to dodge windows.h min/max macros).
    posX = (std::clamp)(posX, 0.0f, (std::max)(0.0f, screenWidth - blockWidth));
    posY = (std::clamp)(posY, 0.0f, (std::max)(0.0f, screenHeight - blockHeight));

    float blockX = posX;
    float y = posY;
    int idx = 0;

    // Colors
    ImU32 bgColor = showBackground ? IM_COL32(19, 19, 19, 150) : IM_COL32(0, 0, 0, 0);
    ImU32 infoColor = IM_COL32(180, 180, 180, 255);  // Light gray for info

    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    for (const auto& entry : entries) {
        float totalWidth = entry.totalWidth;
        float lineHeight = entry.nameSize.y;

        // Get color (rainbow or accent)
        ImU32 moduleColor;
        if (rainbow) {
            moduleColor = GetRainbowColor(idx, rainbowSaturation, rainbowTimeMs);
        } else {
            // Use custom color if set, otherwise use accent color
            if (color[0] < 0) {
                // Use accent color from theme
                moduleColor = IM_COL32((int)(g_SchemeColor[0] * 255), (int)(g_SchemeColor[1] * 255),
                                       (int)(g_SchemeColor[2] * 255), 255);
            } else {
                moduleColor = IM_COL32((int)(color[0] * 255), (int)(color[1] * 255), (int)(color[2] * 255), 255);
            }
        }

        // Calculate X position: rows align to one side of the (movable) block.
        float itemWidth = totalWidth + padding + edgePadding;
        float rectX1, rectX2, textX, barX1, barX2;

        if (anchorLeft) {
            // Left-aligned within the block
            rectX1 = blockX;
            rectX2 = blockX + itemWidth;
            textX = blockX + (colorBar ? barWidth + padding * 0.5f : padding * 0.5f);
            barX1 = blockX;
            barX2 = blockX + barWidth;
        } else {
            // Right-aligned within the block (default)
            float rightEdge = blockX + blockWidth;
            rectX1 = rightEdge - itemWidth;
            rectX2 = rightEdge;
            textX = rectX1 + padding * 0.5f;
            barX1 = rightEdge - barWidth;
            barX2 = rightEdge;
        }

        // Background with rounded corners
        draw->AddRectFilled(ImVec2(rectX1, y), ImVec2(rectX2, y + lineHeight), bgColor,
                            4.0f * scale  // Rounded corners
        );

        // Text shadow for readability when background is off
        if (!showBackground) {
            ImU32 shadowColor = IM_COL32(0, 0, 0, 200);
            float sd = 1.0f * scale;
            draw->AddText(arrayFont, fontSize, ImVec2(textX + sd, y + sd), shadowColor, entry.name.c_str());
            if (!entry.info.empty()) {
                draw->AddText(arrayFont, fontSize, ImVec2(textX + entry.nameSize.x + sd, y + sd), shadowColor,
                              entry.infoText.c_str());
            }
        }

        // Draw name with module color
        draw->AddText(arrayFont, fontSize, ImVec2(textX, y), moduleColor, entry.name.c_str());

        // Draw info with gray color
        if (!entry.info.empty()) {
            draw->AddText(arrayFont, fontSize, ImVec2(textX + entry.nameSize.x, y), infoColor, entry.infoText.c_str());
        }

        // Color bar on edge
        if (colorBar) {
            draw->AddRectFilled(ImVec2(barX1, y), ImVec2(barX2, y + lineHeight), moduleColor);
        }

        y += lineHeight;
        idx++;
    }
}

bool ArrayListRenderer::hasActive() {
    if (!enabled)
        return false;
    if (Menu::isOpen)
        return false;
    if (!player.isValid())
        return false;
    return true;
}
