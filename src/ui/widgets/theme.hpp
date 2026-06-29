#pragma once

// ============================================================================
// Cobble UI Theme - Colors and Styling
// ============================================================================

#include "../../../infra/ext/imgui/imgui.h"

namespace ui {
namespace theme {

// Scheme/Accent color (user configurable)
inline float accentColor[4] = {
    0.9412f,  // R = 240
    0.4667f,  // G = 119
    0.0549f,  // B = 14
    1.0f      // A = 255
};

// Get accent color as ImU32 with optional alpha multiplier
ImU32 getAccent(float alpha = 1.0f);

// Apply the dark theme to ImGui
void apply();

// Base corner-rounding radii (unscaled). Single source of truth for menu
// softness; scaled through ui::s exactly once in theme::apply() (style fields)
// and at draw time for the Module_Card. See design section 3.
namespace rounding {
inline constexpr float Window = 10.f;
inline constexpr float Child = 8.f;
inline constexpr float Frame = 6.f;
inline constexpr float Grab = 6.f;
inline constexpr float Popup = 10.f;
inline constexpr float Scrollbar = 6.f;
inline constexpr float ModuleCard = 8.f;
}  // namespace rounding

// Color constants for reference (dark theme)
namespace colors {
constexpr ImU32 TEXT_WHITE = IM_COL32(255, 255, 255, 255);
constexpr ImU32 TEXT_GRAY = IM_COL32(100, 100, 100, 255);
constexpr ImU32 BG_DARK = IM_COL32(16, 16, 16, 255);
constexpr ImU32 BG_CARD = IM_COL32(20, 20, 20, 255);
constexpr ImU32 BG_HEADER = IM_COL32(26, 26, 26, 255);
constexpr ImU32 BORDER = IM_COL32(255, 255, 255, 15);
}  // namespace colors

}  // namespace theme
}  // namespace ui
