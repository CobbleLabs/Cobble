#pragma once

// ============================================================================
// Shared HUD color helpers.
//
// Single source of truth for HSB->RGB, the flowing rainbow cycle, and the
// packed-color tweaks used by the ArrayList and Watermark overlays. Both HUD
// elements draw the rainbow the same way and read the same saturation setting,
// so they stay visually consistent instead of each reimplementing FromHSB.
// ============================================================================

#include "../../../infra/ext/imgui/imgui.h"

#include <cmath>

namespace hud {

// RGB triple in 0..255 float range.
struct RGB {
    float r, g, b;
};

// HSB/HSV -> RGB (channels 0..255).
inline RGB FromHSB(float h, float s, float b) {
    float c = b * s;
    float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = b - c;

    float r, g, bl;
    if (h < 1.0f / 6.0f) {
        r = c;
        g = x;
        bl = 0;
    } else if (h < 2.0f / 6.0f) {
        r = x;
        g = c;
        bl = 0;
    } else if (h < 3.0f / 6.0f) {
        r = 0;
        g = c;
        bl = x;
    } else if (h < 4.0f / 6.0f) {
        r = 0;
        g = x;
        bl = c;
    } else if (h < 5.0f / 6.0f) {
        r = x;
        g = 0;
        bl = c;
    } else {
        r = c;
        g = 0;
        bl = x;
    }

    return RGB{(r + m) * 255.0f, (g + m) * 255.0f, (bl + m) * 255.0f};
}

// Flowing rainbow color for element `index`. 8s full cycle; each index is
// offset (100ms) so a row/character trails the previous one. `timeMs` is a
// wall-clock millisecond count (std::chrono epoch ms).
inline ImU32 GetRainbowColor(int index, float saturation, long long timeMs) {
    // Modulo keeps the number small and avoids float precision issues.
    const float cycleMs = 8000.0f;  // 8 second full rainbow cycle
    float timeInCycle = std::fmod((float)(timeMs % 8000), cycleMs);
    float offset = index * 100.0f;  // per-element offset for the flowing effect
    float hue = std::fmod((timeInCycle + offset) / cycleMs, 1.0f);
    RGB rgb = FromHSB(hue, saturation, 0.9f);
    return IM_COL32((int)rgb.r, (int)rgb.g, (int)rgb.b, 255);
}

// Scale the alpha channel of a packed color by f (0..1), clamped.
inline ImU32 ColScaleAlpha(ImU32 c, float f) {
    int a = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * f);
    a = a < 0 ? 0 : (a > 255 ? 255 : a);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

// Add `add` to each RGB channel (clamped), keeping alpha untouched.
inline ImU32 ColBrighten(ImU32 c, int add) {
    auto cl = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    int r = (c >> IM_COL32_R_SHIFT) & 0xFF;
    int g = (c >> IM_COL32_G_SHIFT) & 0xFF;
    int b = (c >> IM_COL32_B_SHIFT) & 0xFF;
    int a = (c >> IM_COL32_A_SHIFT) & 0xFF;
    return IM_COL32(cl(r + add), cl(g + add), cl(b + add), a);
}

}  // namespace hud
