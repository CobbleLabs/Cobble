#pragma once

// ============================================================================
// ArrayList - Active modules HUD display
// ============================================================================

namespace ArrayListRenderer {
// Anchor position for the ArrayList
enum class Anchor { TopRight = 0, TopLeft = 1, BottomRight = 2, BottomLeft = 3 };

inline bool enabled = false;
inline bool showBackground = true;
inline bool sortByLength = true;
inline bool rainbow = true;
inline bool colorBar = true;
inline bool showInfo = true;  // Show module info (CPS, speed, etc.)
inline float rainbowSaturation = 0.7f;
inline float scale = 1.0f;
inline float color[3] = {-1.0f, -1.0f, -1.0f};  // Use accent color by default (-1 = accent)
inline float yOffset = 5.0f;
inline int anchor = 0;  // 0=TopRight, 1=TopLeft, 2=BottomRight, 3=BottomLeft

void render();
}  // namespace ArrayListRenderer
