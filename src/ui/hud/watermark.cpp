#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "watermark.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"
#include "../menu/menu.h"
#include "../widgets/widgets.hpp"
#include "hud_color.hpp"
#include "hud_drag.hpp"
#include "generated/version.h"  // COBBLE_VERSION

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

void Watermark::render() {
    auto cancelMove = []() {
        if (dragging) {
            dragging = false;
            Menu::hidden = false;
        }
    };

    // Independent HUD element: hide only when its own toggle is off.
    if (!enabled) {
        cancelMove();
        return;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw) {
        cancelMove();
        return;
    }

    ImFont* f = font(2);  // bold, same as the ArrayList
    if (!f) {
        cancelMove();
        return;
    }

    const float s = scale;
    const float fontSize = 20.0f * s;
    const float padX = 10.0f * s;
    const float padY = 6.0f * s;
    const float rounding = 6.0f * s;
    const float margin = 6.0f * s;

    // Accent color from the active theme.
    hud::RGB ac{g_SchemeColor[0] * 255.0f, g_SchemeColor[1] * 255.0f, g_SchemeColor[2] * 255.0f};
    ImU32 accent = IM_COL32((int)ac.r, (int)ac.g, (int)ac.b, 255);
    ImU32 gray = IM_COL32(150, 155, 165, 255);
    ImU32 shadow = IM_COL32(0, 0, 0, 200);

    // Text segments: "Cobble"  " v1.3.2"   "240"  " FPS"
    const char* nameStr = "Cobble";
    const std::string verStr = " v" COBBLE_VERSION;

    // Ease the displayed FPS toward the real value (seed on first frame so it
    // doesn't animate up from zero when the watermark first appears).
    float fpsTarget = static_cast<float>(RenderData::gameFps);
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;  // guard against hitches / first frame
    if (fpsSmooth <= 0.0f) fpsSmooth = fpsTarget;
    fpsSmooth += (fpsTarget - fpsSmooth) * (std::min)(1.0f, dt * 8.0f);
    char fpsNum[16];
    std::snprintf(fpsNum, sizeof(fpsNum), "%d", (int)(fpsSmooth + 0.5f));
    const char* fpsSuf = " FPS";
    const char* sep = "  ";

    ImVec2 nameSz = f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, nameStr);
    ImVec2 verSz = f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, verStr.c_str());
    ImVec2 sepSz = f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, sep);
    ImVec2 fpsNumSz = f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fpsNum);
    ImVec2 fpsSufSz = f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fpsSuf);

    float textH = nameSz.y;
    float contentW = nameSz.x + verSz.x + sepSz.x + fpsNumSz.x + fpsSufSz.x;

    float boxW = padX + contentW + padX;
    float boxH = padY + textH + padY;

    float screenWidth = static_cast<float>(RenderData::screenWidth);
    float screenHeight = static_cast<float>(RenderData::screenHeight);

    if (posX < 0.0f || posY < 0.0f) {
        posX = margin;
        posY = margin;
    }

    // Shared grab-and-drag (same manner + style as Dropped Pots): hover the box
    // with the menu open to grab it, drop to place.
    hud::dragBox(posX, posY, boxW, boxH, 2.0f, dragging, dragOffsetX, dragOffsetY);

    if (screenWidth > 0.0f && screenHeight > 0.0f) {
        posX = (std::clamp)(posX, 0.0f, (std::max)(0.0f, screenWidth - boxW));
        posY = (std::clamp)(posY, 0.0f, (std::max)(0.0f, screenHeight - boxH));
    }

    float x1 = posX, y1 = posY;
    float x2 = x1 + boxW, y2 = y1 + boxH;

    // ---- Background: flat rounded dark base. No accent wash, no side bar, no
    // outer glow, no gradient pill, no glass sheen.
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(19, 19, 19, 210), rounding);

    // ---- Text -----------------------------------------------------------------
    float tx = x1 + padX;
    float ty = y1 + padY;
    float sd = 1.0f * s;

    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    // "Cobble" + version flow together as one per-character rainbow (or flat
    // accent when rainbow is off).
    const float rainbowSat = 0.7f;
    const std::string head = std::string(nameStr) + verStr;
    float cx = tx;
    for (int i = 0; i < (int)head.size(); ++i) {
        char ch[2] = {head[i], 0};
        ImU32 col = rainbow ? hud::GetRainbowColor(i, rainbowSat, rainbowTimeMs) : accent;
        draw->AddText(f, fontSize, ImVec2(cx + sd, ty + sd), shadow, ch);
        draw->AddText(f, fontSize, ImVec2(cx, ty), col, ch);
        cx += f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ch).x;
    }

    // FPS trails after the sep: accent number, gray " FPS".
    float fpsNumX = tx + nameSz.x + verSz.x + sepSz.x;
    float fpsSufX = fpsNumX + fpsNumSz.x;
    draw->AddText(f, fontSize, ImVec2(fpsNumX + sd, ty + sd), shadow, fpsNum);
    draw->AddText(f, fontSize, ImVec2(fpsSufX + sd, ty + sd), shadow, fpsSuf);
    draw->AddText(f, fontSize, ImVec2(fpsNumX, ty), accent, fpsNum);
    draw->AddText(f, fontSize, ImVec2(fpsSufX, ty), gray, fpsSuf);
}
