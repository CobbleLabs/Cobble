#pragma once

// HUD badge: "Cobble v1.7.0 | 120 FPS", draggable with the menu open.
namespace Watermark {

inline bool enabled = true;
inline float scale = 1.0f;
inline bool showBackground = true;
inline float backgroundColor[4] = {0.0f, 0.0f, 0.0f, 0.53f};
inline bool showAccentBar = true;
inline bool rainbowName = true;

// Top-left corner as a screen fraction (see hud_place.hpp), so the badge keeps
// its relative spot at any resolution. Ignored until the user drags it.
inline float relX = 0.0f;
inline float relY = 0.0f;
inline bool userPlaced = false;

inline bool dragging = false;
inline float dragOffsetX = 0.0f;
inline float dragOffsetY = 0.0f;

// Last drawn box in screen pixels (0 width = not drawn this frame). The
// ArrayList's auto-placement reads it to avoid landing on the watermark.
inline float lastX = 0.0f;
inline float lastY = 0.0f;
inline float lastW = 0.0f;
inline float lastH = 0.0f;

void render();
}  // namespace Watermark
