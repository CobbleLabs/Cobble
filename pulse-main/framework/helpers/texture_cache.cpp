#include "texture_cache.h"

#include "stb_image.h"

#include <d3d11.h>

#include <string>
#include <unordered_map>

namespace
{
    struct entry
    {
        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0;
        int h = 0;
    };

    ID3D11Device* g_device = nullptr;
    std::unordered_map<std::string, entry> g_cache;

    void release_entry(entry& e)
    {
        if (e.srv)
        {
            e.srv->Release();
            e.srv = nullptr;
        }
        e.w = e.h = 0;
    }

    bool load_entry(const char* path, entry& out)
    {
        if (!g_device || !path || !path[0])
            return false;

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load(path, &w, &h, &comp, 4);
        if (!pixels)
            return false;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels;
        sub.SysMemPitch = (UINT)(w * 4);

        ID3D11Texture2D* tex = nullptr;
        const HRESULT hr = g_device->CreateTexture2D(&desc, &sub, &tex);
        stbi_image_free(pixels);
        if (FAILED(hr) || !tex)
            return false;

        ID3D11ShaderResourceView* srv = nullptr;
        const HRESULT hr2 = g_device->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        if (FAILED(hr2) || !srv)
            return false;

        out.srv = srv;
        out.w = w;
        out.h = h;
        return true;
    }
}

namespace texture_cache
{
    void init(ID3D11Device* device)
    {
        g_device = device;
    }

    void shutdown()
    {
        for (auto& pair : g_cache)
            release_entry(pair.second);
        g_cache.clear();
        g_device = nullptr;
    }

    void invalidate(const char* path)
    {
        if (!path)
            return;
        auto it = g_cache.find(path);
        if (it != g_cache.end())
        {
            release_entry(it->second);
            g_cache.erase(it);
        }
    }

    ImTextureID get(const char* path)
    {
        if (!path || !path[0])
            return (ImTextureID)0;

        auto it = g_cache.find(path);
        if (it != g_cache.end() && it->second.srv)
            return (ImTextureID)it->second.srv;

        entry e = {};
        if (!load_entry(path, e))
            return (ImTextureID)0;

        g_cache[path] = e;
        return (ImTextureID)e.srv;
    }

    ImVec2 get_size(const char* path)
    {
        if (!path || !path[0])
            return { 0.f, 0.f };
        get(path);
        auto it = g_cache.find(path);
        if (it == g_cache.end())
            return { 0.f, 0.f };
        return { (float)it->second.w, (float)it->second.h };
    }
}
