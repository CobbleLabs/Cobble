#pragma once

// ============================================================================
// Watermark - "Cobble" client tag + version + FPS, drawn in the HUD corner.
// Free-positioned and draggable, same pattern as the ArrayList.
// ============================================================================

namespace Watermark {
inline bool enabled = true;
inline bool rainbow = true;   // flowing gradient on "Cobble" (vs flat white)
inline int rainbowType = 0;   // 0 Flow, 1 Solid, 2 Fast, 3 Wide
inline float scale = 1.0f;
// Stable hud_fonts ID (see ui/core/hud_fonts.h). 1 = Inter Bold, which also
// selects the built-in Bold/Regular weight pairing for the meta text; any other
// family is used for the whole watermark.
inline int fontStyle = 1;

// Smoothed FPS readout: eases toward RenderData::gameFps so the number
// doesn't jitter frame-to-frame. Seeded lazily on the first render.
inline float fpsSmooth = 0.0f;

// Free positioning: absolute top-left of the box, in screen pixels.
// -1 means "not placed yet" -> default to the top-left corner.
inline float posX = -1.0f;
inline float posY = -1.0f;

// Drag state (same as the ArrayList): grab the box with the menu open, it
// follows the mouse while the menu is visually hidden, drop to place it.
inline bool dragging = false;
inline float dragOffsetX = 0.0f;
inline float dragOffsetY = 0.0f;

void render();
}  // namespace Watermark
