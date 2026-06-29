#pragma once

namespace ui_scale
{
    inline float factor = 1.f;

    // User-controllable global scale multiplier (driven by the in-GUI scale slider).
    // Applied on top of the base device scale so it grows the whole interface — sizes AND fonts.
    inline float user_scale = 1.f;

    inline void set_percent(float percent)
    {
        factor = percent * 0.01f;
    }

    inline float s(float design_px)
    {
        return design_px * factor;
    }

    inline float px(float design_px)
    {
        return design_px * factor;
    }
}
