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
#include "hud_color.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Per-row appear animation state (module name -> progress 0..1), persisted
// across frames. render() runs single-threaded on the GL thread, so a plain
// static map is safe here.
static std::unordered_map<std::string, float> g_rowAnim;

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
    ImU32 infoColor = IM_COL32(180, 180, 180, 255);  // Light gray for info

    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    // Row animation timing + a gentle global pulse shared by every accent bar.
    float dtA = ImGui::GetIO().DeltaTime;
    if (dtA <= 0.0f || dtA > 0.25f) dtA = 1.0f / 60.0f;  // guard hitches / first frame
    double time = ImGui::GetTime();
    float pulse = 0.85f + 0.15f * (float)std::sin(time * 2.0);

    // Forget animation state for rows no longer shown, so a module re-animates
    // the next time it turns on instead of snapping in fully lit.
    {
        std::unordered_set<std::string> seen;
        for (const auto& e : entries) seen.insert(e.name);
        for (auto it = g_rowAnim.begin(); it != g_rowAnim.end();) {
            if (seen.find(it->first) == seen.end())
                it = g_rowAnim.erase(it);
            else
                ++it;
        }
    }

    for (const auto& entry : entries) {
        float totalWidth = entry.totalWidth;
        float lineHeight = entry.nameSize.y;

        // Get color (rainbow or accent)
        ImU32 moduleColor;
        if (rainbow) {
            moduleColor = hud::GetRainbowColor(idx, rainbowSaturation, rainbowTimeMs);
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

        // Per-row appear animation: slide in from the anchored edge + fade in.
        float& prog = g_rowAnim[entry.name];
        prog = (std::min)(1.0f, prog + dtA * 6.0f);  // ~0.17s to settle
        float eased = 1.0f - (1.0f - prog) * (1.0f - prog) * (1.0f - prog);  // easeOutCubic
        float slide = (1.0f - eased) * 14.0f * scale;
        if (anchorLeft) slide = -slide;  // come in from whichever edge the block hugs
        rectX1 += slide;
        rectX2 += slide;
        textX += slide;
        barX1 += slide;
        barX2 += slide;

        float rounding = 4.0f * scale;

        // Background: rounded dark base + a soft module-colored wash hugging the
        // bar side, so each row is subtly tinted by its own accent color.
        if (showBackground) {
            draw->AddRectFilled(ImVec2(rectX1, y), ImVec2(rectX2, y + lineHeight),
                                hud::ColScaleAlpha(IM_COL32(19, 19, 19, 150), eased), rounding);
            ImU32 washOn = hud::ColScaleAlpha(moduleColor, 0.16f * eased);
            ImU32 washOff = hud::ColScaleAlpha(moduleColor, 0.0f);
            ImVec2 wmin(rectX1 + rounding, y), wmax(rectX2 - rounding, y + lineHeight);
            if (wmax.x > wmin.x) {
                if (anchorLeft)
                    draw->AddRectFilledMultiColor(wmin, wmax, washOn, washOff, washOff, washOn);
                else
                    draw->AddRectFilledMultiColor(wmin, wmax, washOff, washOn, washOn, washOff);
            }
        }

        // Text shadow for readability when background is off
        if (!showBackground) {
            ImU32 shadowColor = hud::ColScaleAlpha(IM_COL32(0, 0, 0, 200), eased);
            float sd = 1.0f * scale;
            draw->AddText(arrayFont, fontSize, ImVec2(textX + sd, y + sd), shadowColor, entry.name.c_str());
            if (!entry.info.empty()) {
                draw->AddText(arrayFont, fontSize, ImVec2(textX + entry.nameSize.x + sd, y + sd), shadowColor,
                              entry.infoText.c_str());
            }
        }

        // Draw name with module color (fades in with the row)
        draw->AddText(arrayFont, fontSize, ImVec2(textX, y), hud::ColScaleAlpha(moduleColor, eased), entry.name.c_str());

        // Draw info with gray color
        if (!entry.info.empty()) {
            draw->AddText(arrayFont, fontSize, ImVec2(textX + entry.nameSize.x, y), hud::ColScaleAlpha(infoColor, eased),
                          entry.infoText.c_str());
        }

        // Color bar on edge: a bright lit strip (brighter on the outer edge) with
        // a soft inner glow that gently pulses, so it reads like an LED, not a line.
        if (colorBar) {
            ImU32 mc = hud::ColScaleAlpha(moduleColor, eased);
            ImU32 barBright = hud::ColScaleAlpha(hud::ColBrighten(moduleColor, (int)(70 * pulse)), eased);
            float glowW = 4.0f * scale;
            ImU32 glowOn = hud::ColScaleAlpha(moduleColor, 0.45f * eased * pulse);
            ImU32 glowOff = hud::ColScaleAlpha(moduleColor, 0.0f);
            if (anchorLeft) {
                // bar hugs the left edge; glow spills to the right, over the text
                draw->AddRectFilledMultiColor(ImVec2(barX2, y), ImVec2(barX2 + glowW, y + lineHeight), glowOn,
                                              glowOff, glowOff, glowOn);
                draw->AddRectFilledMultiColor(ImVec2(barX1, y), ImVec2(barX2, y + lineHeight), barBright, mc, mc,
                                              barBright);
            } else {
                // bar hugs the right edge; glow spills to the left, over the text
                draw->AddRectFilledMultiColor(ImVec2(barX1 - glowW, y), ImVec2(barX1, y + lineHeight), glowOff,
                                              glowOn, glowOn, glowOff);
                draw->AddRectFilledMultiColor(ImVec2(barX1, y), ImVec2(barX2, y + lineHeight), mc, barBright,
                                              barBright, mc);
            }
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
