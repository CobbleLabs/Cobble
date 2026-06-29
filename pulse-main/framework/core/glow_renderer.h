#pragma once

#include "imgui.h"
#include "imgui_internal.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace glow_renderer
{
    bool init(ID3D11Device* device);
    void shutdown();

    void queue_rect(const ImRect& rect, float round, float strength, const ImVec4& color);
    void flush(ID3D11DeviceContext* ctx, const ImVec2& display_size);
    void clear_queue();
}
