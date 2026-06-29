#include "core/device.h"
#include "core/window.h"
#include "app/gui.h"
#include "helpers/screenshot.h"
#include "core/optimizer.h"
#include "core/anti_tamper.h"

#include <windows.h>

#include "theme/layout.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

int main(int, char**)
{
    anti_tamper::initialize();
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, app_wnd_proc, 0L, 0L,
        GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
        L"ImguiPortfolio14Shell", nullptr };
    ::RegisterClassExW(&wc);

    GfxShared shared = {};
    if (!gfx_create_shared(shared))
    {
        gfx_destroy_shared(shared);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    AppSurface surface = {};

    const int win_w = ::GetSystemMetrics(SM_CXSCREEN);
    const int win_h = ::GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = ::CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"Imgui Portifolio #15", WS_POPUP,
        0, 0, win_w, win_h,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (hwnd)
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&surface));

    if (!hwnd || !gfx_create_surface(shared, surface, hwnd))
    {
        gfx_destroy_surface(surface);
        gfx_destroy_shared(shared);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(shared.device, shared.context);
    gui::initialize(shared.device);
    optimizer::detect_connection_type();

    surface.imgui_win32_route   = true;
    surface.quit_app_on_destroy = true;

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        unsigned rw = 0, rh = 0;
        if (app_surface_consume_resize(surface, rw, rh))
            gfx_resize_surface(shared, surface, rw, rh);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        app_update_io_for_hwnd(hwnd, io);
        screenshot::begin_frame();
        gui::prepare_frame();
        ImGui::NewFrame();

        gui::render(hwnd);

        ImGui::Render();
        gfx_render_surface(shared, surface, ImGui::GetDrawData());
    }

    gui::shutdown();
    optimizer::cleanup_on_exit();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    gfx_destroy_surface(surface);
    gfx_destroy_shared(shared);
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
