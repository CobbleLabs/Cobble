#include "login_page.h"
#include "helpers/fonts.h"
#include "helpers/icons.h"
#include "theme/colors.h"
#include "theme/layout.h"
#include "theme/ui_scale.h"
#include "core/auth_system.h"

#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstring>

namespace
{
    using ui_scale::s;

    ImU32 col(const ImVec4& c, float a = 1.f)
    {
        return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
    }
}

namespace login_page
{
    void draw(const ImRect& panel, state& st, bool& blocks_drag)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Fondo oscuro de la tarjeta de login
        const float card_w = s(380.f);
        const float card_h = s(340.f);
        const ImVec2 center = panel.GetCenter();
        const ImRect card({ center.x - card_w * 0.5f, center.y - card_h * 0.5f },
                          { center.x + card_w * 0.5f, center.y + card_h * 0.5f });
        const float round = s(12.f);

        dl->AddRectFilled(card.Min, card.Max, col(colors::widget), round);
        dl->AddRect(card.Min, card.Max, col(colors::accent), round, 0, 1.5f);

        // Logo / Onda cardíaca
        ImVec2 waveCenter = { center.x, card.Min.y + s(40.f) };
        ImVec2 wavePts[8];
        float waveX[] = { -16.f, -10.f, -7.f, -3.f, 0.f, 3.f, 7.f, 16.f };
        float waveY[] = { 0.f, 0.f, -8.f, 12.f, -12.f, 12.f, 0.f, 0.f };
        for (int i = 0; i < 8; i++) {
            wavePts[i] = waveCenter + ImVec2(s(waveX[i]), s(waveY[i]));
        }
        dl->AddPolyline(wavePts, 8, col(colors::accent), 0, s(3.f));

        // Exit button
        {
            const float ex_size = s(36.f);
            const ImRect exit_bb({ card.Max.x - ex_size, card.Min.y },
                                 { card.Max.x, card.Min.y + ex_size });

            ImGui::PushID("##exit_btn_login");
            ImGui::SetCursorScreenPos(exit_bb.Min);
            ImGui::InvisibleButton("##exit", exit_bb.GetSize());
            const bool ex_hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                st.exit_pressed = true;
            if (ex_hovered) blocks_drag = true;
            ImGui::PopID();

            ImFont* ex_f = fonts::icon(15.f);
            const float ex_px = fonts::design_to_pixel(15.f);
            const ImVec2 ex_isz = ex_f->CalcTextSizeA(ex_px, FLT_MAX, 0.f, icons::power);
            dl->AddText(ex_f, ex_px,
                        { exit_bb.GetCenter().x - ex_isz.x * 0.5f, exit_bb.GetCenter().y - ex_isz.y * 0.5f - s(0.5f) },
                        col(ex_hovered ? colors::danger : colors::text_secondary), icons::power);
        }

        ImGui::SetCursorScreenPos({ card.Min.x + s(20.f), card.Min.y + s(70.f) });
        ImGui::BeginGroup();

        ImGui::PushFont(fonts::inter(s(16.f), true));
        float textW = ImGui::CalcTextSize("PULSE NETWORK OPTIMIZER").x;
        ImGui::SetCursorPosX(card.Min.x + (card_w - textW) * 0.5f);
        ImGui::TextColored(colors::text, "PULSE NETWORK OPTIMIZER");
        ImGui::PopFont();

        ImGui::PushFont(fonts::inter(s(11.f), false));
        textW = ImGui::CalcTextSize("ENTER YOUR LICENSE KEY").x;
        ImGui::SetCursorPosX(card.Min.x + (card_w - textW) * 0.5f);
        ImGui::TextColored(colors::text_muted, "ENTER YOUR LICENSE KEY");
        ImGui::PopFont();

        ImGui::Dummy({ 0.f, s(15.f) });

        // --- LICENSE KEY INPUT ---
        ImGui::PushFont(fonts::inter(s(12.f), false));
        ImGui::SetCursorPosX(card.Min.x + s(30.f));
        ImGui::TextColored(colors::text_muted, "License Key:");
        
        ImGui::SetCursorPosX(card.Min.x + s(30.f));
        ImGui::PushItemWidth(card_w - s(60.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, col(colors::bg));
        ImGui::PushStyleColor(ImGuiCol_Border, col(colors::divider));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s(6.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { s(8.f), s(8.f) });
        
        if (ImGui::InputText("##lickey", st.input_key, sizeof(st.input_key)))
        {
            st.status_msg = "";
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) blocks_drag = true;

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();

        ImGui::Dummy({ 0.f, s(6.f) });

        // Remember Me checkbox
        ImGui::SetCursorPosX(card.Min.x + s(30.f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, col(colors::bg));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, col(colors::accent));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s(3.f));
        if (ImGui::Checkbox("Remember me", &st.remember_me))
        {
            auth::set_remember_me(st.remember_me);
        }
        if (ImGui::IsItemHovered()) blocks_drag = true;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        ImGui::Dummy({ 0.f, s(8.f) });

        // Botón Activate
        ImGui::SetCursorPosX(card.Min.x + s(30.f));
        ImGui::PushStyleColor(ImGuiCol_Button, col(colors::accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(colors::accent_hover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(colors::accent_purple));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s(6.f));

        const char* btn_label = st.is_logging_in ? "CONNECTING..." : "ACTIVATE";

        if (ImGui::Button(btn_label, { card_w - s(60.f), s(32.f) }) && !st.is_logging_in)
        {
            st.is_logging_in = true;
            auth::set_remember_me(st.remember_me);
            auth::AuthStatus res = auth::login(st.input_key);
            st.is_logging_in = false;

            if (res == auth::AuthStatus::Success)
            {
                st.status_msg = auth::status_to_string(res);
                st.status_color = ImVec4(0.2f, 1.f, 0.4f, 1.f);
            }
            else
            {
                st.status_msg = auth::get_status_message().empty() 
                    ? auth::status_to_string(res) 
                    : auth::get_status_message();
                st.status_color = colors::danger;
            }
        }
        if (ImGui::IsItemHovered()) blocks_drag = true;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::Dummy({ 0.f, s(6.f) });

        // Botón Copy HWID
        ImGui::SetCursorPosX(card.Min.x + s(30.f));
        ImGui::PushStyleColor(ImGuiCol_Button, col(colors::bg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(colors::surface_hover));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s(6.f));

        if (ImGui::Button(st.copied_hwid ? "HWID COPIED!" : "COPY MY HWID TO CLIPBOARD", { card_w - s(60.f), s(26.f) }))
        {
            std::string hwid = auth::get_system_hwid();
            auth::copy_to_clipboard(hwid);
            st.copied_hwid = true;
            st.status_msg = "HWID copied to clipboard.";
            st.status_color = ImVec4(0.3f, 0.8f, 1.f, 1.f);
        }
        if (ImGui::IsItemHovered()) blocks_drag = true;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        ImGui::PopFont();

        // Mensaje de estado
        if (!st.status_msg.empty())
        {
            ImGui::Dummy({ 0.f, s(8.f) });
            ImGui::PushFont(fonts::inter(s(10.f), false));
            textW = ImGui::CalcTextSize(st.status_msg.c_str()).x;
            ImGui::SetCursorPosX(card.Min.x + (card_w - textW) * 0.5f);
            ImGui::TextColored(st.status_color, "%s", st.status_msg.c_str());
            ImGui::PopFont();
        }

        ImGui::EndGroup();
    }
}
