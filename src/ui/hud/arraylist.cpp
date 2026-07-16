#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "arraylist.h"
#include "../../../infra/ext/imgui/imgui.h"
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
#include "hud_drag.hpp"

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
    // If a drag is in progress but the list can't draw (disabled, no player, or
    // no active modules), cancel it so the menu doesn't stay hidden forever.
    auto cancelMove = []() {
        if (dragging) {
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

    // Font picker: 0 = Inter Regular (slot 5, clean default), 1 = Inter Bold (slot 2).
    ImFont* arrayFont = font(fontStyle == 1 ? 2 : 5);
    if (!arrayFont)
        arrayFont = font(2);  // fall back to Bold if the regular slot is unavailable
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

    // Keep the anchored edge fixed when the block resizes (e.g. a module toggles
    // on/off changes the width). posX/posY track the top-left corner, but a
    // right-anchored list should keep its right edge put, and a bottom-anchored
    // one its bottom edge -- otherwise the block drifts out of the corner when a
    // row appears/disappears. Skip while dragging (the user owns the position).
    static float prevBlockWidth = -1.0f;
    static float prevBlockHeight = -1.0f;
    if (prevBlockWidth >= 0.0f && !dragging) {
        if (!anchorLeft)
            posX += prevBlockWidth - blockWidth;  // keep right edge fixed
        if (anchorBottom)
            posY += prevBlockHeight - blockHeight;  // keep bottom edge fixed
    }
    prevBlockWidth = blockWidth;
    prevBlockHeight = blockHeight;

    // Shared grab-and-drag (same manner + style as Dropped Pots): hover the block
    // with the menu open to grab it, drop to place. Position persists on menu
    // close (main thread), not here (render thread).
    hud::dragBox(posX, posY, blockWidth, blockHeight, 2.0f, dragging, dragOffsetX, dragOffsetY);

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

    // Row animation timing.
    float dtA = ImGui::GetIO().DeltaTime;
    if (dtA <= 0.0f || dtA > 0.25f) dtA = 1.0f / 60.0f;  // guard hitches / first frame

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
            moduleColor = hud::GetRainbowColorEx(idx, rainbowSaturation, rainbowTimeMs, rainbowType);
        } else {
            // Use custom color if set, otherwise use accent color
            if (color[0] < 0) {
                // Accent color from theme (follows the global accent + rainbow).
                moduleColor = GetAccentRGB();
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

        // United look: rows share one continuous background. Round only the OUTER
        // corners of the whole block — the top corners of the first row and the
        // bottom corners of the last row — and leave the shared inner edges square
        // so consecutive rows fuse into a single connected shape instead of a
        // stack of separate pills.
        const int rowCount = static_cast<int>(entries.size());
        const bool isFirst = (idx == 0);
        const bool isLast = (idx == rowCount - 1);
        ImDrawFlags cornerFlags;
        if (rowCount == 1)
            cornerFlags = ImDrawFlags_RoundCornersAll;
        else if (isFirst)
            cornerFlags = ImDrawFlags_RoundCornersTop;
        else if (isLast)
            cornerFlags = ImDrawFlags_RoundCornersBottom;
        else
            cornerFlags = ImDrawFlags_RoundCornersNone;

        // Background: flat rounded dark base, no accent wash.
        if (showBackground) {
            draw->AddRectFilled(ImVec2(rectX1, y), ImVec2(rectX2, y + lineHeight),
                                hud::ColScaleAlpha(IM_COL32(19, 19, 19, 150), eased), rounding, cornerFlags);
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

        // Color bar on edge: a clean solid strip of the module color. Rounded on
        // its OUTER corners only (first/last row) so it fuses into one continuous
        // accent strip matching the united background instead of square-capped
        // segments poking past the block's rounded corners.
        if (colorBar) {
            ImU32 mc = hud::ColScaleAlpha(moduleColor, eased);
            ImDrawFlags barFlags;
            if (rowCount == 1)
                barFlags = anchorLeft ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight;
            else if (isFirst)
                barFlags = anchorLeft ? ImDrawFlags_RoundCornersTopLeft : ImDrawFlags_RoundCornersTopRight;
            else if (isLast)
                barFlags = anchorLeft ? ImDrawFlags_RoundCornersBottomLeft : ImDrawFlags_RoundCornersBottomRight;
            else
                barFlags = ImDrawFlags_RoundCornersNone;
            float barRound = (barFlags == ImDrawFlags_RoundCornersNone) ? 0.0f : rounding;
            draw->AddRectFilled(ImVec2(barX1, y), ImVec2(barX2, y + lineHeight), mc, barRound, barFlags);
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
