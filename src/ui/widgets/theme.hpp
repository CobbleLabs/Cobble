#pragma once

// ============================================================================
// Cobble UI Theme - Colors and Styling
// ============================================================================

#include "../../../infra/ext/imgui/imgui.h"

namespace ui {
namespace theme {

// Scheme/Accent color (user configurable)
inline float accentColor[4] = {0.843f, 0.376f, 0.078f, 1.0f};  // Orange default

// Get accent color as ImU32 with optional alpha multiplier
ImU32 getAccent(float alpha = 1.0f);

// Apply the dark theme to ImGui
void apply();

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
