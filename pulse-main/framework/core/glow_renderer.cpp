#include "glow_renderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    struct GlowCB
    {
        float rect[4];
        float round;
        float intensity;
        float softness;
        float pad0;
        float color[4];
    };

    struct GlowRequest
    {
        ImRect  rect;
        float   round;
        float   strength;
        ImVec4  color;
    };

    static const char* k_vs_hlsl = R"(
        cbuffer CB : register(b0)
        {
            float4 rect;
            float2 display;
            float2 pad;
        };
        struct VSIn { float2 pos : POSITION; };
        struct VSOut { float4 pos : SV_POSITION; float2 pixel : TEXCOORD0; };
        VSOut main(VSIn input)
        {
            float2 p = lerp(rect.xy, rect.zw, input.pos);
            float2 ndc;
            ndc.x = (p.x / display.x) * 2.0 - 1.0;
            ndc.y = 1.0 - (p.y / display.y) * 2.0;
            VSOut o;
            o.pos = float4(ndc, 0.0, 1.0);
            o.pixel = p;
            return o;
        }
    )";

    static const char* k_ps_hlsl = R"(
        cbuffer CB : register(b0)
        {
            float4 rect;
            float round;
            float intensity;
            float softness;
            float pad0;
            float4 color;
        };
        struct PSIn { float4 pos : SV_POSITION; float2 pixel : TEXCOORD0; };
        float rounded_box_sd(float2 p, float2 half_size, float round)
        {
            float2 q = abs(p) - half_size + round;
            return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - round;
        }
        float4 main(PSIn input) : SV_TARGET
        {
            float2 center = (rect.xy + rect.zw) * 0.5;
            float2 half_size = (rect.zw - rect.xy) * 0.5;
            float2 p = input.pixel - center;
            float dist = rounded_box_sd(p, half_size, round);
            float edge = max(softness, 0.001);
            float glow = exp(-(dist * dist) / (edge * edge));
            float alpha = saturate(glow * intensity) * color.a;
            return float4(color.rgb * alpha, alpha);
        }
    )";

    ID3D11Device*           s_device           = nullptr;
    ID3D11VertexShader*     s_vs               = nullptr;
    ID3D11PixelShader*      s_ps               = nullptr;
    ID3D11InputLayout*      s_layout           = nullptr;
    ID3D11Buffer*           s_vs_cb            = nullptr;
    ID3D11Buffer*           s_ps_cb            = nullptr;
    ID3D11Buffer*           s_vb               = nullptr;
    ID3D11BlendState*       s_blend            = nullptr;
    ID3D11RasterizerState*  s_raster           = nullptr;
    ID3D11DepthStencilState* s_depth           = nullptr;
    std::vector<GlowRequest> s_queue;

    bool compile_shader(const char* src, const char* entry, const char* target, ID3DBlob** blob)
    {
        ID3DBlob* error = nullptr;
        const HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
            entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &error);
        if (FAILED(hr))
        {
            if (error) error->Release();
            return false;
        }
        if (error) error->Release();
        return true;
    }

    bool create_states()
    {
        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable           = TRUE;
        blend.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlend             = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(s_device->CreateBlendState(&blend, &s_blend)))
            return false;

        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.ScissorEnable = FALSE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(s_device->CreateRasterizerState(&raster, &s_raster)))
            return false;

        D3D11_DEPTH_STENCIL_DESC depth = {};
        depth.DepthEnable = FALSE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depth.StencilEnable = FALSE;
        if (FAILED(s_device->CreateDepthStencilState(&depth, &s_depth)))
            return false;

        D3D11_BUFFER_DESC vb = {};
        vb.Usage = D3D11_USAGE_DYNAMIC;
        vb.ByteWidth = sizeof(float) * 8;
        vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(s_device->CreateBuffer(&vb, nullptr, &s_vb));
    }
}

namespace glow_renderer
{
    bool init(ID3D11Device* device)
    {
        shutdown();
        if (!device)
            return false;

        s_device = device;
        s_device->AddRef();

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        if (!compile_shader(k_vs_hlsl, "main", "vs_4_0", &vs_blob))
            return false;
        if (!compile_shader(k_ps_hlsl, "main", "ps_4_0", &ps_blob))
        {
            vs_blob->Release();
            return false;
        }

        if (FAILED(s_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &s_vs)))
        {
            vs_blob->Release();
            ps_blob->Release();
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(s_device->CreateInputLayout(layout, 1, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &s_layout)))
        {
            vs_blob->Release();
            ps_blob->Release();
            return false;
        }
        vs_blob->Release();

