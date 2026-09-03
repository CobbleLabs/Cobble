#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "watermark.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"
#include "../core/hud_fonts.h"
#include "../menu/menu.h"
#include "../widgets/widgets.hpp"
#include "hud_color.hpp"
#include "hud_drag.hpp"
#include "generated/version.h"  // COBBLE_VERSION

// The version shown on the watermark is deliberately NOT COBBLE_VERSION.
//
// COBBLE_VERSION comes from cobble.json and is the version handed to the auth
// server: the licence check, the telemetry payload, the updater check and the
// session validator all send it, so it has to name a version the server has
// actually released or login fails. The watermark, on the other hand, should say
// what build this really is. Keep this string as the source-of-truth build number
// and bump cobble.json only once that build ships.
#define COBBLE_DISPLAY_VERSION "1.7.0"

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

    // The picked family carries the name and the FPS number. Only the default
    // (Inter Bold) keeps the two-weight pairing — a picked family looks like a
    // mistake when half the line silently renders in Inter instead.
    ImFont* fBold = hud_fonts::GetById(fontStyle);
    if (!fBold)
        fBold = font(2);
    if (!fBold || fBold->FontSize <= 0.0f) {
        cancelMove();
        return;
    }
    ImFont* fReg = (fontStyle == hud_fonts::kInterBold) ? font(5) : fBold;
    if (!fReg || fReg->FontSize <= 0.0f)
        fReg = fBold;

    const float s = scale;
    const float fontSize = 19.0f * s;
    const float metaFS = fontSize * 0.85f;  // version + "FPS" read as secondary
    const float padX = 11.0f * s;
    const float padY = 7.0f * s;
    const float rounding = 6.0f * s;
    const float margin = 6.0f * s;

    // Accent color from the active theme (follows the global accent + rainbow).
    const ImU32 accent = GetAccentRGB();
    const ImU32 nameCol = IM_COL32(244, 245, 249, 255);  // same white as the menu brand
    const ImU32 gray = IM_COL32(140, 146, 158, 255);
    const ImU32 shadow = IM_COL32(0, 0, 0, 120);

    // Ease the displayed FPS toward the real value (seed on first frame so it
    // doesn't animate up from zero when the watermark first appears).
    float fpsTarget = static_cast<float>(RenderData::gameFps);
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.25f)
        dt = 1.0f / 60.0f;  // guard against hitches / first frame
    if (fpsSmooth <= 0.0f)
        fpsSmooth = fpsTarget;
    fpsSmooth += (fpsTarget - fpsSmooth) * (std::min)(1.0f, dt * 8.0f);
    char fpsNum[16];
    std::snprintf(fpsNum, sizeof(fpsNum), "%d", (int)(fpsSmooth + 0.5f));

    // Segments:  |  Cobble  v1.7.0  ·  240 FPS
    const char* nameStr = "Cobble";
    const std::string verStr = "v" COBBLE_DISPLAY_VERSION;
    const char* fpsSuf = "FPS";

    const ImVec2 nameSz = fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, nameStr);
    const ImVec2 verSz = fReg->CalcTextSizeA(metaFS, FLT_MAX, 0.0f, verStr.c_str());
    const ImVec2 fpsNumSz = fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fpsNum);
    const ImVec2 fpsSufSz = fReg->CalcTextSizeA(metaFS, FLT_MAX, 0.0f, fpsSuf);

    const float textH = nameSz.y;
    const float tickW = std::max(2.0f, 3.0f * s);  // accent bar, as on the menu header
    const float gTick = 9.0f * s;
    const float gNameVer = 7.0f * s;
    const float gDot = 8.0f * s;  // padding around the dot separator
    const float dotR = std::max(1.0f, 1.6f * s);
    const float gNumSuf = 4.0f * s;

    const float contentW = tickW + gTick + nameSz.x + gNameVer + verSz.x + gDot + dotR * 2.0f + gDot + fpsNumSz.x +
                           gNumSuf + fpsSufSz.x;

    const float boxW = padX + contentW + padX;
    const float boxH = padY + textH + padY;

    const float screenWidth = static_cast<float>(RenderData::screenWidth);
    const float screenHeight = static_cast<float>(RenderData::screenHeight);

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

    const float x1 = posX, y1 = posY;
    const float x2 = x1 + boxW, y2 = y1 + boxH;

    // ---- Card: plain black, semi-transparent glass (no metallic/blue tint, no
    // bright sheen) — just a dark panel the HUD text sits on. ------------------
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 0, 0, 130), rounding);
    // Very faint edge so it stays crisp against busy scenes.
    draw->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(255, 255, 255, 14), rounding, 0, std::max(1.0f, s));

    const float sd = std::max(1.0f, std::floor(s));
    const float ty = y1 + padY;
    const float cy = (y1 + y2) * 0.5f;
    float cx = x1 + padX;

    // Baselines, so the smaller meta text sits on the same line as the name
    // instead of being top-aligned against a larger em box.
    const float baseline = ty + fBold->Ascent * (fontSize / fBold->FontSize);
    const float metaY = baseline - fReg->Ascent * (metaFS / fReg->FontSize);

    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    // Accent tick: the same brand mark the ClickGUI header uses, so the HUD and
    // the menu read as one client.
    draw->AddRectFilled(ImVec2(cx, cy - textH * 0.40f), ImVec2(cx + tickW, cy + textH * 0.40f), accent, tickW * 0.5f);
    cx += tickW + gTick;

    // "Cobble" — per-character rainbow, or plain white (the accent is carried by
    // the tick and the FPS number, so the name itself stays clean).
    const float rainbowSat = 0.7f;
    const int nameLen = (int)std::char_traits<char>::length(nameStr);
    if (rainbow) {
        for (int i = 0; i < nameLen; ++i) {
            const char ch[2] = {nameStr[i], 0};
            const ImU32 col = hud::GetRainbowColorEx(i, rainbowSat, rainbowTimeMs, rainbowType);
            draw->AddText(fBold, fontSize, ImVec2(cx + sd, ty + sd), shadow, ch);
            draw->AddText(fBold, fontSize, ImVec2(cx, ty), col, ch);
            cx += fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ch).x;
        }
    } else {
        // One draw call, and no per-glyph advance rounding: kerning stays intact.
        draw->AddText(fBold, fontSize, ImVec2(cx + sd, ty + sd), shadow, nameStr);
        draw->AddText(fBold, fontSize, ImVec2(cx, ty), nameCol, nameStr);
        cx += nameSz.x;
    }
    cx += gNameVer;

    // Version — muted, smaller, baseline-aligned.
    draw->AddText(fReg, metaFS, ImVec2(cx + sd, metaY + sd), shadow, verStr.c_str());
    draw->AddText(fReg, metaFS, ImVec2(cx, metaY), gray, verStr.c_str());
    cx += verSz.x + gDot;

    // Dot separator: quieter than a full hairline rule.
    draw->AddCircleFilled(ImVec2(cx + dotR, cy), dotR, IM_COL32(255, 255, 255, 55), 8);
    cx += dotR * 2.0f + gDot;

    // FPS: accent number, muted "FPS".
    draw->AddText(fBold, fontSize, ImVec2(cx + sd, ty + sd), shadow, fpsNum);
    draw->AddText(fBold, fontSize, ImVec2(cx, ty), accent, fpsNum);
    cx += fpsNumSz.x + gNumSuf;
    draw->AddText(fReg, metaFS, ImVec2(cx + sd, metaY + sd), shadow, fpsSuf);
    draw->AddText(fReg, metaFS, ImVec2(cx, metaY), gray, fpsSuf);
}
