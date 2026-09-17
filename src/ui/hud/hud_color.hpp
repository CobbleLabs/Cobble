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

// Rainbow hue (0..1) for element `index` at `timeMs`, with a style `type`:
//   0 Flow (default), 1 Solid (no per-element offset), 2 Fast, 3 Wide (slow+spread).
inline float RainbowHue(int index, long long timeMs, int type) {
    float cycle, spread;
    switch (type) {
        case 1:
            cycle = 6000.0f;
            spread = 0.0f;
            break;
        case 2:
            cycle = 3500.0f;
            spread = 70.0f;
            break;
        case 3:
            cycle = 11000.0f;
            spread = 165.0f;
            break;
        default:
            cycle = 8000.0f;
            spread = 100.0f;
            break;
    }
    float t = std::fmod(static_cast<float>(timeMs % static_cast<long long>(cycle)), cycle);
    return std::fmod((t + index * spread) / cycle, 1.0f);
}

// Flowing rainbow color with a selectable `type`. Type 3 softens saturation a bit.
inline ImU32 GetRainbowColorEx(int index, float saturation, long long timeMs, int type) {
    float sat = (type == 3) ? saturation * 0.72f : saturation;
    RGB rgb = FromHSB(RainbowHue(index, timeMs, type), sat, 0.9f);
    return IM_COL32((int)rgb.r, (int)rgb.g, (int)rgb.b, 255);
}

// Scale the alpha channel of a packed color by f (0..1), clamped.
inline ImU32 ColScaleAlpha(ImU32 c, float f) {
    int a = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * f);
    a = a < 0 ? 0 : (a > 255 ? 255 : a);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

}  // namespace hud
