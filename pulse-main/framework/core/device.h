#pragma once

#include <windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain1;
struct IDCompositionDevice;
struct IDCompositionTarget;
struct IDCompositionVisual;
struct ImDrawData;

struct GfxShared
{
    ID3D11Device*        device  = nullptr;
    ID3D11DeviceContext* context = nullptr;
};

struct AppSurface
{
    HWND                    hwnd                = nullptr;
    bool                    imgui_win32_route   = true;
    bool                    quit_app_on_destroy = true;
    IDXGISwapChain1*        swap_chain          = nullptr;
    ID3D11RenderTargetView* render_target       = nullptr;
    IDCompositionDevice*    dcomp_device        = nullptr;
    IDCompositionTarget*    dcomp_target        = nullptr;
    IDCompositionVisual*    dcomp_visual        = nullptr;
    unsigned                pending_w           = 0;
    unsigned                pending_h           = 0;
};

bool gfx_create_shared(GfxShared& out);
void gfx_destroy_shared(GfxShared& shared);

bool gfx_create_surface(GfxShared& shared, AppSurface& surface, HWND hwnd);
void gfx_destroy_surface(AppSurface& surface);
void gfx_resize_surface(GfxShared& shared, AppSurface& surface, unsigned width, unsigned height);
void gfx_render_surface(GfxShared& shared, AppSurface& surface, ImDrawData* draw_data);
