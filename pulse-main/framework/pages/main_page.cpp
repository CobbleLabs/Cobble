#include "main_page.h"

#include "helpers/fonts.h"
#include "helpers/icons.h"
#include "theme/colors.h"
#include "theme/layout.h"
#include "theme/ui_scale.h"
#include "core/optimizer.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <functional>

namespace
{
    using ui_scale::s;

    ImU32 col(const ImVec4& c, float a = 1.f)
    {
        return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
    }

    float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    struct nav_item { const char* glyph; const char* label; };
    const nav_item k_nav[] = {
        { icons::sliders,  "Pulse" },
        { icons::settings, "Config" },
    };
    constexpr int k_nav_count = (int)(sizeof(k_nav) / sizeof(k_nav[0]));

    void draw_sidebar(ImDrawList* dl, const ImRect& panel, main_page::state& st, bool& blocks_drag)
    {
        const float w = s(layout::sidebar_w);
        const ImRect bar({ panel.Min.x + s(38.f), panel.Min.y + s(50.f) }, { panel.Min.x + s(38.f) + w, panel.Max.y - s(50.f) });
        const float round = s(layout::shell_round);

        dl->AddRectFilled(bar.Min, bar.Max, col(colors::topbar), round);

        float y = bar.Min.y + s(20.f);

        const ImVec2 center = { bar.GetCenter().x, y + s(10.f) };
        
        // Draw the heartbeat wave in the sidebar
        ImVec2 wavePts[8];
        float waveX[] = { -14.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 14.f };
        float waveY[] = { 0.f, 0.f, -6.f, 10.f, -10.f, 10.f, 0.f, 0.f };
        for (int i = 0; i < 8; i++) {
            wavePts[i] = center + ImVec2(s(waveX[i]), s(waveY[i]));
        }
        dl->AddPolyline(wavePts, 8, col(colors::accent), 0, s(2.5f));
        
        y += s(46.f);

        const float item = s(layout::sidebar_item);
        for (int i = 0; i < k_nav_count; ++i)
        {
            const ImRect ib({ bar.Min.x + (w - item) * 0.5f, y },
                            { bar.Min.x + (w - item) * 0.5f + item, y + item });
            const bool hovered = ib.Contains(ImGui::GetIO().MousePos);
            if (hovered) blocks_drag = true;

            ImGui::PushID(i + 100);
            ImGui::SetCursorScreenPos(ib.Min);
            ImGui::InvisibleButton("##nav", ib.GetSize());
            if (ImGui::IsItemClicked()) st.active_nav = i;
            ImGui::PopID();

            const bool sel = (st.active_nav == i);
            if (sel)
            {
                dl->AddRectFilled(ib.Min, ib.Max, col(colors::accent, 0.18f), s(8.f));
            }
            else if (hovered)
            {
                dl->AddRectFilled(ib.Min, ib.Max, col(colors::widget_hover), s(8.f));
            }

            ImFont* icf = fonts::icon(15.f);
            const float ipx = fonts::design_to_pixel(15.f);
            const ImVec2 isz = icf->CalcTextSizeA(ipx, FLT_MAX, 0.f, k_nav[i].glyph);
            dl->AddText(icf, ipx,
                fonts::snap(ImVec2(ib.GetCenter().x - isz.x * 0.5f,
                    ib.GetCenter().y - isz.y * 0.5f - s(1.0f))),
                col(sel ? colors::accent : colors::text_secondary, sel ? 1.f : 0.6f), k_nav[i].glyph);

            y += item + s(layout::sidebar_gap);
        }
    }

