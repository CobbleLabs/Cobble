#pragma once

namespace layout
{
    // ----- Shell / panel -----
    inline constexpr float panel_design = 400.f; // design width used for ui scaling
    inline constexpr float panel_w      = 400.f;
    inline constexpr float panel_h      = 600.f;
    inline constexpr float shell_round  = 16.f;

    // ----- Outer content padding (distance from panel borders) -----
    inline constexpr float content_pad  = 24.f;

    // ----- Top image (slideshow) -----
    // The image sits with `content_pad` padding from the top, left and right
    // panel borders. So a custom image should be authored at this size (or the
    // same aspect ratio) for a pixel-perfect fit:
    //     width  = panel_w - content_pad * 2 = 352 px
    //     height = image_h                   = 180 px
    inline constexpr float image_w      = panel_w - content_pad * 2.f; // 352
    inline constexpr float image_h      = 180.f;
    inline constexpr float image_round  = 12.f;

    // Slideshow dot indicators
    inline constexpr float dot_size     = 7.f;
    inline constexpr float dot_gap      = 8.f;
    inline constexpr float dot_area_h   = 22.f;   // band under the image for dots
    inline constexpr float slide_seconds = 3.0f;  // time each image is shown
    inline constexpr float fade_seconds  = 0.6f;  // cross-fade duration

    // ----- Form -----
    inline constexpr float field_h      = 44.f;
    inline constexpr float field_round  = 10.f;
    inline constexpr float field_gap    = 14.f;   // gap between stacked fields
    inline constexpr float field_icon_w = 40.f;   // leading icon column width
    inline constexpr float label_gap    = 7.f;    // gap between label and its field
    inline constexpr float section_gap  = 18.f;   // gap above the form block

    inline constexpr float button_h     = 46.f;
    inline constexpr float button_round = 10.f;
    inline constexpr float button_gap   = 18.f;   // gap above sign-in button

    inline constexpr float remember_gap = 16.f;   // gap above remember-me row
    inline constexpr float checkbox_size  = 18.f;
    inline constexpr float checkbox_round  = 4.f;
    inline constexpr float checkbox_label_gap = 9.f;

    inline constexpr float footer_pad   = 18.f;   // copyright distance from bottom

    // ----- Expanded app (after sign in) -----
    inline constexpr float sidebar_w     = 64.f;   // thin icon-only sidebar
    inline constexpr float sidebar_item  = 40.f;   // clickable icon button size
    inline constexpr float sidebar_gap   = 8.f;    // gap between sidebar items

    inline constexpr const char* main_window_id = "##imgui_portfolio_15";
    inline constexpr const char* app_name       = "Crucial";
}
