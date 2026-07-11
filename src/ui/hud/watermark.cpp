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

    // Bold "Cobble", regular (Inter) for the meta text — a premium weight contrast.
    ImFont* fBold = font(2);   // Inter Bold
    ImFont* fReg = font(5);    // Inter Regular (clean geometric sans)
    if (!fBold) {
        cancelMove();
        return;
    }
    if (!fReg)
        fReg = fBold;

    const float s = scale;
    const float fontSize = 19.0f * s;
    const float padX = 11.0f * s;
    const float padY = 7.0f * s;
    const float rounding = 6.0f * s;
    const float margin = 6.0f * s;

    // Accent color from the active theme (follows the global accent + rainbow).
    ImU32 accent = GetAccentRGB();
    ImU32 gray = IM_COL32(140, 146, 158, 255);
    ImU32 shadow = IM_COL32(0, 0, 0, 190);

    // Ease the displayed FPS toward the real value (seed on first frame so it
    // doesn't animate up from zero when the watermark first appears).
    float fpsTarget = static_cast<float>(RenderData::gameFps);
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;  // guard against hitches / first frame
    if (fpsSmooth <= 0.0f) fpsSmooth = fpsTarget;
    fpsSmooth += (fpsTarget - fpsSmooth) * (std::min)(1.0f, dt * 8.0f);
    char fpsNum[16];
    std::snprintf(fpsNum, sizeof(fpsNum), "%d", (int)(fpsSmooth + 0.5f));

    // Segments: (dot) Cobble  v1.3.2  |  240 FPS
    const char* nameStr = "Cobble";
    const std::string verStr = "v" COBBLE_VERSION;
    const char* fpsSuf = "FPS";

    ImVec2 nameSz = fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, nameStr);
    ImVec2 verSz = fReg->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, verStr.c_str());
    ImVec2 fpsNumSz = fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fpsNum);
    ImVec2 fpsSufSz = fReg->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fpsSuf);

    const float textH = nameSz.y;
    const float gNameVer = 7.0f * s;
    const float gDiv = 9.0f * s;   // padding around divider
    const float divW = std::max(1.0f, 1.0f * s);
    const float gNumSuf = 4.0f * s;

    float contentW = nameSz.x + gNameVer + verSz.x + gDiv + divW + gDiv + fpsNumSz.x + gNumSuf + fpsSufSz.x;

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

    // ---- Card: plain black, semi-transparent glass (no metallic/blue tint, no
    // bright sheen) — just a dark panel the HUD text sits on. ------------------
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 0, 0, 130), rounding);
    // Very faint edge so it stays crisp against busy scenes.
    draw->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(255, 255, 255, 14), rounding, 0, std::max(1.0f, s));

    float sd = 1.0f * s;
    float ty = y1 + padY;
    float cx = x1 + padX;

    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    // "Cobble" — per-character rainbow, or a clean white when rainbow is off (the
    // accent is carried by the dot + FPS number so the name stays crisp).
    const float rainbowSat = 0.7f;
    for (int i = 0; i < (int)std::char_traits<char>::length(nameStr); ++i) {
        char ch[2] = {nameStr[i], 0};
        ImU32 col = rainbow ? hud::GetRainbowColorEx(i, rainbowSat, rainbowTimeMs, rainbowType) : accent;
        draw->AddText(fBold, fontSize, ImVec2(cx + sd, ty + sd), shadow, ch);
        draw->AddText(fBold, fontSize, ImVec2(cx, ty), col, ch);
        cx += fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ch).x;
    }
    cx += gNameVer;

    // Version — muted regular.
    draw->AddText(fReg, fontSize, ImVec2(cx + sd, ty + sd), shadow, verStr.c_str());
    draw->AddText(fReg, fontSize, ImVec2(cx, ty), gray, verStr.c_str());
    cx += verSz.x + gDiv;

    // Metallic divider.
    draw->AddLine(ImVec2(cx, y1 + padY + 1.0f * s), ImVec2(cx, y2 - padY - 1.0f * s), IM_COL32(255, 255, 255, 28),
                  divW);
    cx += divW + gDiv;

    // FPS: accent number, muted " FPS".
    draw->AddText(fBold, fontSize, ImVec2(cx + sd, ty + sd), shadow, fpsNum);
    draw->AddText(fBold, fontSize, ImVec2(cx, ty), accent, fpsNum);
    cx += fpsNumSz.x + gNumSuf;
    draw->AddText(fReg, fontSize, ImVec2(cx + sd, ty + sd), shadow, fpsSuf);
    draw->AddText(fReg, fontSize, ImVec2(cx, ty), gray, fpsSuf);
}