    static ImVec4 lerp_color(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    void draw_icon_rotated(ImDrawList* dl, ImFont* font, float px, const char* glyph,
        const ImRect& box, ImU32 color, float angle)
    {
        const ImVec2 gsz = font->CalcTextSizeA(px, FLT_MAX, 0.f, glyph);
        const ImVec2 center = box.GetCenter();
        const ImVec2 pos = { center.x - gsz.x * 0.5f, center.y - gsz.y * 0.5f };

        const int vtx_start = dl->VtxBuffer.Size;
        dl->AddText(font, px, fonts::snap(pos), color, glyph);
        const int vtx_end = dl->VtxBuffer.Size;

        if (angle != 0.f)
        {
            const float sn = sinf(angle), cs = cosf(angle);
            for (int i = vtx_start; i < vtx_end; ++i)
            {
                ImDrawVert& v = dl->VtxBuffer[i];
                const float dx = v.pos.x - center.x;
                const float dy = v.pos.y - center.y;
                v.pos.x = center.x + dx * cs - dy * sn;
                v.pos.y = center.y + dx * sn + dy * cs;
            }
        }
    }

    void draw_app_content(ImDrawList* dl, const ImRect& panel, main_page::state& st)
    {
        const float round = s(6.f); 
        
        const ImRect search_bb({ panel.Min.x + s(112.f), panel.Min.y + s(50.f) },
                               { panel.Min.x + s(112.f) + s(304.f), panel.Min.y + s(86.f) });

        const float search_round = s(6.f);
        dl->AddRectFilled(search_bb.Min, search_bb.Max, col(colors::topbar), search_round);

        // Exit button
        {
            const float ex_size = s(36.f);
            const ImRect exit_bb({ panel.Min.x + s(112.f) + s(350.f) - ex_size, panel.Min.y + s(50.f) },
                                 { panel.Min.x + s(112.f) + s(350.f), panel.Min.y + s(86.f) });

            ImGui::PushID("##exit_btn");
            ImGui::SetCursorScreenPos(exit_bb.Min);
            ImGui::InvisibleButton("##exit", exit_bb.GetSize());
            const bool ex_hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                st.exit_pressed = true;
            ImGui::PopID();

            dl->AddRectFilled(exit_bb.Min, exit_bb.Max, col(ex_hovered ? colors::danger : colors::topbar), search_round);

            ImFont* ex_f = fonts::icon(15.f);
            const float ex_px = fonts::design_to_pixel(15.f);
            const ImVec2 ex_isz = ex_f->CalcTextSizeA(ex_px, FLT_MAX, 0.f, icons::power);
            dl->AddText(ex_f, ex_px,
                        { exit_bb.GetCenter().x - ex_isz.x * 0.5f, exit_bb.GetCenter().y - ex_isz.y * 0.5f - s(0.5f) },
                        col(ex_hovered ? colors::text : colors::text_secondary), icons::power);
        }

        static char search_buf[64] = "";

        const ImGuiID search_id = ImGui::GetID("##search");
        const bool active = (ImGui::GetActiveID() == search_id);
        
        static float active_anim = 0.f;
        active_anim += (((active || search_buf[0] != '\0') ? 1.f : 0.f) - active_anim) * ImGui::GetIO().DeltaTime * 12.f;

        ImFont* search_f = fonts::icon(14.f);
        const float search_f_px = fonts::design_to_pixel(14.f);
        const ImVec2 search_isz = search_f->CalcTextSizeA(search_f_px, FLT_MAX, 0.f, icons::search);
        const ImVec2 search_pos = {
            search_bb.Min.x + s(12.f),
            search_bb.GetCenter().y - search_isz.y * 0.5f - s(0.5f)
        };
        ImVec4 icon_color = lerp_color(colors::text_secondary, colors::accent, active_anim);
        dl->AddText(search_f, search_f_px, search_pos, col(icon_color), icons::search);

        ImFont* search_text_f = fonts::inter(13.f);
        const float search_text_px = fonts::design_to_pixel(13.f);
        const ImVec2 text_sz = search_text_f->CalcTextSizeA(search_text_px, FLT_MAX, 0.f, search_buf);
        const ImVec2 text_pos = {
            search_bb.Min.x + s(32.f),
            search_bb.GetCenter().y - text_sz.y * 0.5f - s(0.5f)
        };
        if (search_buf[0] != '\0')
        {
            dl->AddText(search_text_f, search_text_px, text_pos, col(colors::text_secondary), search_buf);
        }

        static float placeholder_anim = 0.f;
        const bool has_text_or_active = (search_buf[0] != '\0' || active);
        placeholder_anim += ((has_text_or_active ? 1.f : 0.f) - placeholder_anim) * ImGui::GetIO().DeltaTime * 12.f;

        if (placeholder_anim < 0.99f)
        {
            ImFont* placeholder_f = fonts::inter(13.f);
            const float placeholder_px = fonts::design_to_pixel(13.f);
            const ImVec2 placeholder_pos = {
                search_bb.Min.x + s(32.f) - placeholder_anim * s(12.f),
                search_bb.GetCenter().y - placeholder_px * 0.5f - s(0.5f)
            };
            dl->AddText(placeholder_f, placeholder_px, placeholder_pos, col(colors::text_muted, 1.f - placeholder_anim), "Search settings...");
        }

        ImGui::PushFont(search_text_f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 0.f, 0.f, 0.f)); 
        
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(32.f), s(9.f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);

        ImGui::SetCursorScreenPos(search_bb.Min + ImVec2(0.f, s(2.5f)));

        ImGui::PushItemWidth(s(304.f));
        ImGui::InputText("##search", search_buf, IM_ARRAYSIZE(search_buf));
        ImGui::PopItemWidth();
        
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::PopFont();

        static float caret_x = 0.f;
        float target_caret_x = search_bb.Min.x + s(32.f) + text_sz.x;
        if (caret_x == 0.f || !active) caret_x = target_caret_x;
        else caret_x += (target_caret_x - caret_x) * ImGui::GetIO().DeltaTime * 20.f;

        if (active)
        {
            float blink = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 12.f);
            dl->AddLine(
                { caret_x, search_bb.Min.y + s(9.f) },
                { caret_x, search_bb.Max.y - s(9.f) },
                col(colors::accent, blink),
                s(1.5f)
            );
        }

        const float item_h = s(44.f);
        const float gap = s(10.f);

        const float content_top = panel.Min.y + s(96.f);
        // Leave room at the bottom for a fixed status bar (from 420 to 452)
        const float content_bottom = panel.Min.y + s(418.f);
        const ImRect content_clip({ panel.Min.x + s(108.f), content_top - s(2.f) },
                                  { panel.Min.x + s(112.f) + s(360.f), content_bottom });

        if (st.active_nav != st.prev_nav)
        {
            st.prev_nav = st.active_nav;
            st.tab_anim = 0.f;
            st.content_scroll_target = 0.f;
            st.content_scroll = 0.f;
        }
        st.tab_anim += (1.f - st.tab_anim) * ImGui::GetIO().DeltaTime * 9.f;
        if (st.tab_anim > 0.999f) st.tab_anim = 1.f;

