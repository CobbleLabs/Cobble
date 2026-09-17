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
inline bool showInfo = true;  // Show module info (CPS, speed, etc.)
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
// Top-left by default. Minecraft draws the sidebar scoreboard against the RIGHT
// edge, vertically centred, so a top-right list grows straight down into it as
// soon as a handful of modules are on. The left edge is empty in vanilla (the
// hotbar/health are bottom-centre), so nothing collides there. A config saved
// with the old default keeps its own value -- switching the anchor now actually
// moves the block, which it did not before.
inline int anchor = 1;  // 0=TopRight, 1=TopLeft, 2=BottomRight, 3=BottomLeft

// Free positioning, stored as the screen FRACTION of the anchored corner rather
// than an absolute top-left pixel (see hud/hud_place.hpp). Absolute pixels broke
// in two ways: a config saved at one resolution -- or written during the first
// frames after injecting into an already-fullscreen game -- described a screen
// that no longer existed, and there was no way to tell a stale saved position
// from a deliberate one, because auto-placement wrote its own computed pixels
// into the same fields that "-1 means auto" was encoded in.
//
// `userPlaced` is that missing bit: only an actual drag sets it. While it is
// false the block is parked in the `anchor` corner every frame.
inline float relX = 0.0f;
inline float relY = 0.0f;
inline bool userPlaced = false;
// Drag state (same pattern as DroppedInfo): grab the block with the menu open,
// it follows the mouse while the menu is visually hidden, drop to place it.
inline bool dragging = false;
inline float dragOffsetX = 0.0f;
inline float dragOffsetY = 0.0f;

void render();
bool hasActive();
}  // namespace ArrayListRenderer
