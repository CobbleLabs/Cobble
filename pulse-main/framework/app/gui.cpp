#include "gui.h"

#include "helpers/fonts.h"
#include "helpers/texture_cache.h"
#include "helpers/window_drag.h"
#include "pages/main_page.h"
#include "pages/login_page.h"
#include "core/auth_system.h"
#include "theme/colors.h"
#include "theme/layout.h"
#include "theme/ui_scale.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <windows.h>
#include <cmath>
#include <vector>

namespace
{
    main_page::state g_main;
    login_page::state g_login;
    ImGuiStyle g_base_style = {};
    bool g_base_style_saved = false;

    void apply_style()
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding   = 0.f;
        s.WindowBorderSize = 0.f;
        s.WindowPadding    = { 0.f, 0.f };
        s.AntiAliasedLines = true;
        s.AntiAliasedFill  = true;
        s.ChildBorderSize  = 0.f;
        s.FrameBorderSize  = 0.f;
        s.ScrollbarSize    = 0.f;

        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]       = { 0.f, 0.f, 0.f, 0.f };
        c[ImGuiCol_ChildBg]        = colors::bg;
        c[ImGuiCol_PopupBg]        = colors::popup;
        c[ImGuiCol_Text]           = colors::text;
        c[ImGuiCol_TextDisabled]   = colors::text_muted;
        c[ImGuiCol_Border]         = colors::divider;
        c[ImGuiCol_FrameBg]        = colors::widget;
        c[ImGuiCol_FrameBgHovered] = colors::widget; // Se queda en el mismo color del fondo
        c[ImGuiCol_FrameBgActive]  = colors::widget;
        c[ImGuiCol_Header]         = colors::surface;
        c[ImGuiCol_HeaderHovered]  = colors::surface; // Se queda en el mismo color del fondo
        c[ImGuiCol_HeaderActive]   = colors::surface;
        c[ImGuiCol_Button]         = colors::control;
        c[ImGuiCol_ButtonHovered]  = colors::control; // Se queda en el color del fondo
        c[ImGuiCol_ButtonActive]   = colors::control;

        if (!g_base_style_saved)
        {
            g_base_style = s;
            g_base_style_saved = true;
        }
    }

    ImVec2 panel_dims()
    {
        return { layout::panel_w, layout::panel_h };
    }
}

namespace gui
{
    void initialize(ID3D11Device* device)
    {
        colors::sync_accent_hover();
        fonts::init();
        apply_style();
        texture_cache::init(device);
        auth::initialize();
    }

    void shutdown()
    {
        fonts::shutdown();
        texture_cache::shutdown();
    }

    void prepare_frame()
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ui_scale::factor = (layout::panel_w / layout::panel_design) * ui_scale::user_scale;
        fonts::sync_ui_scale(ui_scale::factor);
        ImGui::GetStyle() = g_base_style;
        ImGui::GetStyle().ScaleAllSizes(ui_scale::factor);
        ImGui::GetIO().FontGlobalScale = 1.f;
    }

    void render(HWND hwnd)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;

        const float w = ui_scale::s(500.f);
        const float h = ui_scale::s(500.f);

        // Keep the panel centered on screen.
        const ImVec2 panel_pos = { (display.x - w) * 0.5f, (display.y - h) * 0.5f };

        const ImRect panel_bb(panel_pos, panel_pos + ImVec2(w, h));
        const float panel_round = ui_scale::s(layout::shell_round);

        ImGui::SetNextWindowPos({ 0.f, 0.f }, ImGuiCond_Always);
        ImGui::SetNextWindowSize(display, ImGuiCond_Always);

        ImGui::Begin(layout::main_window_id, nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Draw the grey GUI backplate window under the tab and settings box
        const ImVec2 bp_min = panel_bb.Min + ImVec2(ui_scale::s(28.f), ui_scale::s(40.f));
        const ImVec2 bp_max = panel_bb.Min + ImVec2(ui_scale::s(472.f), ui_scale::s(460.f));
        dl->AddRectFilled(bp_min, bp_max, ImGui::GetColorU32(colors::bg), panel_round);

        // Draw a thin crimson border around the backplate
        dl->AddRect(bp_min, bp_max, ImGui::GetColorU32(colors::accent), panel_round, 0, 1.2f);

        bool blocks_drag = false;
        if (!auth::is_authenticated())
        {
            login_page::draw(panel_bb, g_login, blocks_drag);
            if (g_login.exit_pressed)
                ::PostQuitMessage(0);
        }
        else
        {
            main_page::draw(panel_bb, g_main, blocks_drag);
            if (g_main.exit_pressed)
                ::PostQuitMessage(0);
        }

        // Allow window dragging from anywhere on the panel, as long as it's not blocked
        window_drag::handle(hwnd, panel_bb, blocks_drag);

        ImGui::End();
    }

    bool is_point_inside(float x, float y)
    {
        if (!ImGui::GetCurrentContext()) return false;

        const ImVec2 display = ImGui::GetIO().DisplaySize;

        const float w = ui_scale::s(500.f);
        const float h = ui_scale::s(500.f);

        const ImVec2 panel_pos = { (display.x - w) * 0.5f, (display.y - h) * 0.5f };
        const ImRect panel_bb(panel_pos, panel_pos + ImVec2(w, h));

        const ImVec2 bp_min = panel_bb.Min + ImVec2(ui_scale::s(28.f), ui_scale::s(40.f));
        const ImVec2 bp_max = panel_bb.Min + ImVec2(ui_scale::s(472.f), ui_scale::s(460.f));

        if (x >= bp_min.x && x <= bp_max.x && y >= bp_min.y && y <= bp_max.y)
            return true;

        if (g_main.open_popup == 11)
        {
            const float popup_w = ui_scale::s(232.f);
            if (x >= bp_max.x && x <= bp_max.x + popup_w && y >= bp_min.y && y <= bp_max.y)
                return true;
        }

        return false;
    }
}