        const bool content_hovered = content_clip.Contains(ImGui::GetIO().MousePos);
        if (content_hovered && st.open_popup == 0)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
                st.content_scroll_target -= wheel * s(64.f);
        }

        st.content_scroll += (st.content_scroll_target - st.content_scroll) * ImMin(1.f, ImGui::GetIO().DeltaTime * 16.f);

        const float tab_slide = (1.f - st.tab_anim) * s(18.f);
        float row_y = content_top - st.content_scroll + tab_slide;

        dl->PushClipRect(content_clip.Min, content_clip.Max, true);

        auto matches_search = [&](const char* label)
        {
            if (search_buf[0] == '\0') return true;
            std::string text_str(label);
            std::string q_str(search_buf);
            std::transform(text_str.begin(), text_str.end(), text_str.begin(), ::tolower);
            std::transform(q_str.begin(), q_str.end(), q_str.begin(), ::tolower);
            return text_str.find(q_str) != std::string::npos;
        };

        auto row_visible = [&](float h) -> bool
        {
            return (row_y + h) > content_top && row_y < content_bottom;
        };

        static std::unordered_map<int, float> cog_anim;
        auto draw_cog = [&](int popup_idx, float center_x, float center_y) -> float
        {
            const float cog_box = s(22.f); 
            const ImRect cog_bb({ center_x - cog_box, center_y - cog_box * 0.5f },
                                { center_x,           center_y + cog_box * 0.5f });

            ImGui::PushID(popup_idx + 7000);
            ImGui::SetCursorScreenPos(cog_bb.Min);
            ImGui::InvisibleButton("##cog", cog_bb.GetSize());
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                st.open_popup = (st.open_popup == popup_idx) ? 0 : popup_idx;
            ImGui::PopID();

            float& a = cog_anim[popup_idx];
            a += ((hovered ? 1.f : 0.f) - a) * ImGui::GetIO().DeltaTime * 12.f;

            ImFont* icon_f = fonts::icon(16.f); 
            const float icon_px = fonts::design_to_pixel(16.f);
            const float angle = a * 1.5708f; 
            draw_icon_rotated(dl, icon_f, icon_px, icons::cog, cog_bb,
                col(colors::text_secondary), angle); 
            return cog_bb.Min.x;
        };

        auto draw_section_title = [&](const char* title)
        {
            ImFont* tf = fonts::inter(11.5f, true);
            const float tpx = fonts::design_to_pixel(11.5f);
            const ImVec2 title_pos = { panel.Min.x + s(112.f), row_y + s(4.f) };
            const ImVec2 title_sz = tf->CalcTextSizeA(tpx, FLT_MAX, 0.f, title);
            dl->AddText(tf, tpx, title_pos, col(colors::text_muted), title);

            const float line_y = title_pos.y + title_sz.y * 0.5f;
            const float line_x0 = title_pos.x + title_sz.x + s(10.f);
            const float line_x1 = panel.Min.x + s(112.f) + s(350.f);
            if (line_x1 > line_x0)
                dl->AddLine({ line_x0, line_y }, { line_x1, line_y }, col(colors::divider), s(1.0f));

            row_y += s(26.f);
        };

        auto draw_toggle_row = [&](const char* label, bool& value, int popup_idx, const char* inline_val_str = nullptr)
        {
            if (!matches_search(label)) return;
            if (!row_visible(item_h)) { row_y += item_h + gap; return; }

            const ImRect box_bb({ panel.Min.x + s(112.f), row_y }, { panel.Min.x + s(112.f) + s(350.f), row_y + item_h });
            dl->AddRectFilled(box_bb.Min, box_bb.Max, col(colors::topbar), round);

            const float center_y = row_y + item_h * 0.5f;

            {
                ImFont* lbl_f = fonts::inter(12.5f, false);
                const float lbl_px = fonts::design_to_pixel(12.5f);
                dl->AddText(lbl_f, lbl_px,
                            { box_bb.Min.x + s(15.f), center_y - lbl_px * 0.5f },
                            col(colors::text), label);
            }

            const float right_x = box_bb.Max.x - s(15.f);

            // iOS-style switch dimensions
            const ImVec2 toggle_size = { s(34.f), s(18.f) };
            const ImVec2 toggle_min = { right_x - toggle_size.x, center_y - toggle_size.y * 0.5f };
            const ImVec2 toggle_max = { right_x, center_y + toggle_size.y * 0.5f };

            float next_right_x = toggle_min.x;

            if (popup_idx > 0)
            {
                next_right_x = draw_cog(popup_idx, toggle_min.x - s(12.f), center_y);
            }

            if (inline_val_str)
            {
                ImFont* val_f = fonts::inter(13.f);
                const float val_f_px = fonts::design_to_pixel(13.f);
                const ImVec2 val_sz = val_f->CalcTextSizeA(val_f_px, FLT_MAX, 0.f, inline_val_str);
                dl->AddText(val_f, val_f_px, { next_right_x - s(10.f) - val_sz.x, center_y - val_sz.y * 0.5f }, col(colors::text_secondary), inline_val_str);
            }

            const bool interactable = !optimizer::g_state.is_processing;

            ImGui::PushID(label);
            ImGui::SetCursorScreenPos(toggle_min);
            ImGui::InvisibleButton("##toggle", toggle_size);
            const bool cb_hovered = ImGui::IsItemHovered() && interactable;
            if (ImGui::IsItemClicked() && interactable)
                value = !value;
            ImGui::PopID();

            static std::unordered_map<std::string, float> anims;
            float& anim = anims[label];
            anim += (((value) ? 1.f : 0.f) - anim) * ImGui::GetIO().DeltaTime * 14.f;

            const float r = toggle_size.y * 0.5f;

            // Determine track color
            ImVec4 inactive_track = cb_hovered ? colors::widget_hover : colors::widget;
            ImVec4 active_track = colors::accent;
            if (!interactable)
            {
                inactive_track.w *= 0.5f;
                active_track.w *= 0.5f;
            }
            ImVec4 track_col = lerp_color(inactive_track, active_track, anim);
            dl->AddRectFilled(toggle_min, toggle_max, col(track_col), r);

            // Draw sliding knob
            const float knob_r = r - s(2.f);
            const float knob_left = toggle_min.x + r;
            const float knob_right = toggle_max.x - r;
            const float knob_x = knob_left + anim * (knob_right - knob_left);
            
            dl->AddCircleFilled({ knob_x, center_y }, knob_r, col(colors::text));
            dl->AddCircle({ knob_x, center_y }, knob_r, col(colors::bg, 0.25f), 0, s(0.8f)); // Subtle border

            row_y += item_h + gap;
        };

        auto draw_slider_row = [&](const char* label, float& value, float min_val, float max_val, int popup_idx, const char* inline_val_str = nullptr)
        {
            if (!matches_search(label)) return;
            
            const float slider_box_h = s(56.f);
            if (!row_visible(slider_box_h)) { row_y += slider_box_h + gap; return; }

            const ImRect box_bb({ panel.Min.x + s(112.f), row_y }, { panel.Min.x + s(112.f) + s(350.f), row_y + slider_box_h });
            dl->AddRectFilled(box_bb.Min, box_bb.Max, col(colors::topbar), round);

            // Draw label
            {
                ImFont* lbl_f = fonts::inter(12.f, false);
                const float lbl_px = fonts::design_to_pixel(12.f);
                dl->AddText(lbl_f, lbl_px,
                            { box_bb.Min.x + s(15.f), row_y + s(12.f) },
                            col(colors::text_secondary), label);
            }

            const float right_x = box_bb.Max.x - s(15.f);
            float next_right_x = right_x;
            if (popup_idx > 0)
            {
                next_right_x = draw_cog(popup_idx, right_x, row_y + s(18.f));
            }

            // Draw value text
            char val_buf[64];
            if (inline_val_str)
            {
                sprintf_s(val_buf, sizeof(val_buf), "%s  %.0f", inline_val_str, value);
            }
            else
            {
                sprintf_s(val_buf, sizeof(val_buf), "%.0f", value);
            }
            ImFont* val_f = fonts::inter(12.f, true);
            const float val_f_px = fonts::design_to_pixel(12.f);
            const ImVec2 val_sz = val_f->CalcTextSizeA(val_f_px, FLT_MAX, 0.f, val_buf);
            
            const float val_x = next_right_x - (popup_idx > 0 ? s(10.f) : 0.f) - val_sz.x;
            dl->AddText(val_f, val_f_px, { val_x, row_y + s(12.f) }, col(colors::text), val_buf);

            // Slider track metrics
            const float track_left = box_bb.Min.x + s(15.f);
            const float track_right = box_bb.Max.x - s(15.f);
            const float track_w = track_right - track_left;
            const float track_y = row_y + s(38.f);
            const float track_h = s(2.5f);

            const bool interactable = !optimizer::g_state.is_processing;

            ImGui::PushID(label);
            ImGui::SetCursorScreenPos({ track_left, track_y - s(8.f) });
            ImGui::InvisibleButton("##slider", ImVec2(track_w, s(16.f)));
            if (ImGui::IsItemActive() && interactable)
            {
                float mouse_x = ImGui::GetIO().MousePos.x;
                float pct = (mouse_x - track_left) / track_w;
                if (pct < 0.f) pct = 0.f;
                if (pct > 1.f) pct = 1.f;
                value = min_val + pct * (max_val - min_val);
            }
            ImGui::PopID();

            // Draw track background
            dl->AddRectFilled({ track_left, track_y - track_h * 0.5f }, { track_right, track_y + track_h * 0.5f }, col(colors::widget), track_h * 0.5f);

            float pct = (value - min_val) / (max_val - min_val);
            if (pct < 0.f) pct = 0.f;
            if (pct > 1.f) pct = 1.f;
            
            // Draw filled track portion
            if (pct > 0.f)
            {
                ImVec4 accent_color = colors::accent;
                if (!interactable) accent_color.w *= 0.5f;
                dl->AddRectFilled({ track_left, track_y - track_h * 0.5f }, { track_left + pct * track_w, track_y + track_h * 0.5f }, col(accent_color), track_h * 0.5f);
            }

            // Draw circular grab handle
            const float grab_cx = track_left + pct * track_w;
            ImVec4 grab_color = colors::accent;
            if (!interactable) grab_color.w *= 0.5f;
            
            // Glowing outer ring
            dl->AddCircle({ grab_cx, track_y }, s(7.5f), col(grab_color, 0.25f), 0, s(1.5f));
            // Solid center
            dl->AddCircleFilled({ grab_cx, track_y }, s(4.5f), col(grab_color));
            // Tiny inner dot or border to make it clean
            dl->AddCircle({ grab_cx, track_y }, s(4.5f), col(colors::bg), 0, s(0.8f));

            row_y += slider_box_h + gap;
        };

        auto draw_dropdown_row = [&](const char* label, int& value, const char* const items[], int items_count)
        {
            if (!matches_search(label)) return;
            if (!row_visible(item_h)) { row_y += item_h + gap; return; }

            const ImRect box_bb({ panel.Min.x + s(112.f), row_y }, { panel.Min.x + s(112.f) + s(350.f), row_y + item_h });
            dl->AddRectFilled(box_bb.Min, box_bb.Max, col(colors::topbar), round);

            const float center_y = row_y + item_h * 0.5f;

            {
                ImFont* lbl_f = fonts::inter(12.5f, false);
                const float lbl_px = fonts::design_to_pixel(12.5f);
                dl->AddText(lbl_f, lbl_px,
                            { box_bb.Min.x + s(15.f), center_y - lbl_px * 0.5f },
                            col(colors::text), label);
            }

            const float right_x = box_bb.Max.x - s(15.f);
            const float combo_w = s(100.f);
            const ImVec2 combo_pos = { right_x - combo_w, center_y - s(10.f) };

            const bool interactable = !optimizer::g_state.is_processing;

            ImGui::PushID(label);
            ImGui::SetCursorScreenPos(combo_pos);
            ImGui::SetNextItemWidth(combo_w);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, colors::widget);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, colors::widget_hover);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, colors::widget);
            ImGui::PushStyleColor(ImGuiCol_Header, colors::accent_hover);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors::accent);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors::accent_purple);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, colors::dropdown_bg);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(6.f), s(2.f)));

            ImGui::BeginDisabled(!interactable);
            if (ImGui::BeginCombo("##combo", items[value], ImGuiComboFlags_None))
            {
                for (int n = 0; n < items_count; n++)
                {
                    bool is_selected = (value == n);
                    if (ImGui::Selectable(items[n], is_selected))
                    {
                        value = n;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();

            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(7);
            ImGui::PopID();

            row_y += item_h + gap;
        };

        auto draw_value_row = [&](const char* label, const char* value_str, int popup_idx, const ImVec4* color_preview = nullptr)
        {
            if (!matches_search(label)) return;
            if (!row_visible(item_h)) { row_y += item_h + gap; return; }

            const ImRect box_bb({ panel.Min.x + s(112.f), row_y }, { panel.Min.x + s(112.f) + s(350.f), row_y + item_h });
            dl->AddRectFilled(box_bb.Min, box_bb.Max, col(colors::topbar), round);

            const float center_y = row_y + item_h * 0.5f;

            {
                ImFont* lbl_f = fonts::inter(12.5f, false);
                const float lbl_px = fonts::design_to_pixel(12.5f);
                dl->AddText(lbl_f, lbl_px,
                            { box_bb.Min.x + s(15.f), center_y - lbl_px * 0.5f },
                            col(colors::text), label);
            }

            const float right_x = box_bb.Max.x - s(15.f);
            float next_right_x = right_x;

            if (popup_idx > 0)
            {
                next_right_x = draw_cog(popup_idx, right_x, center_y);
            }

            if (color_preview)
            {
                const float box_size = s(12.f);
                const ImVec2 c_min = { next_right_x - s(10.f) - box_size, center_y - box_size * 0.5f };
                const ImVec2 c_max = { next_right_x - s(10.f), center_y + box_size * 0.5f };
                dl->AddRectFilled(c_min, c_max, col(*color_preview), s(2.f));
                next_right_x -= (box_size + s(10.f));
            }

            if (value_str)
            {
                ImFont* val_f = fonts::inter(13.f);
                const float val_f_px = fonts::design_to_pixel(13.f);
                const ImVec2 val_sz = val_f->CalcTextSizeA(val_f_px, FLT_MAX, 0.f, value_str);
                dl->AddText(val_f, val_f_px, { next_right_x - s(10.f) - val_sz.x, center_y - val_sz.y * 0.5f }, col(colors::accent), value_str);
            }

            row_y += item_h + gap;
        };

        auto draw_button_row = [&](const char* label, const char* btn_text, bool enabled, ImVec4 btn_color, std::function<void()> callback)
        {
            if (!matches_search(label)) return;
            if (!row_visible(item_h)) { row_y += item_h + gap; return; }
            
            const ImRect box_bb({ panel.Min.x + s(112.f), row_y }, { panel.Min.x + s(112.f) + s(350.f), row_y + item_h });
            dl->AddRectFilled(box_bb.Min, box_bb.Max, col(colors::topbar), round);
            
            const float center_y = row_y + item_h * 0.5f;
            
            {
                ImFont* lbl_f = fonts::inter(12.5f, false);
                const float lbl_px = fonts::design_to_pixel(12.5f);
                dl->AddText(lbl_f, lbl_px,
                            { box_bb.Min.x + s(15.f), center_y - lbl_px * 0.5f },
                            col(colors::text), label);
            }
            
            const float btn_w = s(100.f);
            const float btn_h = s(28.f);
            const ImRect btn_bb({ box_bb.Max.x - s(15.f) - btn_w, center_y - btn_h * 0.5f },
                                { box_bb.Max.x - s(15.f),         center_y + btn_h * 0.5f });
                                
            ImGui::PushID(label);
            ImGui::SetCursorScreenPos(btn_bb.Min);
            ImGui::InvisibleButton("##btn", btn_bb.GetSize());
            const bool hovered = ImGui::IsItemHovered() && enabled;
            const bool active_press = ImGui::IsItemActive() && enabled;
            if (ImGui::IsItemClicked() && enabled)
            {
                callback();
            }
            ImGui::PopID();
            
            ImVec4 bg_col = btn_color;
            if (!enabled)
            {
                bg_col.w *= 0.4f; 
            }
            else if (active_press)
            {
                bg_col.x *= 0.8f; bg_col.y *= 0.8f; bg_col.z *= 0.8f;
            }
            else if (hovered)
            {
                bg_col.x = fminf(bg_col.x * 1.15f, 1.f);
                bg_col.y = fminf(bg_col.y * 1.15f, 1.f);
                bg_col.z = fminf(bg_col.z * 1.15f, 1.f);
            }
            
            dl->AddRectFilled(btn_bb.Min, btn_bb.Max, col(bg_col), s(4.f));
            
            ImFont* btn_f = fonts::inter(12.5f, true);
            const float btn_px = fonts::design_to_pixel(12.5f);
            const ImVec2 text_sz = btn_f->CalcTextSizeA(btn_px, FLT_MAX, 0.f, btn_text);
            const ImVec2 text_pos = {
                btn_bb.GetCenter().x - text_sz.x * 0.5f,
                btn_bb.GetCenter().y - text_sz.y * 0.5f
            };
            
            // Calculate button text color based on background luminance for high contrast readability
            ImU32 text_color;
            if (!enabled)
            {
                text_color = col(colors::text_secondary);
            }
            else
            {
                float luminance = 0.299f * bg_col.x + 0.587f * bg_col.y + 0.114f * bg_col.z;
                if (luminance > 0.5f)
                {
                    text_color = col(colors::bg); // Dark text on light background
                }
                else
                {
                    text_color = col(colors::text); // Light text on dark background
                }
            }
            dl->AddText(btn_f, btn_px, text_pos, text_color, btn_text);
            
            row_y += item_h + gap;
        };

        if (st.active_nav == 0) 
        {
            draw_section_title("Pulse Optimizer");

            draw_toggle_row("Smart Packets", st.smart_packets, 0);

            const char* hit_reg_lbl = "Vanilla";
            if (st.hit_reg >= 1.f && st.hit_reg < 61.f) hit_reg_lbl = "Level 1";
            else if (st.hit_reg >= 61.f && st.hit_reg < 100.f) hit_reg_lbl = "Level 2";
            else if (st.hit_reg >= 100.f) hit_reg_lbl = "Level 3 (Extreme)";
            draw_slider_row("Hit Registration", st.hit_reg, 0.f, 100.f, 0, hit_reg_lbl);

            const char* kb_lbl = "Vanilla";
            if (st.kb_reduction >= 1.f && st.kb_reduction < 61.f) kb_lbl = "Level 1";
            else if (st.kb_reduction >= 61.f && st.kb_reduction < 100.f) kb_lbl = "Level 2";
            else if (st.kb_reduction >= 100.f) kb_lbl = "Level 3 (Extreme)";
            draw_slider_row("Knockback Reduction", st.kb_reduction, 0.f, 100.f, 0, kb_lbl);

            const char* conn_types[] = { "Ethernet", "Wi-Fi" };
            draw_dropdown_row("Connection Type", st.connection_type, conn_types, 2);

            draw_section_title("Actions");

            const char* run_text = "RUN PULSE";
            if (optimizer::g_state.is_processing) run_text = "PULSING...";
            else if (optimizer::g_state.is_optimized) run_text = "APPLY LIVE TWEAKS";

            bool run_enabled = !optimizer::g_state.is_processing;
            draw_button_row("Optimize network", run_text, run_enabled, colors::accent, [&]() {
                optimizer::g_state.smart_packets = st.smart_packets;
                optimizer::g_state.hit_reg = st.hit_reg;
                optimizer::g_state.kb_reduction = st.kb_reduction;
                optimizer::g_state.connection_type = st.connection_type;
                optimizer::apply_tweaks_async();
            });

            if (optimizer::g_state.is_optimized)
            {
                bool restore_enabled = !optimizer::g_state.is_processing;
                draw_button_row("Restore defaults", "RESTORE", restore_enabled, colors::danger, [&]() {
                    optimizer::restore_defaults_async();
                });
            }
        }
        else if (st.active_nav == 1) 
        {
            draw_section_title("Appearance");
            draw_value_row("Accent Color", nullptr, 11);
            draw_slider_row("UI Opacity", st.ui_opacity, 0.2f, 1.f, 0);

            draw_section_title("Interface");
            draw_slider_row("Interface Scale", st.ui_scale, 0.80f, 1.50f, 0);
        }

        ui_scale::user_scale = st.ui_scale;

        dl->PopClipRect();

        // Viewport scroll clamp
        {
            const float viewport_h = content_bottom - content_top;
            const float content_h = (row_y + st.content_scroll - tab_slide) - content_top;
            const float max_scroll = content_h - viewport_h > 0.f ? content_h - viewport_h : 0.f;

            if (st.content_scroll_target < 0.f)          st.content_scroll_target = 0.f;
            if (st.content_scroll_target > max_scroll)   st.content_scroll_target = max_scroll;
        }

        // --- Render fixed status bar at the bottom of backplate ---
        {
            const float bar_y = content_bottom + s(8.f);
            const float line_x0 = panel.Min.x + s(112.f);
            const float line_x1 = panel.Min.x + s(112.f) + s(350.f);
            
            // Thin horizontal separator line
            dl->AddLine({ line_x0, bar_y }, { line_x1, bar_y }, col(colors::divider), s(1.0f));
            
            // Status dot
            const ImVec2 dot_center = { line_x0 + s(12.f), bar_y + s(16.f) };
            ImVec4 dot_color = colors::text_secondary;
            if (optimizer::g_state.is_optimized)
            {
                dot_color = colors::accent; 
            }
            else if (optimizer::g_state.is_processing)
            {
                // Pulsing yellow/orange
                float p = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 8.f);
                dot_color = lerp_color(colors::text_secondary, colors::accent, p);
            }
            
            float pulse = 1.0f;
            if (optimizer::g_state.is_optimized)
            {
                pulse = 0.6f + 0.4f * sinf((float)ImGui::GetTime() * 5.f);
            }
            dl->AddCircleFilled(dot_center, s(4.f), col(dot_color, pulse));
            
            // Status text
            ImFont* status_f = fonts::inter(12.f);
            const float status_px = fonts::design_to_pixel(12.f);
            const float text_y = dot_center.y - status_px * 0.5f;
            dl->AddText(status_f, status_px, { dot_center.x + s(10.f), text_y }, col(colors::text_secondary), optimizer::g_state.status_text.c_str());
        }

        // --- Render Slide-out Selection Popup (Accent Color Picker only) ---
        const ImVec2 bp_min = panel.Min + ImVec2(s(28.f), s(40.f));

        static float popup_anim = 0.f;
        popup_anim += (((st.open_popup == 11) ? 1.f : 0.f) - popup_anim) * ImGui::GetIO().DeltaTime * 12.f;

        if (st.open_popup != 11)
            st.pick_init_for = 0;

        if (popup_anim > 0.01f)
        {
            const float popup_w = s(232.f);
            float target_h = 12.f + 208.f + 8.f + 28.f + 6.f + 28.f + 12.f; 

            static float popup_h_anim = 110.f;
            popup_h_anim += (target_h - popup_h_anim) * ImGui::GetIO().DeltaTime * 14.f;

            const float popup_h = s(popup_h_anim);
            
            float target_x = panel.Max.x - s(10.f);
            const float current_x = target_x + (popup_anim - 1.f) * s(20.f);
            const ImRect popup_bb({ current_x, bp_min.y }, { current_x + popup_w, bp_min.y + popup_h });

            dl->AddRectFilled(popup_bb.Min, popup_bb.Max, col(colors::topbar, popup_anim), s(6.f));

            if (st.open_popup == 11)
            {
                ImVec4& value = colors::accent;

                if (st.pick_init_for != st.open_popup)
                {
                    ImGui::ColorConvertRGBtoHSV(value.x, value.y, value.z, st.pick_h, st.pick_s, st.pick_v);
                    st.pick_a = value.w;
                    st.pick_init_for = st.open_popup;
                }

                const float pad = s(12.f);
                const float inner_w = popup_w - pad * 2.f;
                const float sq_x = popup_bb.Min.x + pad;
                const float sq_y = popup_bb.Min.y + pad;
                const float sq_size = inner_w; 

                const ImRect sv_bb({ sq_x, sq_y }, { sq_x + sq_size, sq_y + sq_size });
                {
                    ImGui::PushID("##sv_picker");
                    ImGui::SetCursorScreenPos(sv_bb.Min);
                    ImGui::InvisibleButton("##sv", sv_bb.GetSize());
                    if (ImGui::IsItemActive())
                    {
                        const float ns = clamp01((ImGui::GetIO().MousePos.x - sv_bb.Min.x) / sv_bb.GetWidth());
                        const float nv = 1.f - clamp01((ImGui::GetIO().MousePos.y - sv_bb.Min.y) / sv_bb.GetHeight());
                        st.pick_s = ns; st.pick_v = nv;
                    }
                    ImGui::PopID();

                    const ImU32 hue_col = ImColor::HSV(st.pick_h, 1.f, 1.f);
                    const ImU32 white = col(ImVec4(1, 1, 1, 1), popup_anim);
                    const ImU32 black = col(ImVec4(0, 0, 0, 1), popup_anim);
                    const ImU32 trans = ImGui::GetColorU32(ImVec4(0, 0, 0, 0));
                    dl->PushClipRect(sv_bb.Min, sv_bb.Max, true);
                    dl->AddRectFilledMultiColor(sv_bb.Min, sv_bb.Max, white, hue_col, hue_col, white);
                    dl->AddRectFilledMultiColor(sv_bb.Min, sv_bb.Max, trans, trans, black, black);
                    dl->PopClipRect();

                    const float sel_x = sv_bb.Min.x + sv_bb.GetWidth() * st.pick_s;
                    const float sel_y = sv_bb.Min.y + sv_bb.GetHeight() * (1.f - st.pick_v);
                    dl->AddCircle({ sel_x, sel_y }, s(5.f), col(ImVec4(1, 1, 1, 1), popup_anim), 24, s(2.f));
                    dl->AddCircle({ sel_x, sel_y }, s(4.f), col(ImVec4(0, 0, 0, 0.47f), popup_anim), 24, s(1.f));
                }

                const float track_h = s(10.f);
                const float row_h = s(28.f);
                float cursor_y = sv_bb.Max.y + s(8.f);

                // Hue slider
                {
                    const ImRect hue_bb({ sq_x, cursor_y + (row_h - track_h) * 0.5f },
                                        { sq_x + sq_size, cursor_y + (row_h - track_h) * 0.5f + track_h });
                    ImGui::PushID("##hue_slider");
                    ImGui::SetCursorScreenPos({ hue_bb.Min.x, cursor_y });
                    ImGui::InvisibleButton("##hue", ImVec2(sq_size, row_h));
                    if (ImGui::IsItemActive())
                        st.pick_h = clamp01((ImGui::GetIO().MousePos.x - hue_bb.Min.x) / hue_bb.GetWidth());
                    ImGui::PopID();

                    const float rad = track_h * 0.5f;
                    const int cols = (int)(hue_bb.GetWidth() / s(2.f));
                    for (int i = 0; i < cols; ++i)
                    {
                        const float t0 = (float)i / (float)cols;
                        const float t1 = (float)(i + 1) / (float)cols;
                        const ImU32 hc = ImColor::HSV((t0 + t1) * 0.5f, 1.f, 1.f);
                        ImDrawFlags flags = ImDrawFlags_RoundCornersNone;
                        if (i == 0) flags = ImDrawFlags_RoundCornersLeft;
                        else if (i == cols - 1) flags = ImDrawFlags_RoundCornersRight;
                        dl->AddRectFilled({ ImLerp(hue_bb.Min.x, hue_bb.Max.x, t0), hue_bb.Min.y },
                                          { ImLerp(hue_bb.Min.x, hue_bb.Max.x, t1), hue_bb.Max.y }, hc, rad, flags);
                    }
                    const float gw = s(5.f), gh = s(18.f);
                    const float cx = ImLerp(hue_bb.Min.x + gh * 0.5f, hue_bb.Max.x - gh * 0.5f, st.pick_h);
                    dl->AddRectFilled({ cx - gw * 0.5f, hue_bb.GetCenter().y - gh * 0.5f },
                                      { cx + gw * 0.5f, hue_bb.GetCenter().y + gh * 0.5f }, col(ImVec4(1, 1, 1, 1), popup_anim), s(3.f));
                    cursor_y += row_h + s(6.f);
                }

                ImVec4 rgb = value;
                ImGui::ColorConvertHSVtoRGB(st.pick_h, st.pick_s, st.pick_v, rgb.x, rgb.y, rgb.z);

                // Opacity slider
                {
                    const ImRect a_bb({ sq_x, cursor_y + (row_h - track_h) * 0.5f },
                                      { sq_x + sq_size, cursor_y + (row_h - track_h) * 0.5f + track_h });
                    ImGui::PushID("##alpha_slider");
                    ImGui::SetCursorScreenPos({ a_bb.Min.x, cursor_y });
                    ImGui::InvisibleButton("##alpha", ImVec2(sq_size, row_h));
                    if (ImGui::IsItemActive())
                        st.pick_a = clamp01((ImGui::GetIO().MousePos.x - a_bb.Min.x) / a_bb.GetWidth());
                    ImGui::PopID();

                    dl->PushClipRect(a_bb.Min, a_bb.Max, true);
                    const float cell = s(6.f);
                    const ImU32 c0 = col(ImVec4(42.f / 255.f, 42.f / 255.f, 50.f / 255.f, 1.f), popup_anim);
                    const ImU32 c1 = col(ImVec4(32.f / 255.f, 32.f / 255.f, 38.f / 255.f, 1.f), popup_anim);
                    for (float y = a_bb.Min.y; y < a_bb.Max.y; y += cell)
                        for (float x = a_bb.Min.x; x < a_bb.Max.x; x += cell)
                        {
                            const int ix = (int)((x - a_bb.Min.x) / cell);
                            const int iy = (int)((y - a_bb.Min.y) / cell);
                            dl->AddRectFilled({ x, y }, { ImMin(x + cell, a_bb.Max.x), ImMin(y + cell, a_bb.Max.y) }, (ix + iy) % 2 ? c0 : c1);
                        }
                    const ImU32 rgb_col = col(ImVec4(rgb.x, rgb.y, rgb.z, 1.f), popup_anim);
                    const ImU32 trans_col = ImGui::GetColorU32(ImVec4(rgb.x, rgb.y, rgb.z, 0.f));
                    dl->AddRectFilledMultiColor(a_bb.Min, a_bb.Max, trans_col, rgb_col, rgb_col, trans_col);
                    dl->PopClipRect();

                    const float gw = s(5.f), gh = s(18.f);
                    const float cx = ImLerp(a_bb.Min.x + gh * 0.5f, a_bb.Max.x - gh * 0.5f, st.pick_a);
                    dl->AddRectFilled({ cx - gw * 0.5f, a_bb.GetCenter().y - gh * 0.5f },
                                      { cx + gw * 0.5f, a_bb.GetCenter().y + gh * 0.5f }, col(ImVec4(1, 1, 1, 1), popup_anim), s(3.f));
                }

                ImGui::ColorConvertHSVtoRGB(st.pick_h, st.pick_s, st.pick_v, value.x, value.y, value.z);
                value.w = st.pick_a;
                colors::sync_accent_hover();
            }
        }
    }
}

namespace main_page
{
    void draw(const ImRect& panel, state& st, bool& blocks_window_drag)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool sb_block = false;
        draw_sidebar(dl, panel, st, sb_block);
        draw_app_content(dl, panel, st);
        blocks_window_drag = blocks_window_drag || sb_block;
    }
}
