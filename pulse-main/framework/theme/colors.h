#pragma once

#include "imgui.h"

#include <cmath>

namespace colors
{
    inline ImVec4 accent       = { 255.f / 255.f, 60.f / 255.f, 65.f / 255.f, 1.f }; // Neon Red
    inline ImVec4 accent_hover = { 255.f / 255.f, 85.f / 255.f, 90.f / 255.f, 1.f };
    inline ImVec4 accent_purple= { 210.f / 255.f, 40.f / 255.f, 45.f / 255.f, 1.f }; // Darker Red

    // Backdrop: matching Portfolio #1 dark layout
    inline constexpr ImVec4 backdrop_login = { 10.f / 255.f, 10.f / 255.f, 12.f / 255.f, 1.f };
    inline constexpr ImVec4 backdrop_app   = { 10.f / 255.f, 10.f / 255.f, 12.f / 255.f, 1.f };

    inline constexpr ImVec4 bg              = { 10.f / 255.f, 10.f / 255.f, 12.f / 255.f, 1.f }; // Dark background (CLR_BG)
    inline constexpr ImVec4 panel           = { 16.f / 255.f, 16.f / 255.f, 18.f / 255.f, 1.f }; // Dark Panel (CLR_PANEL)
    inline constexpr ImVec4 topbar          = { 16.f / 255.f, 16.f / 255.f, 18.f / 255.f, 1.f };
    inline constexpr ImVec4 widget          = { 25.f / 255.f, 25.f / 255.f, 28.f / 255.f, 1.f }; // Slate Black/Grey (CLR_CTRL_BG)
    inline constexpr ImVec4 widget_hover    = { 35.f / 255.f, 35.f / 255.f, 40.f / 255.f, 1.f }; // Border Grey (CLR_BORDER)
    inline constexpr ImVec4 surface         = { 16.f / 255.f, 16.f / 255.f, 18.f / 255.f, 1.f };
    inline constexpr ImVec4 surface_hover   = { 25.f / 255.f, 25.f / 255.f, 28.f / 255.f, 1.f };
    inline constexpr ImVec4 control         = { 25.f / 255.f, 25.f / 255.f, 28.f / 255.f, 1.f };
    inline constexpr ImVec4 control_hover   = { 35.f / 255.f, 35.f / 255.f, 40.f / 255.f, 1.f };
    inline constexpr ImVec4 popup           = { 12.f / 255.f, 12.f / 255.f, 14.f / 255.f, 1.f }; // Footer dark (CLR_FOOTER)
    inline constexpr ImVec4 module_box      = { 16.f / 255.f, 16.f / 255.f, 18.f / 255.f, 1.f };
    inline constexpr ImVec4 module_box_hover = { 25.f / 255.f, 25.f / 255.f, 28.f / 255.f, 1.f };
    inline constexpr ImVec4 text            = { 1.f, 1.f, 1.f, 1.f };
    inline constexpr ImVec4 text_muted      = { 142.f / 255.f, 146.f / 255.f, 162.f / 255.f, 1.f }; // Muted Label Grey (CLR_MUTED)
    inline constexpr ImVec4 text_secondary  = { 142.f / 255.f, 146.f / 255.f, 162.f / 255.f, 1.f };
    inline constexpr ImVec4 danger          = { 255.f / 255.f, 59.f / 255.f, 63.f / 255.f, 1.f };
    inline constexpr ImVec4 divider         = { 28.f / 255.f, 28.f / 255.f, 32.f / 255.f, 1.f }; // Border color (CLR_BORDER)
    inline constexpr ImVec4 scroll_track     = { 10.f / 255.f, 10.f / 255.f, 12.f / 255.f, 1.f };
    inline constexpr ImVec4 scroll_thumb     = { 25.f / 255.f, 25.f / 255.f, 28.f / 255.f, 1.f };
    inline constexpr ImVec4 dropdown_bg      = { 12.f / 255.f, 12.f / 255.f, 14.f / 255.f, 1.f };

    inline void sync_accent_hover()
    {
        accent_hover = {
            fminf(accent.x * 1.08f + 0.04f, 1.f),
            fminf(accent.y * 1.08f + 0.04f, 1.f),
            fminf(accent.z * 1.08f + 0.04f, 1.f),
            accent.w };
    }
}
