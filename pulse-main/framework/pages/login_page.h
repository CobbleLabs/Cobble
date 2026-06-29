#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include <string>

namespace login_page
{
    struct state
    {
        char  input_key[128] = "";

        std::string status_msg = "";
        ImVec4      status_color = { 0.7f, 0.7f, 0.7f, 1.f };

        bool  copied_hwid = false;
        bool  remember_me = true;
        bool  exit_pressed = false;
        bool  is_logging_in = false;
    };

    void draw(const ImRect& panel, state& s, bool& blocks_window_drag);
}
