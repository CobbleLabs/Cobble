#pragma once

#include "imgui.h"
#include "imgui_internal.h"

struct HWND__;

namespace window_drag
{
    void handle(HWND__* hwnd, const ImRect& drag_rect, bool block_drag = false);
    void handle_panel(ImVec2& panel_pos, const ImRect& drag_rect, bool block_drag = false);
}
