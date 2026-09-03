#pragma once

// ============================================================================
// ArrayList - Active modules HUD display
// ============================================================================

namespace ArrayListRenderer {
// Anchor position for the ArrayList
enum class Anchor { TopRight = 0, TopLeft = 1, BottomRight = 2, BottomLeft = 3 };

inline bool enabled = false;
// Clean default look: geometric sans (Inter Regular), one accent colour, colour
// bar, no boxy background (crisp outlined text). Rainbow/background stay
// available but off by default.
inline bool showBackground = false;
inline bool sortByLength = true;
inline bool rainbow = false;
inline int rainbowType = 0;  // 0 Flow, 1 Solid, 2 Fast, 3 Wide
inline bool colorBar = true;
inline bool showInfo = true;   // Show module info (CPS, speed, etc.)
// Stable hud_fonts ID, not an atlas slot. 0/1 are Inter Regular/Bold, which is
// what keeps the old two-entry values ("arrayListFontStyle") meaning the same
// faces now that system families are selectable too.
inline int fontStyle = 0;
inline float rainbowSaturation = 0.7f;
inline float scale = 1.0f;
// Follow the global accent (default) or use `color`. This used to be a -1
// sentinel inside `color`, which drew the settings swatch black and — because
// ui::color() edits four floats — wrote past the end of a three-float array.
inline bool useAccent = true;
inline float color[4] = {1.0f, 0.47f, 0.05f, 1.0f};
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
