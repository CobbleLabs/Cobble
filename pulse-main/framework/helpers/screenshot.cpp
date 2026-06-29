#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <cstring>
#include <vector>

#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "screenshot.h"
#include "imgui_internal.h"

namespace
{
    screenshot::state g_state;
    bool g_pending = false;

    void bgra_to_rgba(uint8_t* dst, const uint8_t* src, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            dst[i * 4 + 0] = src[i * 4 + 2];
            dst[i * 4 + 1] = src[i * 4 + 1];
            dst[i * 4 + 2] = src[i * 4 + 0];
            dst[i * 4 + 3] = src[i * 4 + 3];
        }
    }

    bool write_png_next_to_exe(const std::vector<uint8_t>& rgba, int w, int h)
    {
        wchar_t exe_dir[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exe_dir, MAX_PATH) == 0)
            return false;

        wchar_t* slash = wcsrchr(exe_dir, L'\\');
        if (!slash)
            return false;
        *(slash + 1) = L'\0';

        wchar_t screenshots_dir[MAX_PATH]{};
        swprintf_s(screenshots_dir, L"%sscreenshots", exe_dir);
        CreateDirectoryW(screenshots_dir, nullptr);

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t path[MAX_PATH]{};
        swprintf_s(path, L"%s\\portfolio9_%04d%02d%02d_%02d%02d%02d.png",
            screenshots_dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        char path_a[MAX_PATH]{};
        WideCharToMultiByte(CP_UTF8, 0, path, -1, path_a, MAX_PATH, nullptr, nullptr);
        return stbi_write_png(path_a, w, h, 4, rgba.data(), w * 4) != 0;
    }
}

namespace screenshot
{
    state& get()
    {
        return g_state;
    }

    void begin_frame()
    {
        g_state.bounds_valid = false;
        if (g_pending)
        {
            g_state.capture_frame = true;
            g_pending             = false;
        }
    }

    void request()
    {
        g_pending = true;
    }

    void set_bounds(ImVec2 min, ImVec2 max)
    {
        g_state.bounds_min   = min;
        g_state.bounds_max   = max;
        g_state.bounds_valid = true;
    }

    bool capture_and_save(ID3D11Device* device, ID3D11DeviceContext* ctx,
        ID3D11RenderTargetView* rtv, ImVec2 bounds_min, ImVec2 bounds_max)
    {
        if (!device || !ctx || !rtv)
            return false;

        ID3D11Resource* resource = nullptr;
        rtv->GetResource(&resource);
        if (!resource)
            return false;

        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&back_buffer))))
        {
            resource->Release();
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        back_buffer->GetDesc(&desc);

        int x = (int)ImFloor(bounds_min.x);
        int y = (int)ImFloor(bounds_min.y);
        int w = (int)ImCeil(bounds_max.x - bounds_min.x);
        int h = (int)ImCeil(bounds_max.y - bounds_min.y);

        x = ImClamp(x, 0, (int)desc.Width - 1);
        y = ImClamp(y, 0, (int)desc.Height - 1);
        w = ImClamp(w, 1, (int)desc.Width - x);
        h = ImClamp(h, 1, (int)desc.Height - y);

        D3D11_TEXTURE2D_DESC staging_desc = desc;
        staging_desc.Width              = w;
        staging_desc.Height             = h;
        staging_desc.BindFlags          = 0;
        staging_desc.MiscFlags          = 0;
        staging_desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
        staging_desc.Usage              = D3D11_USAGE_STAGING;

        ID3D11Texture2D* staging = nullptr;
        if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging)))
        {
            back_buffer->Release();
            resource->Release();
            return false;
        }

        D3D11_BOX box{};
        box.left   = x;
        box.top    = y;
        box.right  = x + w;
        box.bottom = y + h;
        box.front  = 0;
        box.back   = 1;
        ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, back_buffer, 0, &box);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
        {
            staging->Release();
            back_buffer->Release();
            resource->Release();
            return false;
        }

        std::vector<uint8_t> pixels((size_t)w * (size_t)h * 4);
        const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
        const int dst_stride = w * 4;
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* src_row = src + row * mapped.RowPitch;
            bgra_to_rgba(pixels.data() + row * dst_stride, src_row, w);
        }

        ctx->Unmap(staging, 0);
        staging->Release();
        back_buffer->Release();
        resource->Release();

        return write_png_next_to_exe(pixels, w, h);
    }
}
