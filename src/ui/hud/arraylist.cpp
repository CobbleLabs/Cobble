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
#include "../core/hud_fonts.h"
#include "../core/overlay.h"
#include "../menu/menu.h"
#include "../widgets/widgets.hpp"
#include "hud_color.hpp"
#include "hud_drag.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

// Persistent row state, held in draw order.
//
// A row outlives the module being enabled: when a module turns off its row keeps
// its slot and folds away (height + alpha) instead of disappearing between two
// frames, which is what made the list pop on every toggle. render() runs
// single-threaded on the GL thread, so plain statics are safe here.
namespace {

struct Row {
    std::string name;
    std::string info;
    float prog = 0.0f;   // 0 = fully folded away, 1 = fully in
    bool alive = false;  // seen in this frame's module sweep
    // Re-measured every frame: font, scale and the info text all change under us.
    float nameW = 0.0f;
    float infoW = 0.0f;
    float itemW = 0.0f;
};

std::vector<Row> g_rows;

void markLive(const std::string& name, const std::string& info) {
    for (auto& r : g_rows) {
        if (r.name == name) {
            r.alive = true;
            r.info = info;
            return;
        }
    }
    Row row;
    row.name = name;
    row.info = info;
    row.alive = true;
    g_rows.push_back(std::move(row));
}

inline float easeOutCubic(float t) {
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

}  // namespace

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
        g_rows.clear();
        cancelMove();
        return;
    }
    // Render while the menu is open too, so the block can be grabbed and dragged
    // for positioning (same as the DroppedInfo overlay).
    if (!player.isValid()) {
        g_rows.clear();
        cancelMove();
        return;  // Hide in main menu
    }

    // ---- Live sweep -------------------------------------------------------
    for (auto& r : g_rows)
        r.alive = false;

    for (auto* mod : moduleManager->getModules()) {
        if (mod && mod->enabled)
            markLive(mod->name, showInfo ? mod->getDisplayInfo() : std::string());
    }

    // AutoRefill and ArmorSwitch keep working while disabled (passive
    // inventory-open mode), so they still belong in the list.
    if (autoRefill && !autoRefill->enabled && autoRefill->settings.onInventoryOpen)
        markLive(autoRefill->name, showInfo ? autoRefill->getDisplayInfo() : std::string());
    if (armorSwitch && !armorSwitch->enabled && armorSwitch->settings.onInventoryOpen)
        markLive(armorSwitch->name, showInfo ? armorSwitch->getDisplayInfo() : std::string());

    // ---- Fold in / out ----------------------------------------------------
    float dtA = ImGui::GetIO().DeltaTime;
    if (dtA <= 0.0f || dtA > 0.25f)
        dtA = 1.0f / 60.0f;  // guard hitches / first frame
    const float step = dtA * 7.0f;  // ~0.14s each way
    for (auto& r : g_rows)
        r.prog = (std::clamp)(r.prog + (r.alive ? step : -step), 0.0f, 1.0f);
    g_rows.erase(std::remove_if(g_rows.begin(), g_rows.end(),
                                [](const Row& r) { return !r.alive && r.prog <= 0.0f; }),
                 g_rows.end());

    if (g_rows.empty()) {
        cancelMove();
        return;
    }

    // Font picker: a stable hud_fonts ID (0 = Inter Regular, 1 = Inter Bold, then
    // whatever system families this machine has).
    ImFont* arrayFont = hud_fonts::GetById(fontStyle);
    if (!arrayFont)
        arrayFont = font(5);
    if (!arrayFont)
        arrayFont = font(2);
    if (!arrayFont || arrayFont->FontSize <= 0.0f) {
        cancelMove();
        return;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw) {
        cancelMove();
        return;
    }

    // ---- Metrics ----------------------------------------------------------
    const float fontSize = 24.0f * scale;
    const float infoFS = fontSize * 0.86f;  // the info reads as secondary
    // CalcTextSizeA reports the em box (height == font size), so rows drawn at
    // exactly that height leave no air and the block reads as a wall of text.
    const float rowH = std::floor(fontSize * 1.18f);
    const float padIn = 7.0f * scale;   // air between the text and the row edge
    const float barWidth = 3.0f * scale;
    const float barGap = 6.0f * scale;  // air between the text and the colour bar
    const float infoGap = 5.0f * scale;
    const float rounding = 4.0f * scale;

    // Baseline-align the smaller info text against the name instead of
    // top-aligning two different font sizes.
    const float unit = 1.0f / arrayFont->FontSize;
    const float infoDrop = arrayFont->Ascent * unit * (fontSize - infoFS);

    for (auto& r : g_rows) {
        r.nameW = arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, r.name.c_str()).x;
        r.infoW = r.info.empty() ? 0.0f : arrayFont->CalcTextSizeA(infoFS, FLT_MAX, 0.0f, r.info.c_str()).x;
        const float textW = r.nameW + (r.info.empty() ? 0.0f : infoGap + r.infoW);
        r.itemW = padIn + textW + (colorBar ? barGap + barWidth : padIn);
    }

    // stable_sort: equal-width rows must not trade places every frame.
    if (sortByLength)
        std::stable_sort(g_rows.begin(), g_rows.end(),
                         [](const Row& a, const Row& b) { return a.itemW > b.itemW; });

    float screenWidth = static_cast<float>(RenderData::screenWidth);
    float screenHeight = static_cast<float>(RenderData::screenHeight);

    // `anchor` now only decides row alignment (left/right) and the default
    // corner the block starts at before the user drags it elsewhere.
    bool anchorLeft = (anchor == 1 || anchor == 3);    // TopLeft or BottomLeft
    bool anchorBottom = (anchor == 2 || anchor == 3);  // BottomRight or BottomLeft

    // Whole-block dimensions. Both axes take the folded contribution so the box
    // grows and shrinks with the animation instead of snapping a frame early.
    float blockWidth = 0.0f;
    float blockHeight = 0.0f;
    for (const auto& r : g_rows) {
        const float e = easeOutCubic(r.prog);
        blockWidth = (std::max)(blockWidth, r.itemW * e);
        blockHeight += rowH * e;
    }

    // First time (or after a reset) place the block at the chosen corner.
    if (posX < 0.0f || posY < 0.0f) {
        posX = anchorLeft ? 0.0f : (screenWidth - blockWidth);
        posY = anchorBottom ? (screenHeight - yOffset - blockHeight) : yOffset;
    }

    // Keep the anchored edge fixed when the block resizes. What the user placed is
    // the corner the list hugs, not the top-left of the box, so a right-anchored
    // list has to keep its right edge and a bottom-anchored one its bottom edge --
    // otherwise the block drifts every time a row appears or disappears. Storing
    // the edge (rather than applying a size delta) also survives the list going
    // empty and coming back, which a running delta does not: it would restart from
    // a collapsed box and walk the whole list sideways. Skip while dragging (the
    // user owns the position).
    static float prevRightEdge = -1.0f;
    static float prevBottomEdge = -1.0f;
    if (!dragging) {
        if (!anchorLeft && prevRightEdge >= 0.0f)
            posX = prevRightEdge - blockWidth;
        if (anchorBottom && prevBottomEdge >= 0.0f)
            posY = prevBottomEdge - blockHeight;
    }

    // Shared grab-and-drag (same manner + style as Dropped Pots): hover the block
    // with the menu open to grab it, drop to place. Position persists on menu
    // close (main thread), not here (render thread).
    hud::dragBox(posX, posY, blockWidth, blockHeight, 2.0f, dragging, dragOffsetX, dragOffsetY);

    // Keep the block fully on-screen (parenthesized to dodge windows.h min/max macros).
    posX = (std::clamp)(posX, 0.0f, (std::max)(0.0f, screenWidth - blockWidth));
    posY = (std::clamp)(posY, 0.0f, (std::max)(0.0f, screenHeight - blockHeight));

    prevRightEdge = posX + blockWidth;
    prevBottomEdge = posY + blockHeight;

    const float blockX = posX;
    float y = posY;

    const ImU32 infoColor = IM_COL32(158, 163, 173, 255);

    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    const ImU32 flatColor =
        useAccent ? GetAccentRGB()
                  : IM_COL32((int)(color[0] * 255.0f), (int)(color[1] * 255.0f), (int)(color[2] * 255.0f), 255);

    const int rowCount = static_cast<int>(g_rows.size());
    for (int idx = 0; idx < rowCount; ++idx) {
        const Row& r = g_rows[static_cast<size_t>(idx)];
        const float e = easeOutCubic(r.prog);
        const float rh = rowH * e;

        const ImU32 moduleColor =
            rainbow ? hud::GetRainbowColorEx(idx, rainbowSaturation, rainbowTimeMs, rainbowType) : flatColor;

        // Rows align to one side of the (movable) block.
        float rectX1, rectX2, textX, barX1, barX2;
        if (anchorLeft) {
            rectX1 = blockX;
            rectX2 = blockX + r.itemW;
            textX = rectX1 + (colorBar ? barWidth + barGap : padIn);
            barX1 = blockX;
            barX2 = blockX + barWidth;
        } else {
            const float rightEdge = blockX + blockWidth;
            rectX1 = rightEdge - r.itemW;
            rectX2 = rightEdge;
            textX = rectX1 + padIn;
            barX1 = rightEdge - barWidth;
            barX2 = rightEdge;
        }

        // Slide in from whichever edge the block hugs.
        float slide = (1.0f - e) * 14.0f * scale;
        if (anchorLeft)
            slide = -slide;
        rectX1 += slide;
        rectX2 += slide;
        textX += slide;
        barX1 += slide;
        barX2 += slide;

        // United look: rows share one continuous background. Round only the OUTER
        // corners of the whole block — the top corners of the first row and the
        // bottom corners of the last row — and leave the shared inner edges square
        // so consecutive rows fuse into a single connected shape instead of a
        // stack of separate pills.
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
            draw->AddRectFilled(ImVec2(rectX1, y), ImVec2(rectX2, y + rh),
                                hud::ColScaleAlpha(IM_COL32(19, 19, 19, 150), e), rounding, cornerFlags);
        }

        // Text sits centred in the row, and folds toward the row centre with it.
        const float nameY = y + (rh - fontSize) * 0.5f;
        const float infoY = nameY + infoDrop;
        const float infoX = textX + r.nameW + infoGap;

        // Without a background the text needs its own contrast. A 4-tap dark
        // outline stays crisp over any terrain; a single diagonal drop shadow
        // only works when the background happens to be lighter on that side.
        if (!showBackground) {
            const ImU32 sh = hud::ColScaleAlpha(IM_COL32(0, 0, 0, 140), e);
            const float sd = (std::max)(1.0f, std::floor(scale));
            const float tap[4][2] = {{-sd, 0.0f}, {sd, 0.0f}, {0.0f, -sd}, {0.0f, sd}};
            for (const auto& t : tap) {
                draw->AddText(arrayFont, fontSize, ImVec2(textX + t[0], nameY + t[1]), sh, r.name.c_str());
                if (!r.info.empty())
                    draw->AddText(arrayFont, infoFS, ImVec2(infoX + t[0], infoY + t[1]), sh, r.info.c_str());
            }
        }

        draw->AddText(arrayFont, fontSize, ImVec2(textX, nameY), hud::ColScaleAlpha(moduleColor, e), r.name.c_str());
        if (!r.info.empty()) {
            draw->AddText(arrayFont, infoFS, ImVec2(infoX, infoY), hud::ColScaleAlpha(infoColor, e), r.info.c_str());
        }

        // Color bar on edge: a clean solid strip of the module color. Rounded on
        // its OUTER corners only (first/last row) so it fuses into one continuous
        // accent strip matching the united background instead of square-capped
        // segments poking past the block's rounded corners.
        if (colorBar) {
            const ImU32 mc = hud::ColScaleAlpha(moduleColor, e);
            ImDrawFlags barFlags;
            if (rowCount == 1)
                barFlags = anchorLeft ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight;
            else if (isFirst)
                barFlags = anchorLeft ? ImDrawFlags_RoundCornersTopLeft : ImDrawFlags_RoundCornersTopRight;
            else if (isLast)
                barFlags = anchorLeft ? ImDrawFlags_RoundCornersBottomLeft : ImDrawFlags_RoundCornersBottomRight;
            else
                barFlags = ImDrawFlags_RoundCornersNone;
            const float barRound = (barFlags == ImDrawFlags_RoundCornersNone) ? 0.0f : rounding;
            draw->AddRectFilled(ImVec2(barX1, y), ImVec2(barX2, y + rh), mc, barRound, barFlags);
        }

        y += rh;
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
