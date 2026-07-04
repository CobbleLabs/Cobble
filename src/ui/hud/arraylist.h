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

// Free positioning: absolute top-left of the whole block, in screen pixels.
// -1 means "not placed yet" -> derive the start position from `anchor`.
inline float posX = -1.0f;
inline float posY = -1.0f;
// Drag state (same pattern as DroppedInfo): grab the block with the menu open,
// it follows the mouse while the menu is visually hidden, drop to place it.
inline bool dragging = false;
inline float dragOffsetX = 0.0f;
inline float dragOffsetY = 0.0f;

void render();
bool hasActive();
}  // namespace ArrayListRenderer
