#pragma once

#include "imgui.h"
#include "imgui_internal.h"

namespace main_page
{
    struct state
    {
        bool  exit_pressed = false;
        int   active_nav = 0;    // selected sidebar tab

        // Scroll / tab-switch animation
        float content_scroll = 0.f;        // current (animated) vertical scroll offset
        float content_scroll_target = 0.f; // where the scroll is easing toward
        int   prev_nav = 0;                // last tab, to detect tab switches
        float tab_anim = 1.f;              // 0..1 fade/slide-in animation for the active tab

        int   open_popup = 0;

        // --- Optimizer settings (bound to UI controls) ---
        bool  smart_packets = true;
        float hit_reg = 100.f;
        float kb_reduction = 100.f;
        int   connection_type = 0; // 0 = Fiber, 1 = Wi-Fi

        // --- UI config / Color picker state ---
        float pick_h = 0.f, pick_s = 0.f, pick_v = 0.f, pick_a = 1.f;
        int   pick_init_for = 0;
        float ui_opacity = 0.95f;
        float ui_scale = 1.0f;
    };

    void draw(const ImRect& panel, state& s, bool& blocks_window_drag);
}
