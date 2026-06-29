#include "window.h"
#include "../app/gui.h"

#include <float.h>

#include "imgui.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI app_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    auto* surface = reinterpret_cast<AppSurface*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (surface && surface->imgui_win32_route)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
            return true;
    }

    switch (msg)
    {
    case WM_NCHITTEST:
    {
        POINT pt = { (int)(short)LOWORD(lparam), (int)(short)HIWORD(lparam) };
        ::ScreenToClient(hwnd, &pt);
        if (gui::is_point_inside((float)pt.x, (float)pt.y))
        {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
            ::PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (surface && wparam != SIZE_MINIMIZED)
        {
            surface->pending_w = (unsigned)LOWORD(lparam);
            surface->pending_h = (unsigned)HIWORD(lparam);
        }
        return 0;
    case WM_DESTROY:
        if (surface && surface->quit_app_on_destroy)
            ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool app_surface_consume_resize(AppSurface& surface, unsigned& width, unsigned& height)
{
    if (surface.pending_w == 0 || surface.pending_h == 0) return false;
    width  = surface.pending_w;
    height = surface.pending_h;
    surface.pending_w = surface.pending_h = 0;
    return true;
}

void app_update_io_for_hwnd(HWND hwnd, ImGuiIO& io)
{
    RECT client = {};
    ::GetClientRect(hwnd, &client);
    io.DisplaySize = ImVec2(
        (float)max(client.right  - client.left, 1),
        (float)max(client.bottom - client.top,  1));

    POINT cursor = {};
    ::GetCursorPos(&cursor);

    HWND hovered = ::WindowFromPoint(cursor);
    const bool over = (hovered == hwnd || ::IsChild(hwnd, hovered));

    if (over)
    {
        POINT local = cursor;
        ::ScreenToClient(hwnd, &local);
        io.AddMousePosEvent((float)local.x, (float)local.y);
    }
    else
    {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
}
