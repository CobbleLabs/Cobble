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
        lastW = 0.0f;
        lastH = 0.0f;
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

    // Straight edges on purpose. This used to be a 6px-rounded pill with a
    // pill-rounded accent tick floating inside it and a round dot separator, and at
    // watermark size those three radii fought each other and read as mush. The card
    // is now a plain rectangle with a full-height accent edge, a 1px rule between
    // the two halves, and every coordinate snapped to whole pixels so the text stays
    // crisp instead of resampled.
    const float s = scale;
    const float fontSize = std::floor(19.0f * s);
    const float metaFS = std::floor(fontSize * 0.85f);  // version + "FPS" read as secondary
    const float padX = std::floor(10.0f * s);
    const float padY = std::floor(6.0f * s);
    const float margin = std::floor(6.0f * s);

    // Accent color from the active theme (follows the global accent + rainbow).
    const ImU32 accent = GetAccentRGB();
    const ImU32 nameCol = IM_COL32(244, 245, 249, 255);  // same white as the menu brand
    const ImU32 gray = IM_COL32(140, 146, 158, 255);

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

    // Segments:  ▌ Cobble  v1.7.0 │ 240 FPS
    const char* nameStr = "Cobble";
    const std::string verStr = "v" COBBLE_DISPLAY_VERSION;
    const char* fpsSuf = "FPS";

    const ImVec2 nameSz = fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, nameStr);
    const ImVec2 verSz = fReg->CalcTextSizeA(metaFS, FLT_MAX, 0.0f, verStr.c_str());
    const ImVec2 fpsNumSz = fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fpsNum);
    const ImVec2 fpsSufSz = fReg->CalcTextSizeA(metaFS, FLT_MAX, 0.0f, fpsSuf);

    const float textH = std::floor(nameSz.y);
    const float barW = (std::max)(2.0f, std::floor(3.0f * s));  // accent edge, flush left
    const float gBar = std::floor(9.0f * s);                    // bar -> name
    const float gNameVer = std::floor(7.0f * s);
    const float sepW = (std::max)(1.0f, std::floor(s));  // hairline rule, not a dot
    const float gSep = std::floor(9.0f * s);             // air on both sides of the rule
    const float gNumSuf = std::floor(4.0f * s);

    const float contentW = barW + gBar + std::ceil(nameSz.x) + gNameVer + std::ceil(verSz.x) + gSep + sepW + gSep +
                           std::ceil(fpsNumSz.x) + gNumSuf + std::ceil(fpsSufSz.x);

    const float boxW = contentW + padX;  // the accent edge replaces the left padding
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

    // Snap the card to whole pixels. A watermark parked on a half-pixel resamples
    // every glyph and is a big part of why the old one looked soft.
    const float x1 = std::floor(posX), y1 = std::floor(posY);
    const float x2 = x1 + boxW, y2 = y1 + boxH;

    // Publish the drawn rect so the ArrayList's auto-placement can dodge it.
    lastX = x1;
    lastY = y1;
    lastW = boxW;
    lastH = boxH;

    const float sd = (std::max)(1.0f, std::floor(s));

    // ---- Card: a sharp, plain black panel. Zero corner radius on purpose: on a
    // ~30px-tall box any radius large enough to notice also clips the accent edge
    // and bows the text's own bounding box, which is exactly what made the old
    // pill read as a smudge. One flat fill, one hairline border. ---------------
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 0, 0, 135), 0.0f);
    draw->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(255, 255, 255, 16), 0.0f, 0, sd);

    const float ty = std::floor(y1 + padY);
    const float cy = std::floor((y1 + y2) * 0.5f);
    float cx = x1;

    // Baselines, so the smaller meta text sits on the same line as the name
    // instead of being top-aligned against a larger em box.
    const float baseline = ty + fBold->Ascent * (fontSize / fBold->FontSize);
    const float metaY = std::floor(baseline - fReg->Ascent * (metaFS / fReg->FontSize));

    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    // Accent edge: full card height, flush with the left border, square ends. It
    // belongs to the card instead of floating inside it like the old pill tick.
    draw->AddRectFilled(ImVec2(cx, y1), ImVec2(cx + barW, y2), accent, 0.0f);
    cx += barW + gBar;

    // "Cobble" — per-character rainbow, or plain white (the accent is carried by
    // the edge and the FPS number, so the name itself stays clean). No drop
    // shadow: the card already supplies the contrast, and doubling every glyph
    // just thickens the text.
    const float rainbowSat = 0.7f;
    const int nameLen = (int)std::char_traits<char>::length(nameStr);
    if (rainbow) {
        for (int i = 0; i < nameLen; ++i) {
            const char ch[2] = {nameStr[i], 0};
            const ImU32 col = hud::GetRainbowColorEx(i, rainbowSat, rainbowTimeMs, rainbowType);
            draw->AddText(fBold, fontSize, ImVec2(cx, ty), col, ch);
            cx += fBold->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ch).x;
        }
        cx = std::floor(cx);
    } else {
        // One draw call, and no per-glyph advance rounding: kerning stays intact.
        draw->AddText(fBold, fontSize, ImVec2(cx, ty), nameCol, nameStr);
        cx += std::ceil(nameSz.x);
    }
    cx += gNameVer;

    // Version — muted, smaller, baseline-aligned.
    draw->AddText(fReg, metaFS, ImVec2(cx, metaY), gray, verStr.c_str());
    cx += std::ceil(verSz.x) + gSep;

    // Hairline rule instead of a dot: a 1px vertical tick is the one separator
    // shape that can't disagree with a square card.
    const float ruleH = std::floor(textH * 0.36f);
    draw->AddRectFilled(ImVec2(cx, cy - ruleH), ImVec2(cx + sepW, cy + ruleH), IM_COL32(255, 255, 255, 45), 0.0f);
    cx += sepW + gSep;

    // FPS: accent number, muted "FPS".
    draw->AddText(fBold, fontSize, ImVec2(cx, ty), accent, fpsNum);
    cx += std::ceil(fpsNumSz.x) + gNumSuf;
    draw->AddText(fReg, metaFS, ImVec2(cx, metaY), gray, fpsSuf);
}
