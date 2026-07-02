#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "watermark.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"
#include "../../util/logger.hpp"
#include "../hud/arraylist.h"
#include "../menu/menu.h"
#include "../widgets/widgets.hpp"
#include "hud_color.hpp"
#include "generated/version.h"  // COBBLE_VERSION

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

void Watermark::render() {
    auto cancelMove = []() {
        if (placing || dragging) {
            placing = false;
            dragging = false;
            Menu::hidden = false;
        }
    };

    // Tied to the ArrayList: hide the watermark whenever the watermark toggle is
    // off OR the ArrayList itself is disabled (even while the menu is open, which
    // forces the overlay frame to render for the menu).
    if (!enabled || !ArrayListRenderer::enabled) {
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
    const float barW = 3.0f * s;
    const float rounding = 6.0f * s;
    const float margin = 6.0f * s;
    const double time = ImGui::GetTime();

    // Accent color from the active theme.
    hud::RGB ac{g_SchemeColor[0] * 255.0f, g_SchemeColor[1] * 255.0f, g_SchemeColor[2] * 255.0f};
    ImU32 accent = IM_COL32((int)ac.r, (int)ac.g, (int)ac.b, 255);
    ImU32 white = IM_COL32(238, 238, 240, 255);
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

    float boxW = barW + padX + contentW + padX;
    float boxH = padY + textH + padY;

    float screenWidth = static_cast<float>(RenderData::screenWidth);
    float screenHeight = static_cast<float>(RenderData::screenHeight);

    if (posX < 0.0f || posY < 0.0f) {
        posX = margin;
        posY = margin;
    }

    // ---- Move / drag (same pattern as the ArrayList) --------------------------
    ImVec2 mousePos = ImGui::GetMousePos();
    if (placing) {
        posX = mousePos.x - boxW * 0.5f;
        posY = mousePos.y - boxH * 0.5f;
        draw->AddRect(ImVec2(posX - 2.0f, posY - 2.0f), ImVec2(posX + boxW + 2.0f, posY + boxH + 2.0f),
                      IM_COL32(120, 200, 255, 255), rounding, 0, 1.5f);
        if (ImGui::IsMouseClicked(0)) {
            placing = false;
            Menu::hidden = false;
            log_ok("Watermark placed at (%.0f, %.0f)", posX, posY);
        }
    } else {
        bool hover = mousePos.x >= posX && mousePos.x <= posX + boxW && mousePos.y >= posY &&
                     mousePos.y <= posY + boxH;
        if (Menu::isOpen && !dragging && hover) {
            draw->AddRect(ImVec2(posX - 2.0f, posY - 2.0f), ImVec2(posX + boxW + 2.0f, posY + boxH + 2.0f),
                          IM_COL32(120, 200, 255, 255), rounding, 0, 1.5f);
            if (ImGui::IsMouseClicked(0)) {
                dragging = true;
                dragOffsetX = mousePos.x - posX;
                dragOffsetY = mousePos.y - posY;
                Menu::hidden = true;
                log_ok("Watermark drag start");
            }
        }
        if (dragging) {
            if (ImGui::IsMouseDown(0)) {
                posX = mousePos.x - dragOffsetX;
                posY = mousePos.y - dragOffsetY;
            } else {
                dragging = false;
                Menu::hidden = false;
                log_ok("Watermark dropped at (%.0f, %.0f)", posX, posY);
            }
        }
    }

    if (screenWidth > 0.0f && screenHeight > 0.0f) {
        posX = (std::clamp)(posX, 0.0f, (std::max)(0.0f, screenWidth - boxW));
        posY = (std::clamp)(posY, 0.0f, (std::max)(0.0f, screenHeight - boxH));
    }

    float x1 = posX, y1 = posY;
    float x2 = x1 + boxW, y2 = y1 + boxH;

    // ---- Background: soft glow + gradient pill + accent edge + glass highlight -
    // Outer drop shadow / glow: a few expanding rounded rects with falling alpha
    // give the box a soft lift off the HUD instead of a hard edge.
    for (int i = 4; i >= 1; --i) {
        float gExp = i * 2.0f * s;
        int gA = 44 / i;  // 11, 14, 22, 44 -> outer softer
        draw->AddRectFilled(ImVec2(x1 - gExp, y1 - gExp), ImVec2(x2 + gExp, y2 + gExp),
                            IM_COL32(0, 0, 0, gA), rounding + gExp);
    }
    // Accent halo hugging the border so the pill reads as lit by the theme color.
    // Breathes very gently in sync with the accent bar below.
    float halo = 0.6f + 0.4f * (float)std::sin(time * 1.6);
    for (int i = 3; i >= 1; --i) {
        float hExp = i * 1.6f * s;
        int hA = (int)((26 / i) * halo);
        draw->AddRect(ImVec2(x1 - hExp, y1 - hExp), ImVec2(x2 + hExp, y2 + hExp),
                      IM_COL32((int)ac.r, (int)ac.g, (int)ac.b, hA), rounding + hExp, 0, 1.5f * s);
    }

    // Rounded fill (AddRectFilledMultiColor can't round corners) with a manual
    // vertical gradient: two stacked rects, darker at the bottom, clipped to the
    // rounded shape via the base fill drawn first.
    ImU32 bgTop = IM_COL32(30, 30, 36, 220);
    ImU32 bgBot = IM_COL32(16, 16, 20, 220);
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), bgBot, rounding);
    draw->AddRectFilledMultiColor(ImVec2(x1 + rounding, y1), ImVec2(x2 - rounding, y2), bgTop, bgTop, bgBot,
                                  bgBot);

    // Accent border + a slightly brighter inner ring for depth.
    draw->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32((int)ac.r, (int)ac.g, (int)ac.b, 110), rounding, 0,
                  1.0f * s);
    // Soft top glass highlight, brightest in the middle and fading to the rounded
    // ends (two mirrored gradient strips) so it reads like light catching glass.
    {
        float hlY0 = y1 + 1.0f, hlY1 = y1 + 2.5f * s;
        float hlL = x1 + rounding, hlR = x2 - rounding, hlM = (hlL + hlR) * 0.5f;
        ImU32 hlEdge = IM_COL32(255, 255, 255, 0);
        ImU32 hlMid = IM_COL32(255, 255, 255, 55);
        draw->AddRectFilledMultiColor(ImVec2(hlL, hlY0), ImVec2(hlM, hlY1), hlEdge, hlMid, hlMid, hlEdge);
        draw->AddRectFilledMultiColor(ImVec2(hlM, hlY0), ImVec2(hlR, hlY1), hlMid, hlEdge, hlEdge, hlMid);
    }

    // Animated accent bar on the left edge: a vertical gradient (bright top ->
    // accent bottom) whose overall brightness pulses gently, wrapped in a faint
    // outward glow so it reads as a lit strip rather than a flat rect.
    float pulse = 0.75f + 0.25f * (float)std::sin(time * 2.0);
    ImU32 barGlow = IM_COL32((int)ac.r, (int)ac.g, (int)ac.b, (int)(80 * pulse));
    draw->AddRectFilled(ImVec2(x1 - 1.0f * s, y1), ImVec2(x1 + barW + 2.5f * s, y2), barGlow, rounding,
                        ImDrawFlags_RoundCornersLeft);
    ImU32 barTop = IM_COL32((int)(std::min)(255.0f, ac.r * 1.35f + 40.0f),
                            (int)(std::min)(255.0f, ac.g * 1.35f + 40.0f),
                            (int)(std::min)(255.0f, ac.b * 1.35f + 40.0f), 255);
    ImU32 barBot = IM_COL32((int)(ac.r * pulse), (int)(ac.g * pulse), (int)(ac.b * pulse), 255);
    // Base rounded fill (rounding on the left), then a vertical gradient on top of
    // the straight portion so the corners stay rounded but the strip has depth.
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x1 + barW, y2), barBot, rounding, ImDrawFlags_RoundCornersLeft);
    draw->AddRectFilledMultiColor(ImVec2(x1 + rounding * 0.5f, y1), ImVec2(x1 + barW, y2), barTop, barTop,
                                  barBot, barBot);

    // ---- Sheen sweep: a soft diagonal glare crosses the pill every few seconds -
    // Clipped to the box so it stays inside the rounded shape. Off most of the
    // time (long period), then a quick bright pass for a "glass" catch of light.
    {
        const double sweepPeriod = 4.5;   // seconds between passes
        const double sweepDur = 0.85;     // seconds the glare takes to cross
        double sp = std::fmod(time, sweepPeriod);
        if (sp < sweepDur) {
            float n = (float)(sp / sweepDur);          // 0..1 across the box
            float w = 9.0f * s;                        // glare half-width
            float slant = boxH * 0.5f;                 // diagonal lean
            float travel = boxW + slant + 4.0f * w;
            float sx = x1 - 2.0f * w + n * travel;     // leading edge x
            // Fade in/out at the ends of the pass so it doesn't pop.
            float edge = (std::min)(n, 1.0f - n) * 2.0f;
            edge = (std::clamp)(edge, 0.0f, 1.0f);
            draw->PushClipRect(ImVec2(x1, y1), ImVec2(x2, y2), true);
            // Three stacked parallelograms: dim, bright, dim -> soft band.
            const struct { float off; int a; } bands[] = {{-w, 16}, {0.0f, 40}, {w, 16}};
            for (const auto& bnd : bands) {
                float cx = sx + bnd.off;
                ImU32 col = IM_COL32(255, 255, 255, (int)(bnd.a * edge));
                draw->AddQuadFilled(ImVec2(cx, y1), ImVec2(cx + w, y1), ImVec2(cx + w - slant, y2),
                                    ImVec2(cx - slant, y2), col);
            }
            draw->PopClipRect();
        }
    }

    // ---- Text -----------------------------------------------------------------
    float tx = x1 + barW + padX;
    float ty = y1 + padY;

    // "Cobble" as a flowing per-character gradient (rainbow) or flat accent.
    // Uses the same rainbow cycle + saturation setting as the ArrayList so both
    // HUD elements stay in sync.
    long long rainbowTimeMs = 0;
    if (rainbow) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        rainbowTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }
    float cx = tx;
    for (int i = 0; nameStr[i]; ++i) {
        char ch[2] = {nameStr[i], 0};
        ImU32 col;
        if (rainbow) {
            col = hud::GetRainbowColor(i, ArrayListRenderer::rainbowSaturation, rainbowTimeMs);
        } else {
            col = accent;
        }
        draw->AddText(f, fontSize, ImVec2(cx + 1.0f * s, ty + 1.0f * s), shadow, ch);
        draw->AddText(f, fontSize, ImVec2(cx, ty), col, ch);
        cx += f->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ch).x;
    }

    float verX = tx + nameSz.x;
    float fpsNumX = verX + verSz.x + sepSz.x;
    float fpsSufX = fpsNumX + fpsNumSz.x;

    // Shadows
    float sd = 1.0f * s;
    draw->AddText(f, fontSize, ImVec2(verX + sd, ty + sd), shadow, verStr.c_str());
    draw->AddText(f, fontSize, ImVec2(fpsNumX + sd, ty + sd), shadow, fpsNum);
    draw->AddText(f, fontSize, ImVec2(fpsSufX + sd, ty + sd), shadow, fpsSuf);

    // Foreground: white version, accent FPS number, gray " FPS".
    draw->AddText(f, fontSize, ImVec2(verX, ty), white, verStr.c_str());
    draw->AddText(f, fontSize, ImVec2(fpsNumX, ty), accent, fpsNum);
    draw->AddText(f, fontSize, ImVec2(fpsSufX, ty), gray, fpsSuf);
}
