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
#include "generated/version.h"  // COBBLE_VERSION

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
struct RGBf {
    float r, g, b;
};

// HSB -> RGB (0..255), same math the ArrayList uses for its rainbow.
RGBf FromHSB(float h, float s, float b) {
    float c = b * s;
    float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = b - c;
    float r, g, bl;
    if (h < 1.0f / 6.0f) {
        r = c; g = x; bl = 0;
    } else if (h < 2.0f / 6.0f) {
        r = x; g = c; bl = 0;
    } else if (h < 3.0f / 6.0f) {
        r = 0; g = c; bl = x;
    } else if (h < 4.0f / 6.0f) {
        r = 0; g = x; bl = c;
    } else if (h < 5.0f / 6.0f) {
        r = x; g = 0; bl = c;
    } else {
        r = c; g = 0; bl = x;
    }
    return RGBf{(r + m) * 255.0f, (g + m) * 255.0f, (bl + m) * 255.0f};
}
}  // namespace

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
    RGBf ac{g_SchemeColor[0] * 255.0f, g_SchemeColor[1] * 255.0f, g_SchemeColor[2] * 255.0f};
    ImU32 accent = IM_COL32((int)ac.r, (int)ac.g, (int)ac.b, 255);
    ImU32 white = IM_COL32(238, 238, 240, 255);
    ImU32 gray = IM_COL32(150, 155, 165, 255);
    ImU32 shadow = IM_COL32(0, 0, 0, 200);

    // Text segments: "Cobble"  " v1.3.2"   "240"  " FPS"
    const char* nameStr = "Cobble";
    const std::string verStr = " v" COBBLE_VERSION;
    char fpsNum[16];
    std::snprintf(fpsNum, sizeof(fpsNum), "%d", RenderData::gameFps);
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

    // ---- Background: rounded pill + accent edge + glass highlight -------------
    // Solid rounded fill (AddRectFilledMultiColor can't round corners, which is
    // why the previous gradient showed square corners). Rounded fill first, then
    // a subtle accent border and a soft top highlight line inside the rounding.
    ImU32 bg = IM_COL32(20, 20, 24, 212);
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), bg, rounding);
    draw->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32((int)ac.r, (int)ac.g, (int)ac.b, 90), rounding, 0,
                  1.0f * s);
    draw->AddLine(ImVec2(x1 + rounding, y1 + 1.0f), ImVec2(x2 - rounding, y1 + 1.0f),
                  IM_COL32(255, 255, 255, 24), 1.0f);
    // Animated accent bar on the left edge (brightness pulses gently).
    float pulse = 0.75f + 0.25f * (float)std::sin(time * 2.0);
    ImU32 barCol = IM_COL32((int)(ac.r * pulse), (int)(ac.g * pulse), (int)(ac.b * pulse), 255);
    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x1 + barW, y2), barCol, rounding, ImDrawFlags_RoundCornersLeft);

    // ---- Text -----------------------------------------------------------------
    float tx = x1 + barW + padX;
    float ty = y1 + padY;

    // "Cobble" as a flowing per-character gradient (rainbow) or flat accent.
    float cx = tx;
    for (int i = 0; nameStr[i]; ++i) {
        char ch[2] = {nameStr[i], 0};
        ImU32 col;
        if (rainbow) {
            float hue = std::fmod((float)(time * 0.22) + i * 0.055f, 1.0f);
            RGBf c = FromHSB(hue, 0.55f, 1.0f);
            col = IM_COL32((int)c.r, (int)c.g, (int)c.b, 255);
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
