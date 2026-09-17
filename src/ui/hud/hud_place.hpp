#pragma once

// ============================================================================
// hud::place - resolution-independent placement for draggable HUD boxes.
//
// A movable element stores WHERE ITS ANCHOR CORNER SITS as a fraction of the
// screen, not an absolute top-left pixel, and resolves that to pixels every
// frame against the live screen size. Two bugs fall out of the old
// absolute-pixel scheme, and both disappear here:
//
//   * Injecting while the game is already fullscreen -- or simply loading a
//     config saved at another resolution -- applied pixels measured against a
//     different screen. There was no way to tell a stale saved position from a
//     deliberate one, because "auto" was encoded as a negative coordinate and
//     auto-placement wrote its own computed pixels back into the same fields,
//     so the next launch read them as a user placement.
//   * The anchored edge of a box that grows and shrinks (ArrayList rows folding
//     in and out) had to be tracked with a remembered "previous right edge",
//     which was reset by every resolution change and every restart.
//
// Elements pair these helpers with an explicit `userPlaced` flag, so automatic
// placement never masquerades as a drag.
// ============================================================================

#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/player/render_data.h"

namespace hud {

// Live screen size in pixels. ImGui's DisplaySize is what the overlay actually
// laid out this frame; RenderData is the fallback for the first frames after
// injection, before a frame has been set up. Returns false while neither is
// usable, which is the signal to skip placement for this frame instead of
// clamping every coordinate to zero against a 0x0 screen.
inline bool screenSize(float& w, float& h) {
    const ImGuiIO& io = ImGui::GetIO();
    w = io.DisplaySize.x;
    h = io.DisplaySize.y;
    if (w < 16.0f || h < 16.0f) {
        w = static_cast<float>(RenderData::screenWidth);
        h = static_cast<float>(RenderData::screenHeight);
    }
    return w >= 16.0f && h >= 16.0f;
}

// Fractions -> top-left pixel of a w x h box. `right` / `bottom` name the corner
// the fractions were measured from, so the box keeps that edge planted when it
// resizes.
inline void placeToPixels(bool right, bool bottom, float relX, float relY, float w, float h, float sw, float sh,
                          float& x, float& y) {
    x = relX * sw - (right ? w : 0.0f);
    y = relY * sh - (bottom ? h : 0.0f);
}

// Top-left pixel -> fractions: the exact inverse of placeToPixels. Called after
// a drag or a clamp, so what is stored always matches what was drawn.
inline void placeToFractions(bool right, bool bottom, float x, float y, float w, float h, float sw, float sh,
                             float& relX, float& relY) {
    if (sw > 0.0f)
        relX = (x + (right ? w : 0.0f)) / sw;
    if (sh > 0.0f)
        relY = (y + (bottom ? h : 0.0f)) / sh;
}

}  // namespace hud
