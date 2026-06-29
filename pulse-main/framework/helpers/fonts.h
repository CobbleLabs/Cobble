#pragma once

#include "imgui.h"

#include <cmath>

namespace fonts
{
    void init();
    void shutdown();

    void  sync_ui_scale(float factor);
    float design_to_pixel(float design_size);
    float size(ImFont* font);

    inline float snap(float v) { return floorf(v + 0.5f); }
    inline ImVec2 snap(ImVec2 v) { return { snap(v.x), snap(v.y) }; }

    ImFont* inter(float design_size, bool bold = false);
    ImFont* icon(float design_size);
    ImFont* logo(float design_size);
}
