#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "watermark.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"
#include "../core/hud_fonts.h"
#include "../widgets/widgets.hpp"
#include "hud_color.hpp"
#include "hud_drag.hpp"
#include "hud_place.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

// Deliberately not COBBLE_VERSION: that one is sent to the auth server and has to
// name a released build, while the watermark shows the build actually running.
// Bump cobble.json only once this build ships.
#define COBBLE_DISPLAY_VERSION "1.7.0"

namespace {

constexpr ImU32 kNameColor = IM_COL32(244, 245, 249, 255);
constexpr ImU32 kMetaColor = IM_COL32(140, 146, 158, 255);
constexpr ImU32 kBorderColor = IM_COL32(255, 255, 255, 15);
constexpr ImU32 kSeparatorColor = IM_COL32(255, 255, 255, 46);

float g_fpsSmooth = 0.0f;

float ascentPx(const ImFont* font, float size) {
    return font->Ascent * (size / font->FontSize);
}

float textWidth(ImFont* font, float size, const char* begin, const char* end = nullptr) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x;
}

}  // namespace

void Watermark::render() {
    lastW = 0.0f;
    lastH = 0.0f;
    if (!enabled)
        return;

    ImFont* bold = hud_fonts::GetById(hud_fonts::kInterBold);
    ImFont* regular = hud_fonts::GetById(hud_fonts::kInterRegular);
    if (!bold || bold->FontSize <= 0.0f || !regular || regular->FontSize <= 0.0f)
        return;

    float screenW = 0.0f;
    float screenH = 0.0f;
    if (!hud::screenSize(screenW, screenH))
        return;

    const float s = (std::clamp)(scale, 0.25f, 8.0f);
    const float nameSize = 19.0f * s;
    const float metaSize = nameSize * 0.85f;

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.25f)
        dt = 1.0f / 60.0f;
    const float fpsNow = static_cast<float>(RenderData::gameFps);
    g_fpsSmooth = g_fpsSmooth <= 0.0f ? fpsNow : g_fpsSmooth + (fpsNow - g_fpsSmooth) * (std::min)(dt * 6.0f, 1.0f);
    char fps[16];
    std::snprintf(fps, sizeof(fps), "%d", static_cast<int>(g_fpsSmooth + 0.5f));

    const char* name = "Cobble";
    const char* version = "v" COBBLE_DISPLAY_VERSION;
    const char* fpsLabel = "FPS";

    // Offsets from the content origin.
    const float versionX = textWidth(bold, nameSize, name) + 7.0f * s;
    const float sepX = versionX + textWidth(regular, metaSize, version) + 9.0f * s;
    const float sepW = (std::max)(1.0f, s);
    const float fpsX = sepX + sepW + 9.0f * s;
    const float labelX = fpsX + textWidth(bold, nameSize, fps) + 4.0f * s;
    const float contentW = labelX + textWidth(regular, metaSize, fpsLabel);
    const float contentH = nameSize;

    const float padX = showBackground ? 10.0f * s : 0.0f;
    const float padY = showBackground ? 6.0f * s : 0.0f;
    const float barW = 3.0f * s;
    const float barSpace = showAccentBar ? barW + 9.0f * s : 0.0f;
    const float boxW = contentW + padX * 2.0f + barSpace;
    const float boxH = contentH + padY * 2.0f;

    float boxX = userPlaced ? relX * screenW : 6.0f * s;
    float boxY = userPlaced ? relY * screenH : 6.0f * s;
    hud::dragBox(boxX, boxY, boxW, boxH, 2.0f, dragging, dragOffsetX, dragOffsetY);
    if (dragging)
        userPlaced = true;
    boxX = (std::clamp)(boxX, 0.0f, (std::max)(0.0f, screenW - boxW));
    boxY = (std::clamp)(boxY, 0.0f, (std::max)(0.0f, screenH - boxH));
    if (userPlaced) {
        relX = boxX / screenW;
        relY = boxY / screenH;
    }

    lastX = boxX;
    lastY = boxY;
    lastW = boxW;
    lastH = boxH;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 accent = GetAccentRGB();

    if (showBackground) {
        const ImVec2 p1(boxX, boxY);
        const ImVec2 p2(boxX + boxW, boxY + boxH);
        draw->AddRectFilled(p1, p2, ImGui::ColorConvertFloat4ToU32(ImVec4(backgroundColor[0], backgroundColor[1],
                                                                          backgroundColor[2], backgroundColor[3])));
        draw->AddRect(p1, p2, kBorderColor, 0.0f, ImDrawFlags_None, (std::max)(0.5f, s));
    }
    if (showAccentBar)
        draw->AddRectFilled(ImVec2(boxX + padX, boxY + padY), ImVec2(boxX + padX + barW, boxY + boxH - padY), accent);

    const float x = boxX + padX + barSpace;
    const float y = boxY + padY;
    // Small text shares the big text's baseline instead of hanging from the top.
    const float metaY = y + ascentPx(bold, nameSize) - ascentPx(regular, metaSize);

    if (rainbowName) {
        // Wall clock, matching the ArrayList so both rainbows are in phase.
        const long long timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
        float cx = x;
        for (int i = 0; name[i] != '\0'; ++i) {
            draw->AddText(bold, nameSize, ImVec2(cx, y), hud::GetRainbowColorEx(i, 0.7f, timeMs, 0), name + i,
                          name + i + 1);
            cx += textWidth(bold, nameSize, name + i, name + i + 1);
        }
    } else {
        draw->AddText(bold, nameSize, ImVec2(x, y), kNameColor, name);
    }

    draw->AddText(regular, metaSize, ImVec2(x + versionX, metaY), kMetaColor, version);

    const float sepH = contentH * 0.36f;
    draw->AddRectFilled(ImVec2(x + sepX, y + (contentH - sepH) * 0.5f),
                        ImVec2(x + sepX + sepW, y + (contentH + sepH) * 0.5f), kSeparatorColor);

    draw->AddText(bold, nameSize, ImVec2(x + fpsX, y), accent, fps);
    draw->AddText(regular, metaSize, ImVec2(x + labelX, metaY), accent, fpsLabel);
}
