#pragma once

// ============================================================================
// Watermark - "Cobble" client tag + version + FPS, drawn in the HUD corner.
// Free-positioned and draggable, same pattern as the ArrayList.
// ============================================================================

namespace Watermark {
inline bool enabled = true;
inline bool rainbow = true;  // flowing gradient on "Cobble" (vs flat accent color)
inline float scale = 1.0f;

// Free positioning: absolute top-left of the box, in screen pixels.
// -1 means "not placed yet" -> default to the top-left corner.
inline float posX = -1.0f;
inline float posY = -1.0f;

// Drag state (same as the ArrayList): grab the box with the menu open, it
// follows the mouse while the menu is visually hidden, drop to place it.
inline bool dragging = false;
inline float dragOffsetX = 0.0f;
inline float dragOffsetY = 0.0f;
// Placement mode started from the menu button: box follows the cursor until
// a single click drops it.
inline bool placing = false;

void render();
}  // namespace Watermark
