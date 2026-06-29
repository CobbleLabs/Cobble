#pragma once

#include <d3d11.h>
#include "imgui.h"

namespace screenshot
{
    struct state
    {
        bool   capture_frame = false;
        bool   bounds_valid  = false;
        ImVec2 bounds_min    = {};
        ImVec2 bounds_max    = {};
    };

    state& get();

    void begin_frame();
    void request();
    void set_bounds(ImVec2 min, ImVec2 max);

    bool capture_and_save(ID3D11Device* device, ID3D11DeviceContext* ctx,
        ID3D11RenderTargetView* rtv, ImVec2 bounds_min, ImVec2 bounds_max);
}