        if (FAILED(s_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &s_ps)))
        {
            ps_blob->Release();
            return false;
        }
        ps_blob->Release();

        D3D11_BUFFER_DESC cb = {};
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cb.ByteWidth = sizeof(float) * 4;
        if (FAILED(s_device->CreateBuffer(&cb, nullptr, &s_vs_cb)))
            return false;

        cb.ByteWidth = (sizeof(GlowCB) + 15) & ~15;
        if (FAILED(s_device->CreateBuffer(&cb, nullptr, &s_ps_cb)))
            return false;

        if (!create_states())
            return false;

        return true;
    }

    void shutdown()
    {
        s_queue.clear();
        if (s_vb)     { s_vb->Release();     s_vb = nullptr; }
        if (s_vs_cb)  { s_vs_cb->Release();  s_vs_cb = nullptr; }
        if (s_ps_cb)  { s_ps_cb->Release();  s_ps_cb = nullptr; }
        if (s_blend)  { s_blend->Release();  s_blend = nullptr; }
        if (s_raster) { s_raster->Release(); s_raster = nullptr; }
        if (s_depth)  { s_depth->Release();  s_depth = nullptr; }
        if (s_layout) { s_layout->Release(); s_layout = nullptr; }
        if (s_ps)     { s_ps->Release();     s_ps = nullptr; }
        if (s_vs)     { s_vs->Release();     s_vs = nullptr; }
        if (s_device) { s_device->Release(); s_device = nullptr; }
    }

    void queue_rect(const ImRect& rect, float round, float strength, const ImVec4& color)
    {
        if (strength < 0.01f)
            return;
        s_queue.push_back({ rect, round, strength, color });
    }

    void clear_queue()
    {
        s_queue.clear();
    }

    void flush(ID3D11DeviceContext* ctx, const ImVec2& display_size)
    {
        if (!ctx || !s_vs || !s_ps || s_queue.empty() || display_size.x <= 0.f || display_size.y <= 0.f)
            return;

        D3D11_VIEWPORT vp = {};
        vp.Width    = display_size.x;
        vp.Height   = display_size.y;
        vp.MaxDepth = 1.f;
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(s_layout);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(s_vs, nullptr, 0);
        ctx->PSSetShader(s_ps, nullptr, 0);
        ctx->RSSetState(s_raster);
        ctx->OMSetDepthStencilState(s_depth, 0);

        const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
        ctx->OMSetBlendState(s_blend, blend_factor, 0xFFFFFFFF);

        const UINT stride = sizeof(float) * 2;
        const UINT offset = 0;

        for (const GlowRequest& req : s_queue)
        {
            const float expand = 18.f + req.strength * 12.f;
            const ImRect draw_bb(
                { req.rect.Min.x - expand, req.rect.Min.y - expand },
                { req.rect.Max.x + expand, req.rect.Max.y + expand });

            struct VSCB { float rect[4]; float display[2]; float pad[2]; } vs_data = {};
            vs_data.rect[0] = draw_bb.Min.x;
            vs_data.rect[1] = draw_bb.Min.y;
            vs_data.rect[2] = draw_bb.Max.x;
            vs_data.rect[3] = draw_bb.Max.y;
            vs_data.display[0] = display_size.x;
            vs_data.display[1] = display_size.y;

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(ctx->Map(s_vs_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                memcpy(mapped.pData, &vs_data, sizeof(vs_data));
                ctx->Unmap(s_vs_cb, 0);
            }

            GlowCB ps_data = {};
            ps_data.rect[0] = req.rect.Min.x;
            ps_data.rect[1] = req.rect.Min.y;
            ps_data.rect[2] = req.rect.Max.x;
            ps_data.rect[3] = req.rect.Max.y;
            ps_data.round     = req.round;
            ps_data.intensity = req.strength * 0.62f;
            ps_data.softness  = 9.f + req.round * 0.45f;
            ps_data.color[0]  = req.color.x;
            ps_data.color[1]  = req.color.y;
            ps_data.color[2]  = req.color.z;
            ps_data.color[3]  = 1.f;

            if (SUCCEEDED(ctx->Map(s_ps_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                memcpy(mapped.pData, &ps_data, sizeof(ps_data));
                ctx->Unmap(s_ps_cb, 0);
            }

            const float verts[8] = { 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f };
            if (SUCCEEDED(ctx->Map(s_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                memcpy(mapped.pData, verts, sizeof(verts));
                ctx->Unmap(s_vb, 0);
            }

            ctx->IASetVertexBuffers(0, 1, &s_vb, &stride, &offset);
            ctx->VSSetConstantBuffers(0, 1, &s_vs_cb);
            ctx->PSSetConstantBuffers(0, 1, &s_ps_cb);
            ctx->Draw(4, 0);
        }
    }
}
