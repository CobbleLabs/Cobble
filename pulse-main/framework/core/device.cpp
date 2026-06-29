#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

#include "device.h"
#include "glow_renderer.h"
#include "../helpers/screenshot.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "../theme/colors.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

bool gfx_create_shared(GfxShared& out)
{
    const UINT create_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level = {};

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_flags,
        levels, 2, D3D11_SDK_VERSION, &out.device, &level, &out.context);

    if (hr == DXGI_ERROR_UNSUPPORTED)
    {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, create_flags,
            levels, 2, D3D11_SDK_VERSION, &out.device, &level, &out.context);
    }

    return SUCCEEDED(hr);
}

void gfx_destroy_shared(GfxShared& shared)
{
    if (shared.context) { shared.context->Release(); shared.context = nullptr; }
    if (shared.device)  { shared.device->Release();  shared.device  = nullptr; }
}

bool gfx_create_surface(GfxShared& shared, AppSurface& surface, HWND hwnd)
{
    surface.hwnd = hwnd;

    RECT rc = {};
    ::GetClientRect(hwnd, &rc);
    const UINT width  = (UINT)max(rc.right  - rc.left, 1);
    const UINT height = (UINT)max(rc.bottom - rc.top,  1);

    IDXGIDevice*   dxgi_device = nullptr;
    IDXGIAdapter*  adapter     = nullptr;
    IDXGIFactory2* factory     = nullptr;

    if (FAILED(shared.device->QueryInterface(IID_PPV_ARGS(&dxgi_device)))) return false;
    if (FAILED(dxgi_device->GetAdapter(&adapter)))                          return false;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))                 return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width       = width;
    sd.Height      = height;
    sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;

    HRESULT hr = factory->CreateSwapChainForComposition(shared.device, &sd, nullptr, &surface.swap_chain);
    if (FAILED(hr)) return false;

    if (FAILED(DCompositionCreateDevice(dxgi_device, IID_PPV_ARGS(&surface.dcomp_device)))) return false;
    if (FAILED(surface.dcomp_device->CreateTargetForHwnd(hwnd, TRUE, &surface.dcomp_target))) return false;
    if (FAILED(surface.dcomp_device->CreateVisual(&surface.dcomp_visual))) return false;

    surface.dcomp_visual->SetContent(surface.swap_chain);
    surface.dcomp_target->SetRoot(surface.dcomp_visual);
    surface.dcomp_device->Commit();

    factory->Release();
    adapter->Release();
    dxgi_device->Release();

    gfx_resize_surface(shared, surface, width, height);
    return true;
}

void gfx_destroy_surface(AppSurface& surface)
{
    if (surface.render_target) { surface.render_target->Release(); surface.render_target = nullptr; }
    if (surface.swap_chain)    { surface.swap_chain->Release();    surface.swap_chain    = nullptr; }
    if (surface.dcomp_visual)  { surface.dcomp_visual->Release();  surface.dcomp_visual  = nullptr; }
    if (surface.dcomp_target)  { surface.dcomp_target->Release();  surface.dcomp_target  = nullptr; }
    if (surface.dcomp_device)  { surface.dcomp_device->Release();  surface.dcomp_device  = nullptr; }
    surface.hwnd = nullptr;
}

void gfx_resize_surface(GfxShared& shared, AppSurface& surface, unsigned width, unsigned height)
{
    if (!surface.swap_chain) return;

    if (surface.render_target)
    {
        surface.render_target->Release();
        surface.render_target = nullptr;
    }

    if (width > 0 && height > 0)
        surface.swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* back_buffer = nullptr;
    surface.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer)
    {
        shared.device->CreateRenderTargetView(back_buffer, nullptr, &surface.render_target);
        back_buffer->Release();
    }
}

void gfx_render_surface(GfxShared& shared, AppSurface& surface, ImDrawData* draw_data)
{
    if (!draw_data || !surface.render_target) return;

    screenshot::state& ss = screenshot::get();
    float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    shared.context->OMSetRenderTargets(1, &surface.render_target, nullptr);
    shared.context->ClearRenderTargetView(surface.render_target, clear);

    glow_renderer::flush(shared.context, draw_data->DisplaySize);
    glow_renderer::clear_queue();

    ImGui_ImplDX11_RenderDrawData(draw_data);

    if (ss.capture_frame && ss.bounds_valid)
    {
        screenshot::capture_and_save(shared.device, shared.context, surface.render_target,
            ss.bounds_min, ss.bounds_max);
        ss.capture_frame = false;
        ss.bounds_valid  = false;
    }

    if (surface.swap_chain)   surface.swap_chain->Present(1, 0);
    if (surface.dcomp_device) surface.dcomp_device->Commit();
}
