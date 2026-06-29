#include "fonts.h"

#include "fontello_data.h"
#include "fonts_data.h"
#include "logo_data.h"
#include "theme/layout.h"
#include "imgui_freetype.h"
#include "imgui_impl_dx11.h"

#include <cmath>
#include <vector>

namespace
{
    enum class kind { inter, icon, logo };

    struct font_entry
    {
        float   design = 0.f;
        bool    bold   = false;
        kind    type   = kind::inter;
        ImFont* font   = nullptr;
    };

    std::vector<font_entry> g_entries;
    float g_ui_scale    = 1.f;
    float g_built_scale = -1.f;
    bool  g_pending_rebuild = false;

    static const ImWchar icon_range[] = { 0xE000, 0xF8FF, 0 };

    float pixel_size(float design)
    {
        const float px = roundf(design * g_ui_scale);
        return px < 1.f ? 1.f : px;
    }

    ImFontConfig base_cfg()
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.PixelSnapH           = false;
        cfg.OversampleH          = 3;
        cfg.OversampleV          = 3;
        cfg.RasterizerMultiply   = 1.05f;
        cfg.FontBuilderFlags     = ImGuiFreeTypeBuilderFlags_LightHinting;
        return cfg;
    }

    ImFontConfig icon_cfg()
    {
        ImFontConfig cfg = base_cfg();
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        return cfg;
    }

    void rebuild_atlas()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui_ImplDX11_InvalidateDeviceObjects();
        io.Fonts->Clear();

        for (auto& e : g_entries)
            e.font = nullptr;

        for (auto& e : g_entries)
        {
            const float px = pixel_size(e.design);
            ImFontConfig cfg = (e.type == kind::icon) ? icon_cfg() : base_cfg();
            if (e.bold)
                cfg.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_Bold;

            switch (e.type)
            {
            case kind::icon:
                e.font = io.Fonts->AddFontFromMemoryTTF(
                    (void*)fontello_data::rawData, (int)fontello_data::ttf_size,
                    px, &cfg, icon_range);
                break;
            case kind::logo:
                e.font = io.Fonts->AddFontFromMemoryTTF(
                    (void*)logo_data::ttf, (int)logo_data::ttf_size,
                    px, &cfg,
                    io.Fonts->GetGlyphRangesDefault());
                break;
            case kind::inter:
            default:
                e.font = io.Fonts->AddFontFromMemoryTTF(
                    (void*)inter.data(), (int)inter.size(),
                    px, &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
                break;
            }
        }

        for (const auto& e : g_entries)
        {
            if (e.type == kind::inter && !e.bold && e.design == 15.f && e.font)
            {
                io.FontDefault = e.font;
                break;
            }
        }

        io.Fonts->Build();
        ImGui_ImplDX11_CreateDeviceObjects();
        g_built_scale = g_ui_scale;
        g_pending_rebuild = false;
    }

    bool scale_step_changed(float new_scale)
    {
        if (g_built_scale < 0.f)
            return true;

        for (const auto& e : g_entries)
        {
            g_ui_scale = g_built_scale;
            const float old_px = pixel_size(e.design);
            g_ui_scale = new_scale;
            const float new_px = pixel_size(e.design);
            if (old_px != new_px)
                return true;
        }
        return false;
    }

    font_entry* find_entry(float design, bool bold, kind type)
    {
        for (auto& e : g_entries)
        {
            if (e.design == design && e.bold == bold && e.type == type)
                return &e;
        }
        return nullptr;
    }

    font_entry& ensure_entry(float design, bool bold, kind type)
    {
        if (font_entry* found = find_entry(design, bold, type))
            return *found;

        g_entries.push_back({ design, bold, type, nullptr });
        g_pending_rebuild = true;
        return g_entries.back();
    }
}

namespace fonts
{
    void init()
    {
        g_ui_scale    = 1.f;
        g_built_scale = -1.f;
        g_entries = {
            { 10.f, false, kind::inter, nullptr },
            { 11.f, false, kind::inter, nullptr },
            { 11.5f, false, kind::inter, nullptr },
            { 11.5f, true,  kind::inter, nullptr },
            { 12.f, false, kind::inter, nullptr },
            { 12.f, true,  kind::inter, nullptr },
            { 12.5f, false, kind::inter, nullptr },
            { 13.f, false, kind::inter, nullptr },
            { 13.f, true,  kind::inter, nullptr },
            { 14.f, false, kind::inter, nullptr },
            { 14.f, true,  kind::inter, nullptr },
            { 15.f, false, kind::inter, nullptr },
            { 15.f, true,  kind::inter, nullptr },
            { 16.f, false, kind::inter, nullptr },
            { 16.f, true,  kind::inter, nullptr },
            { 17.f, false, kind::inter, nullptr },
            { 17.f, true,  kind::inter, nullptr },
            { 18.f, false, kind::inter, nullptr },
            { 18.f, true,  kind::inter, nullptr },
            { 20.f, true,  kind::inter, nullptr },
            { 22.f, true,  kind::inter, nullptr },
            { 11.f, false, kind::icon, nullptr },
            { 12.f, false, kind::icon, nullptr },
            { 13.f, false, kind::icon, nullptr },
            { 14.f, false, kind::icon, nullptr },
            { 15.f, false, kind::icon, nullptr },
            { 16.f, false, kind::icon, nullptr },
        };
        rebuild_atlas();
    }

    void shutdown()
    {
        g_entries.clear();
        g_built_scale = -1.f;
    }

    void sync_ui_scale(float factor)
    {
        if (factor < 0.01f)
            factor = 0.01f;

        g_ui_scale = factor;

        if (!g_pending_rebuild && !scale_step_changed(factor))
            return;

        rebuild_atlas();
    }

    float design_to_pixel(float design_size)
    {
        return pixel_size(design_size);
    }

    float size(ImFont* font)
    {
        return font ? font->FontSize : 13.f;
    }

    ImFont* inter(float design_size, bool bold)
    {
        ImFont* f = ensure_entry(design_size, bold, kind::inter).font;
        return f ? f : ImGui::GetIO().FontDefault;
    }

    ImFont* icon(float design_size)
    {
        ImFont* f = ensure_entry(design_size, false, kind::icon).font;
        return f ? f : ImGui::GetIO().FontDefault;
    }

    ImFont* logo(float design_size)
    {
        ImFont* f = ensure_entry(design_size, false, kind::logo).font;
        return f ? f : ImGui::GetIO().FontDefault;
    }
}
