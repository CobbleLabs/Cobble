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

// When true the accent cycles a rainbow (rainbowType selects the style) instead
// of the fixed accentColor; every menu accent that uses GetSchemeColor follows it.
inline bool accentRainbow = false;
inline int rainbowType = 0;  // 0 Flow, 1 Solid, 2 Fast, 3 Wide

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

}  // namespace theme
}  // namespace ui
