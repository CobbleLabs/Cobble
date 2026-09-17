#pragma once

// ============================================================================
// hud::dragBox - shared "grab & drag" behavior for movable HUD elements
// (DroppedInfo, ArrayList, Watermark). One implementation, so the drag logic is
// not copied per element.
//
// Replicates the Dropped Pots manner + style:
//   * With the menu open, hovering the element draws a white outline.
//   * Click-and-hold grabs it; it follows the cursor while the menu is visually
//     hidden (cursor stays free), and drops on release.
// There is no separate "placement" mode and no "Move" button -- you drag the
// element directly on screen.
// ============================================================================

#include "../../../infra/ext/imgui/imgui.h"
#include "../menu/menu.h"

namespace hud {

// Handle drag for a box whose top-left is (x, y) with size (w, h). `pad` expands
// both the hover area and the drawn outline. Updates x/y in place. Call once per
// frame from the element's render(), after computing its size and position.
//
// The drag state lives in the caller (dragging + the grab offset) so each
// element keeps its own persistent state without this helper owning any.
inline void dragBox(float& x, float& y, float w, float h, float pad, bool& dragging, float& offX, float& offY) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw)
        return;

    const ImVec2 m = ImGui::GetMousePos();
    auto outline = [&]() {
        draw->AddRect(ImVec2(x - pad, y - pad), ImVec2(x + w + pad, y + h + pad), IM_COL32(255, 255, 255, 255));
    };

    if (dragging) {
        outline();
        if (ImGui::IsMouseDown(0)) {
            x = m.x - offX;
            y = m.y - offY;
        } else {
            dragging = false;
            Menu::hidden = false;  // restore the menu visuals
        }
        return;
    }

    // Not dragging: only grabbable while the menu is open.
    if (!Menu::isOpen)
        return;

    const bool hover = m.x >= x - pad && m.x <= x + w + pad && m.y >= y - pad && m.y <= y + h + pad;
    if (!hover)
        return;

    outline();
    if (ImGui::IsMouseClicked(0)) {
        dragging = true;
        offX = m.x - x;
        offY = m.y - y;
        Menu::hidden = true;  // hide the menu visuals but keep it "open" (cursor free)
    }
}

}  // namespace hud
