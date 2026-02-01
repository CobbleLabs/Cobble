#include "widgets.hpp"
#include <Windows.h>
#include <cmath>
#include "../../module/module.h"
#include "../menu/menu.h"
#include "icons.hpp"
#include "theme.hpp"

using namespace ImGui;

// ==================== BINDER STATE ====================
static bool g_BinderActive = false;

bool ui::IsBinderActive() {
    return g_BinderActive;
}

void ui::ResetBinderState() {
    g_BinderActive = false;
}

// ==================== GLOBAL SCHEME COLOR ====================
float g_SchemeColor[4] = {0.843f, 0.376f, 0.078f, 1.0f};

inline ImU32 GetSchemeColor(float alpha) {
    ImVec4 col = ImVec4(g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], g_SchemeColor[3]);
    col.w *= alpha * ImGui::GetStyle().Alpha * ui::alpha_mod;
    return ImGui::ColorConvertFloat4ToU32(col);
}

// ==================== TAB DEFINITIONS ====================
std::vector<ui::tab_t> ui::tabs = {
    {ICON_FA_CROSSHAIRS,
     "Combat",
     {"AutoClicker", "AimAssist", "Velocity", "Reach", "HitSelect", "Hitbox", "NoHitDelay", "Backtrack"}},
    {ICON_FA_EYE, "Visuals", {"NameTags", "ESP", "ItemESP", "Tracers", "BlockESP", "Trajectories", "ArrayList", "Notifications"}},
    {ICON_FA_RUNNING, "Movement", {"Timer", "NoJumpDelay", "Blink"}},
    {ICON_FA_KEYBOARD,
     "Utility",
     {"ArmorSwitch", "AutoRefill", "FastPlace", "NoItemRelease", "BridgeAssist", "AutoTool", "InvManager"}},
    {ICON_FA_GEAR, "Settings", {}}};

// ==================== SCALING ====================

void ui::updateScale() {
    float screenHeight = ImGui::GetIO().DisplaySize.y;
    scale = screenHeight / baseHeight;
    if (scale < 0.85f)
        scale = 0.85f;
    if (scale > 2.0f)
        scale = 2.0f;
}

float ui::s(float value) {
    return value * scale;
}

ImVec2 ui::s(const ImVec2& v) {
    return {v.x * scale, v.y * scale};
}

ImU32 ui::GetColor(ImGuiCol idx, float alpha_mult) {
    ImVec4 col = ImGui::GetStyle().Colors[idx];
    col.w *= alpha_mult * ImGui::GetStyle().Alpha * alpha_mod;
    return ImGui::ColorConvertFloat4ToU32(col);
}

// ==================== ANIMATION SYSTEM ====================

float ui::animate(float current, float min, float max, bool state) {
    float target = state ? max : min;
    return ImLerp(current, target, 21.f * ImGui::GetIO().DeltaTime);
}

ImColor ui::col_anim(ImU32 from, ImU32 to, float t) {
    ImVec4 f = ImGui::ColorConvertU32ToFloat4(from);
    ImVec4 tt = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImLerp(f, tt, t));
}

void ui::handle_anims() {
    anim = ImLerp(anim, anim_dst, 21.f * ImGui::GetIO().DeltaTime);
    if (abs(anim - anim_dst) < 0.01f) {
        anim_dst = 1.f;
        cur_tab = next_tab;
    }

    content_anim = ImLerp(content_anim, content_anim_dst, 21.f * ImGui::GetIO().DeltaTime);
    if (abs(content_anim - content_anim_dst) < 0.01f) {
        content_anim_dst = 1.f;
        if (cur_tab < tabs.size())
            tabs[cur_tab].cur_subtab = tabs[cur_tab].next_subtab;
    }
}

// ==================== LAYOUT HELPERS ====================

void ui::two_column(const char* leftTitle, std::function<void()> leftContent, const char* rightTitle,
                    std::function<void()> rightContent) {
    float halfWidth =
        (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2;

    ImGui::BeginGroup();
    {
        begin_child(leftTitle, {halfWidth, 0});
        leftContent();
        end_child();
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    {
        begin_child(rightTitle, {halfWidth, 0});
        rightContent();
        end_child();
    }
    ImGui::EndGroup();
}

void ui::section(const char* title, std::function<void()> content) {
    float fullWidth = ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2;

    ImGui::BeginGroup();
    begin_child(title, {fullWidth, 0});
    content();
    end_child();
    ImGui::EndGroup();
}

void ui::full_width(const char* title, std::function<void()> content) {
    section(title, content);
}

// ==================== CHILD CONTAINERS ====================

void ui::begin_child(const char* name, ImVec2 size) {
    float headerHeight = s(33.f);

    ImGui::BeginChild(std::string(name).append("decor").c_str(),
                      CalcItemSize(size,
                                   (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 -
                                       GImGui->Style.ItemSpacing.x / 2,
                                   0),
                      0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(),
                                              GetColor(ImGuiCol_ChildBg), GImGui->Style.ChildRounding);

    if (ImGui::CalcTextSize(name, 0, 1).x > 0) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetWindowPos(),
            {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight},
            GetColor(ImGuiCol_Header), GImGui->Style.ChildRounding, ImDrawFlags_RoundCornersTop);
        ImGui::GetWindowDrawList()->AddLine(
            {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + headerHeight},
            {ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight},
            GetSchemeColor(0.5f));
        ImGui::GetWindowDrawList()->AddText(
            font(1), font(1)->FontSize,
            {ImGui::GetWindowPos().x + s(13),
             ImGui::GetWindowPos().y + headerHeight / 2 -
                 font(1)->CalcTextSizeA(font(1)->FontSize, FLT_MAX, 0.f, name, ImGui::FindRenderedTextEnd(name)).y / 2},
            GetColor(ImGuiCol_Text), name, ImGui::FindRenderedTextEnd(name));
        ImGui::SetCursorPosY(headerHeight + 1);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {s(13), s(15)});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {s(8), s(14)});
    ImGui::BeginChild(name, {-1, size.y == 0 ? size.y : -1}, 0,
                      ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoBackground);
}

void ui::end_child() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
}

// ==================== CHECKBOX ====================

bool ui::Checkbox(const char* label, bool* v) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();

    auto name_size = CalcTextSize(label, 0, 1);
    auto draw = window->DrawList;
    auto pos = window->DC.CursorPos;

    float square_sz = s(18.f);
    float checkSize = s(9.f);
    ImRect bb(pos, {pos.x + square_sz, pos.y + square_sz});
    ImRect total_bb(pos, {pos.x + square_sz + name_size.x + GImGui->Style.ItemInnerSpacing.x, pos.y + square_sz});
    ItemAdd(total_bb, id);
    ItemSize(total_bb);

    bool hovered, held;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        MarkItemEdited(id);
    }

    struct cb_state {
        float anim;
        float hover;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, cb_state{0.f, 0.f, 0});

    obj.anim = animate(obj.anim, 0.f, 1.f, *v);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered && !(*v));
    obj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.hover);

    draw->AddText({total_bb.Max.x - name_size.x, bb.GetCenter().y - name_size.y / 2}, GetColor(ImGuiCol_Text), label,
                  FindRenderedTextEnd(label));
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddRectFilled(bb.Min, bb.Max, GetSchemeColor(obj.anim), style.FrameRounding);
    RenderCheckMark(draw,
                    {bb.GetCenter().x - (checkSize * obj.anim) / 2, bb.GetCenter().y - (checkSize * obj.anim) / 2},
                    GetColor(ImGuiCol_Text, obj.anim), checkSize * obj.anim);

    return pressed;
}

// ==================== CHECKBOX WITH BIND ====================

bool ui::CheckboxBind(const char* label, bool* v, int* key) {
    auto window = GetCurrentWindow();
    auto style = GetStyle();
    auto draw = window->DrawList;

    float availWidth = CalcItemWidth();
    float bindBoxWidth = s(50.0f);
    float spacing = s(8.0f);
    float checkboxWidth = availWidth - bindBoxWidth - spacing;

    ImVec2 pos = window->DC.CursorPos;
    float height = s(20.f);
    float square_sz = s(18.f);

    // Checkbox part
    auto cbId = window->GetID(label);
    auto name_size = CalcTextSize(label, 0, 1);

    ImRect cbBox(pos, {pos.x + square_sz, pos.y + square_sz});
    ImRect cbTotal(pos, {pos.x + checkboxWidth, pos.y + height});

    bool cbHovered, cbHeld;
    ItemAdd(cbTotal, cbId);
    bool cbPressed = ButtonBehavior(cbTotal, cbId, &cbHovered, &cbHeld);
    if (cbPressed) {
        *v = !(*v);
        MarkItemEdited(cbId);
    }

    struct cb_s {
        float anim;
        float hover;
        ImColor col;
    };
    auto& cbObj = anim_obj(label, 0, cb_s{0.f, 0.f, 0});
    cbObj.anim = animate(cbObj.anim, 0.f, 1.f, *v);
    cbObj.hover = animate(cbObj.hover, 0.f, 1.f, cbHovered && !(*v));
    cbObj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), cbObj.hover);

    draw->AddRectFilled(cbBox.Min, cbBox.Max, cbObj.col, style.FrameRounding);
    draw->AddRect(cbBox.Min, cbBox.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddRectFilled(cbBox.Min, cbBox.Max, GetSchemeColor(cbObj.anim), style.FrameRounding);
    RenderCheckMark(draw, {cbBox.GetCenter().x - (9 * cbObj.anim) / 2, cbBox.GetCenter().y - (9 * cbObj.anim) / 2},
                    GetColor(ImGuiCol_Text, cbObj.anim), 9 * cbObj.anim);
    draw->AddText({cbBox.Max.x + style.ItemInnerSpacing.x, cbBox.GetCenter().y - name_size.y / 2},
                  GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));

    // Binder part
    std::string bindId = std::string(label) + "##bind";
    auto bId = window->GetID(bindId.c_str());

    ImRect bindBox(pos + ImVec2(availWidth - bindBoxWidth, (height - s(18.f)) / 2),
                   pos + ImVec2(availWidth, (height + s(18.f)) / 2));
    ItemAdd(bindBox, bId);

    bool bindHovered = ItemHoverable(bindBox, bId, 0);
    bool bindActive = GetActiveID() == bId;
    
    // Update global binder state for ESC handling in overlay
    if (bindActive) g_BinderActive = true;

    struct bind_s {
        float anim;
        float hover;
        ImColor col;
        ImColor textCol;
        int framesSinceActive;
    };
    auto& bindObj = anim_obj(bindId.c_str(), 0, bind_s{0.f, 0.f, 0, 0, 0});
    bindObj.anim = animate(bindObj.anim, 0.f, 1.f, bindActive);
    bindObj.hover = animate(bindObj.hover, 0.f, 1.f, bindHovered || bindActive);
    bindObj.col = col_anim(col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), bindObj.hover),
                           GetColor(ImGuiCol_FrameBgActive), bindObj.anim);
    bindObj.textCol = col_anim(GetColor(ImGuiCol_Text), GetSchemeColor(), bindObj.anim);

    if (bindHovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        SetActiveID(bId, window);
        FocusWindow(window);
        bindObj.framesSinceActive = 0;
    }

    if (bindActive && !bindHovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    bool changed = false;
    if (bindActive) {
        bindObj.framesSinceActive++;
        if (bindObj.framesSinceActive > 5) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                *key = 0;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                *key = VK_RBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                *key = VK_MBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) {
                *key = VK_XBUTTON1;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) {
                *key = VK_XBUTTON2;
                ClearActiveID();
                changed = true;
            } else {
                // Check all common key ranges
                static const int keyRanges[][2] = {
                    {0x08, 0x2F},  // Control keys, arrows, etc.
                    {0x30, 0x39},  // Numbers 0-9
                    {0x41, 0x5A},  // Letters A-Z
                    {0x60, 0x6F},  // Numpad
                    {0x70, 0x87},  // F1-F24
                    {0x90, 0x91},  // NUMLOCK, SCROLL
                    {0xA0, 0xB7},  // Shift variants, browser/media keys
                    {0xBA, 0xC0},  // OEM keys: ;=,-./`
                    {0xDB, 0xDF},  // OEM keys: [\]'
                };
                bool found = false;
                for (const auto& range : keyRanges) {
                    for (int vk = range[0]; vk <= range[1] && !found; vk++) {
                        if (vk == VK_ESCAPE)
                            continue;
                        if (GetAsyncKeyState(vk) & 0x8000) {
                            *key = vk;
                            ClearActiveID();
                            changed = true;
                            found = true;
                        }
                    }
                    if (found)
                        break;
                }
            }
        }
    }

    const char* displayText = bindActive ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    draw->AddRectFilled(bindBox.Min, bindBox.Max, bindObj.col, style.FrameRounding);
    draw->AddRect(bindBox.Min, bindBox.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({bindBox.GetCenter().x - textSize.x / 2, bindBox.GetCenter().y - textSize.y / 2}, bindObj.textCol,
                  displayText);

    ItemSize(ImVec2(availWidth, height));
    return cbPressed || changed;
}

// ==================== SLIDER FLOAT ====================

bool ui::SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, bool allowDirectInput) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = CalcTextSize(label, 0, 1);
    ImVec2 frame_size = ImVec2(CalcItemWidth(), s(6));
    ImRect total_bb(pos, pos + ImVec2(CalcItemWidth(), label_size.y + style.ItemInnerSpacing.y * 1.25f + frame_size.y));
    ImRect bb(ImVec2(total_bb.Min.x, total_bb.Max.y - frame_size.y), ImVec2(total_bb.Max.x, total_bb.Max.y));

    // Calculate value text area for direct input detection
    char value_buf[64];
    snprintf(value_buf, sizeof(value_buf), format, *v);
    ImVec2 value_size = CalcTextSize(value_buf);
    ImRect value_bb(ImVec2(total_bb.Max.x - value_size.x - s(4), total_bb.Min.y - s(2)),
                    ImVec2(total_bb.Max.x + s(4), total_bb.Min.y + value_size.y + s(2)));

    // Track editing state with persistent storage
    struct direct_input_state {
        bool editing;
        char buffer[32];
        bool justStarted;
    };
    auto& inputState = anim_obj(label, 1, direct_input_state{false, "", false});

    // Check for click on value text - only if allowDirectInput is true
    if (allowDirectInput && !inputState.editing) {
        if (IsMouseHoveringRect(value_bb.Min, value_bb.Max) && IsMouseClicked(ImGuiMouseButton_Left)) {
            inputState.editing = true;
            inputState.justStarted = true;
            snprintf(inputState.buffer, sizeof(inputState.buffer), format, *v);
        }
    }

    // Handle text input mode - render input and return early
    if (inputState.editing) {
        ItemAdd(total_bb, id);
        ItemSize(total_bb);

        // Draw slider background while editing
        struct slider_state {
            float grab;
            float anim;
            ImColor col;
        };
        auto& obj = anim_obj(label, 0, slider_state{0.f, 0.f, 0});
        float t = ImClamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
        obj.grab = t * bb.GetWidth();

        window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColor(ImGuiCol_FrameBg), 100);
        window->DrawList->AddRectFilled(bb.Min, {bb.Min.x + obj.grab * anim, bb.Max.y}, GetSchemeColor(), 100);
        float grab_radius = s(6.f);
        window->DrawList->AddCircleFilled({bb.Min.x + obj.grab * anim, bb.GetCenter().y}, grab_radius,
                                          GetColor(ImGuiCol_Text), 64);
        window->DrawList->AddText(total_bb.Min, GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));

        // Position input at value location
        float inputWidth = s(25.f);
        ImVec2 inputPos(total_bb.Max.x - inputWidth, total_bb.Min.y);
        SetCursorScreenPos(inputPos);
        PushItemWidth(inputWidth);

        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(2), s(1)));
        PushStyleColor(ImGuiCol_FrameBg, GetColor(ImGuiCol_FrameBgActive));

        // Set focus only on first frame
        if (inputState.justStarted) {
            SetKeyboardFocusHere();
            inputState.justStarted = false;
        }

        ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
        bool enterPressed = InputText("##valueinput", inputState.buffer, sizeof(inputState.buffer), flags);

        // Only close on Enter key press
        if (enterPressed) {
            float newVal = 0.0f;
            if (sscanf_s(inputState.buffer, "%f", &newVal) == 1 && std::isfinite(newVal)) {
                *v = newVal;
                MarkItemEdited(id);
            }
            inputState.editing = false;
        }

        // Cancel on escape
        if (IsKeyPressed(ImGuiKey_Escape)) {
            inputState.editing = false;
        }

        // Close if clicked outside the input box
        if (IsMouseClicked(ImGuiMouseButton_Left) && !IsItemHovered()) {
            float newVal = 0.0f;
            if (sscanf_s(inputState.buffer, "%f", &newVal) == 1 && std::isfinite(newVal)) {
                *v = newVal;
                MarkItemEdited(id);
            }
            inputState.editing = false;
        }

        PopStyleColor();
        PopStyleVar();
        PopItemWidth();

        return true;
    }

    // Normal slider logic
    ItemAdd(total_bb, id);
    ItemSize(total_bb);

    const bool hovered = ItemHoverable(bb, id, 0);
    const bool clicked = hovered && IsMouseClicked(ImGuiMouseButton_Left);
    const bool active = GetActiveID() == id;

    if (clicked) {
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
    }

    if (active && IsMouseReleased(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    struct slider_state {
        float grab;
        float anim;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, slider_state{0.f, 0.f, 0});

    float t = (*v - v_min) / (v_max - v_min);
    t = ImClamp(t, 0.0f, 1.0f);
    float grab_pos = t * bb.GetWidth();

    if (active) {
        float mouse_rel = ImClamp((GetMousePos().x - bb.Min.x) / bb.GetWidth(), 0.0f, 1.0f);
        *v = v_min + mouse_rel * (v_max - v_min);
        MarkItemEdited(id);
        t = (*v - v_min) / (v_max - v_min);
        grab_pos = t * bb.GetWidth();
        obj.grab = ImLerp(obj.grab, grab_pos, 24.f * GetIO().DeltaTime);
    } else {
        obj.grab = grab_pos;
    }

    obj.anim = animate(obj.anim, 0.f, 1.f, hovered || active);
    obj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.anim);

    window->DrawList->AddRectFilled(bb.Min, bb.Max, obj.col, 100);
    window->DrawList->AddRectFilled(bb.Min, {bb.Min.x + obj.grab * anim, bb.Max.y}, GetSchemeColor(), 100);

    float grab_radius = s(active ? 7.f : (hovered ? 5.f : 6.f));
    window->DrawList->AddCircleFilled({bb.Min.x + obj.grab * anim, bb.GetCenter().y}, grab_radius,
                                      GetColor(ImGuiCol_Text), 64);

    window->DrawList->AddText(total_bb.Min, GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));
    window->DrawList->AddText({total_bb.Max.x - CalcTextSize(value_buf).x, total_bb.Min.y}, GetColor(ImGuiCol_Text),
                              value_buf);

    return active;
}

// ==================== SLIDER FLOAT WITH CLAMPING ====================
// Same as SliderFloat but clamps value to [*clamp_min, *clamp_max] DURING drag
// This prevents visual overshoot when linking min/max sliders

bool ui::SliderFloat(const char* label, float* v, float v_min, float v_max, float* clamp_min, float* clamp_max,
                     const char* format) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = CalcTextSize(label, 0, 1);
    ImVec2 frame_size = ImVec2(CalcItemWidth(), s(6));
    ImRect total_bb(pos, pos + ImVec2(CalcItemWidth(), label_size.y + style.ItemInnerSpacing.y * 1.25f + frame_size.y));
    ImRect bb(ImVec2(total_bb.Min.x, total_bb.Max.y - frame_size.y), ImVec2(total_bb.Max.x, total_bb.Max.y));

    char value_buf[64];
    snprintf(value_buf, sizeof(value_buf), format, *v);

    ItemAdd(total_bb, id);
    ItemSize(total_bb);

    const bool hovered = ItemHoverable(bb, id, 0);
    const bool clicked = hovered && IsMouseClicked(ImGuiMouseButton_Left);
    const bool active = GetActiveID() == id;

    if (clicked) {
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
    }

    if (active && IsMouseReleased(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    struct slider_state {
        float grab;
        float anim;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, slider_state{0.f, 0.f, 0});

    float t = (*v - v_min) / (v_max - v_min);
    t = ImClamp(t, 0.0f, 1.0f);
    float grab_pos = t * bb.GetWidth();

    if (active) {
        float mouse_rel = ImClamp((GetMousePos().x - bb.Min.x) / bb.GetWidth(), 0.0f, 1.0f);
        float new_val = v_min + mouse_rel * (v_max - v_min);

        // Apply linked clamping DURING drag
        if (clamp_min && new_val < *clamp_min)
            new_val = *clamp_min;
        if (clamp_max && new_val > *clamp_max)
            new_val = *clamp_max;

        *v = new_val;
        MarkItemEdited(id);
        t = (*v - v_min) / (v_max - v_min);
        grab_pos = t * bb.GetWidth();
        obj.grab = ImLerp(obj.grab, grab_pos, 24.f * GetIO().DeltaTime);
    } else {
        obj.grab = grab_pos;
    }

    obj.anim = animate(obj.anim, 0.f, 1.f, hovered || active);
    obj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.anim);

    window->DrawList->AddRectFilled(bb.Min, bb.Max, obj.col, 100);
    window->DrawList->AddRectFilled(bb.Min, {bb.Min.x + obj.grab * anim, bb.Max.y}, GetSchemeColor(), 100);

    float grab_radius = s(active ? 7.f : (hovered ? 5.f : 6.f));
    window->DrawList->AddCircleFilled({bb.Min.x + obj.grab * anim, bb.GetCenter().y}, grab_radius,
                                      GetColor(ImGuiCol_Text), 64);

    window->DrawList->AddText(total_bb.Min, GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));
    window->DrawList->AddText({total_bb.Max.x - CalcTextSize(value_buf).x, total_bb.Min.y}, GetColor(ImGuiCol_Text),
                              value_buf);

    return active;
}

// ==================== SLIDER INT ====================

bool ui::SliderInt(const char* label, int* v, int v_min, int v_max, const char* format) {
    float val = (float)*v;
    bool changed = ui::SliderFloat(label, &val, (float)v_min, (float)v_max, "%.0f");
    *v = (int)val;
    return changed;
}

// ==================== KEY NAME LOOKUP ====================

const char* ui::GetKeyName(int vk) {
    switch (vk) {
        // Mouse buttons
        case 0x00:
            return "NONE";
        case 0x01:
            return "LMB";
        case 0x02:
            return "RMB";
        case 0x03:
            return "BREAK";
        case 0x04:
            return "MMB";
        case 0x05:
            return "MB4";
        case 0x06:
            return "MB5";

        // Control keys
        case 0x08:
            return "BACK";
        case 0x09:
            return "TAB";
        case 0x0C:
            return "CLEAR";
        case 0x0D:
            return "ENTER";
        case 0x10:
            return "SHIFT";
        case 0x11:
            return "CTRL";
        case 0x12:
            return "ALT";
        case 0x13:
            return "PAUSE";
        case 0x14:
            return "CAPS";
        case 0x1B:
            return "ESC";
        case 0x20:
            return "SPACE";
        case 0x21:
            return "PGUP";
        case 0x22:
            return "PGDN";
        case 0x23:
            return "END";
        case 0x24:
            return "HOME";
        case 0x25:
            return "LEFT";
        case 0x26:
            return "UP";
        case 0x27:
            return "RIGHT";
        case 0x28:
            return "DOWN";
        case 0x29:
            return "SELECT";
        case 0x2A:
            return "PRINT";
        case 0x2B:
            return "EXEC";
        case 0x2C:
            return "PRTSC";
        case 0x2D:
            return "INS";
        case 0x2E:
            return "DEL";
        case 0x2F:
            return "HELP";

        // Numbers
        case 0x30:
            return "0";
        case 0x31:
            return "1";
        case 0x32:
            return "2";
        case 0x33:
            return "3";
        case 0x34:
            return "4";
        case 0x35:
            return "5";
        case 0x36:
            return "6";
        case 0x37:
            return "7";
        case 0x38:
            return "8";
        case 0x39:
            return "9";

        // Letters
        case 0x41:
            return "A";
        case 0x42:
            return "B";
        case 0x43:
            return "C";
        case 0x44:
            return "D";
        case 0x45:
            return "E";
        case 0x46:
            return "F";
        case 0x47:
            return "G";
        case 0x48:
            return "H";
        case 0x49:
            return "I";
        case 0x4A:
            return "J";
        case 0x4B:
            return "K";
        case 0x4C:
            return "L";
        case 0x4D:
            return "M";
        case 0x4E:
            return "N";
        case 0x4F:
            return "O";
        case 0x50:
            return "P";
        case 0x51:
            return "Q";
        case 0x52:
            return "R";
        case 0x53:
            return "S";
        case 0x54:
            return "T";
        case 0x55:
            return "U";
        case 0x56:
            return "V";
        case 0x57:
            return "W";
        case 0x58:
            return "X";
        case 0x59:
            return "Y";
        case 0x5A:
            return "Z";

        // Windows keys
        case 0x5B:
            return "LWIN";
        case 0x5C:
            return "RWIN";
        case 0x5D:
            return "APPS";
        case 0x5F:
            return "SLEEP";

        // Numpad
        case 0x60:
            return "NUM0";
        case 0x61:
            return "NUM1";
        case 0x62:
            return "NUM2";
        case 0x63:
            return "NUM3";
        case 0x64:
            return "NUM4";
        case 0x65:
            return "NUM5";
        case 0x66:
            return "NUM6";
        case 0x67:
            return "NUM7";
        case 0x68:
            return "NUM8";
        case 0x69:
            return "NUM9";
        case 0x6A:
            return "NUM*";
        case 0x6B:
            return "NUM+";
        case 0x6C:
            return "NUMSEP";
        case 0x6D:
            return "NUM-";
        case 0x6E:
            return "NUM.";
        case 0x6F:
            return "NUM/";

        // Function keys
        case 0x70:
            return "F1";
        case 0x71:
            return "F2";
        case 0x72:
            return "F3";
        case 0x73:
            return "F4";
        case 0x74:
            return "F5";
        case 0x75:
            return "F6";
        case 0x76:
            return "F7";
        case 0x77:
            return "F8";
        case 0x78:
            return "F9";
        case 0x79:
            return "F10";
        case 0x7A:
            return "F11";
        case 0x7B:
            return "F12";
        case 0x7C:
            return "F13";
        case 0x7D:
            return "F14";
        case 0x7E:
            return "F15";
        case 0x7F:
            return "F16";
        case 0x80:
            return "F17";
        case 0x81:
            return "F18";
        case 0x82:
            return "F19";
        case 0x83:
            return "F20";
        case 0x84:
            return "F21";
        case 0x85:
            return "F22";
        case 0x86:
            return "F23";
        case 0x87:
            return "F24";

        // Lock keys
        case 0x90:
            return "NUMLOCK";
        case 0x91:
            return "SCROLL";

        // Shift variants
        case 0xA0:
            return "LSHIFT";
        case 0xA1:
            return "RSHIFT";
        case 0xA2:
            return "LCTRL";
        case 0xA3:
            return "RCTRL";
        case 0xA4:
            return "LALT";
        case 0xA5:
            return "RALT";

        // Browser keys
        case 0xA6:
            return "BBACK";
        case 0xA7:
            return "BFWD";
        case 0xA8:
            return "BREF";
        case 0xA9:
            return "BSTOP";
        case 0xAA:
            return "BSRCH";
        case 0xAB:
            return "BFAV";
        case 0xAC:
            return "BHOME";

        // Media keys
        case 0xAD:
            return "MUTE";
        case 0xAE:
            return "VOLDN";
        case 0xAF:
            return "VOLUP";
        case 0xB0:
            return "NEXT";
        case 0xB1:
            return "PREV";
        case 0xB2:
            return "STOP";
        case 0xB3:
            return "PLAY";
        case 0xB4:
            return "MAIL";
        case 0xB5:
            return "MEDIA";
        case 0xB6:
            return "APP1";
        case 0xB7:
            return "APP2";

        // OEM keys (punctuation/symbols)
        case 0xBA:
            return ";";  // OEM_1: ;:
        case 0xBB:
            return "=";  // OEM_PLUS: =+
        case 0xBC:
            return ",";  // OEM_COMMA: ,<
        case 0xBD:
            return "-";  // OEM_MINUS: -_
        case 0xBE:
            return ".";  // OEM_PERIOD: .>
        case 0xBF:
            return "/";  // OEM_2: /?
        case 0xC0:
            return "`";  // OEM_3: `~
        case 0xDB:
            return "[";  // OEM_4: [{
        case 0xDC:
            return "\\";  // OEM_5: \|
        case 0xDD:
            return "]";  // OEM_6: ]}
        case 0xDE:
            return "'";  // OEM_7: '"
        case 0xDF:
            return "OEM8";
        case 0xE2:
            return "<>";  // OEM_102: additional key on non-US keyboards

        default:
            return "NONE";
    }
}

// ==================== BINDER ====================

bool ui::Binder(const char* label, int* key) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();
    auto draw = window->DrawList;

    ImVec2 labelSize = CalcTextSize(label, 0, 1);
    float boxWidth = s(50.0f);
    float boxHeight = s(18.f);

    ImVec2 pos = window->DC.CursorPos;
    float availWidth = CalcItemWidth();
    float rowHeight = s(20.f);

    ImRect bb(pos + ImVec2(availWidth - boxWidth, (rowHeight - boxHeight) / 2),
              pos + ImVec2(availWidth, (rowHeight + boxHeight) / 2));
    ImRect totalBb(pos, pos + ImVec2(availWidth, rowHeight));

    ItemAdd(bb, id);
    ItemSize(totalBb);

    bool hovered = ItemHoverable(bb, id, 0);
    bool active = GetActiveID() == id;
    
    // Update global binder state for ESC handling in overlay
    if (active) g_BinderActive = true;

    struct binder_state {
        float anim;
        float hover;
        ImColor col;
        ImColor textCol;
        int framesSinceActive;
    };
    auto& obj = anim_obj(label, 0, binder_state{0.f, 0.f, 0, 0, 0});

    obj.anim = animate(obj.anim, 0.f, 1.f, active);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered || active);
    obj.col = col_anim(col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.hover),
                       GetColor(ImGuiCol_FrameBgActive), obj.anim);
    obj.textCol = col_anim(GetColor(ImGuiCol_Text), GetSchemeColor(), obj.anim);

    if (hovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        SetActiveID(id, window);
        FocusWindow(window);
        obj.framesSinceActive = 0;
    }

    if (active && !hovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }

    bool changed = false;
    if (active) {
        obj.framesSinceActive++;
        if (obj.framesSinceActive > 5) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                *key = 0;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                *key = VK_RBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                *key = VK_MBUTTON;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) {
                *key = VK_XBUTTON1;
                ClearActiveID();
                changed = true;
            } else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) {
                *key = VK_XBUTTON2;
                ClearActiveID();
                changed = true;
            } else {
                // Check all common key ranges
                static const int keyRanges[][2] = {
                    {0x08, 0x2F},  // Control keys, arrows, etc.
                    {0x30, 0x39},  // Numbers 0-9
                    {0x41, 0x5A},  // Letters A-Z
                    {0x60, 0x6F},  // Numpad
                    {0x70, 0x87},  // F1-F24
                    {0x90, 0x91},  // NUMLOCK, SCROLL
                    {0xA0, 0xB7},  // Shift variants, browser/media keys
                    {0xBA, 0xC0},  // OEM keys: ;=,-./`
                    {0xDB, 0xDF},  // OEM keys: [\]'
                };
                bool found = false;
                for (const auto& range : keyRanges) {
                    for (int vk = range[0]; vk <= range[1] && !found; vk++) {
                        if (vk == VK_ESCAPE)
                            continue;
                        if (GetAsyncKeyState(vk) & 0x8000) {
                            *key = vk;
                            ClearActiveID();
                            changed = true;
                            found = true;
                        }
                    }
                    if (found)
                        break;
                }
            }
        }
    }

    draw->AddText({pos.x, pos.y + rowHeight / 2 - labelSize.y / 2}, GetColor(ImGuiCol_Text), label,
                  FindRenderedTextEnd(label));

    const char* displayText = active ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({bb.GetCenter().x - textSize.x / 2, bb.GetCenter().y - textSize.y / 2}, obj.textCol, displayText);

    return changed;
}

// ==================== COLOR PICKER ====================

bool ui::color(const char* name, float col[4], bool alpha) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(name);
    auto draw = window->DrawList;
    auto pos = window->DC.CursorPos;

    ImVec2 size{s(16), s(16)};
    auto label_size = CalcTextSize(name, 0, 1);

    ImRect bb(pos, {pos.x + CalcItemWidth() - size.x, pos.y + size.y});
    ItemAdd(bb, id);
    ItemSize(bb);

    draw->AddText({bb.Min.x, bb.GetCenter().y - label_size.y / 2}, GetColor(ImGuiCol_Text), name,
                  FindRenderedTextEnd(name));

    bool value_changed = false;
    if (col) {
        pos = window->DC.CursorPos;
        window->DC.CursorPos = ImVec2{bb.Max.x, bb.Min.y};

        ImGuiColorEditFlags flags =
            alpha ? (ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs |
                     ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar)
                  : (ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                     ImGuiColorEditFlags_NoAlpha);

        value_changed = ColorEdit4(std::string(name).append("##col").c_str(), col, flags);
        window->DC.CursorPos = pos;
    }

    return value_changed;
}

// ==================== COMBO ====================

bool ui::Combo(const char* label, int* current, const char* const items[], int count) {
    return ImGui::Combo(label, current, items, count);
}

// ==================== TAB WIDGET ====================

static int rotation_start_index;

static void rotate_start() {
    rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
}

static ImVec2 rotation_center() {
    ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX);
    const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);
    return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2);
}

static void rotate_end(float rad, ImVec2 center) {
    float s = sin(rad), c = cos(rad);
    center = ImRotate(center, s, c) - center;
    auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
}

void ui::tab(int num) {
    auto icon = tabs[num].m_icon;
    auto name = tabs[num].m_name;

    bool selected = ui::next_tab == num;

    auto window = ImGui::GetCurrentWindow();
    auto id = window->GetID(name);

    ImVec2 icon_size = CalcTextSize(icon, 0, 1);
    ImVec2 name_size =
        font(2 + (selected ? 1 : 0))
            ->CalcTextSizeA(font(2 + (selected ? 1 : 0))->FontSize, FLT_MAX, 0.f, name, FindRenderedTextEnd(name));

    ImVec2 tab_size{window->Size.x, s(38)};
    ImRect bb{window->DC.CursorPos, window->DC.CursorPos + tab_size};
    ItemAdd(bb, id);
    ItemSize(bb);

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    if (pressed && !selected) {
        ui::next_tab = num;
        ui::anim_dst = 0.f;
    }

    struct tab_state {
        float cur;
        float hover;
        float rad;
        ImColor col;
    };
    auto& obj = anim_obj(name, 0, tab_state{0.f, 0.f, 0.f, 0});

    obj.cur = animate(obj.cur, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
    obj.rad = animate(obj.rad, 0.f, IM_PI, selected);
    obj.col = col_anim(col_anim(GetColor(ImGuiCol_TextDisabled), GetColor(ImGuiCol_Text, 0.75f), obj.hover),
                       GetColor(ImGuiCol_Text), obj.cur);

    // Hide icons on small resolutions
    bool showIcons = ImGui::GetIO().DisplaySize.y >= 600.0f;

    // Icon position and text spacing
    float iconX = bb.Min.x + s(15);
    float iconTextGap = s(10);  // consistent gap between icon and text
    float textOffsetX = showIcons ? (iconX - bb.Min.x + icon_size.x + iconTextGap) : s(15);

    if (showIcons) {
        window->DrawList->AddText({iconX, bb.GetCenter().y - icon_size.y / 2}, obj.col, icon,
                                  FindRenderedTextEnd(icon));
    }
    window->DrawList->AddText(font(2 + (selected ? 1 : 0)), font(2 + (selected ? 1 : 0))->FontSize,
                              {bb.Min.x + textOffsetX, bb.GetCenter().y - name_size.y / 2}, obj.col, name,
                              FindRenderedTextEnd(name));

    if (tabs[num].m_subtabs.size() > 0) {
        rotate_start();
        RenderArrow(window->DrawList, {bb.Max.x - s(22), bb.GetCenter().y - s(5)}, GetColor(ImGuiCol_Text),
                    ImGuiDir_Down, 0.65f);
        rotate_end(obj.rad, rotation_center());
    }

    window->DrawList->AddLine({bb.Min.x + s(40), window->DC.CursorPos.y}, {bb.Max.x, window->DC.CursorPos.y},
                              GetColor(ImGuiCol_Border, 0.5f));

    if (tabs[num].m_subtabs.size() > 0 && selected) {
        PushStyleVar(ImGuiStyleVar_ItemSpacing, {s(8), s(6)});
        Dummy({0, s(2)});

        for (size_t i = 0; i < tabs[num].m_subtabs.size(); ++i) {
            SetCursorPosX(GetCursorPosX() + s(40));
            ui::subtab((int)i, num);
        }

        Dummy({0, s(4)});
        PopStyleVar();
    }
}

void ui::subtab(int num, int tab_num) {
    auto name = tabs[tab_num].m_subtabs[num];
    auto name_size = ImGui::CalcTextSize(name, 0, 1);

    // Check if module is locked (experimental feature)
    Module* mod = getModuleForSubtab(tab_num, num);
    bool isLocked = mod && !mod->isAccessible();

    bool selected = tabs[tab_num].next_subtab == num;

    auto window = ImGui::GetCurrentWindow();
    auto id = window->GetID(name);

    // Larger subtab row with proper padding
    float rowHeight = s(20);
    float availWidth = window->Size.x - GetCursorPosX();
    ImRect bb{window->DC.CursorPos, window->DC.CursorPos + ImVec2(availWidth, rowHeight)};
    ItemAdd(bb, id);
    ItemSize(bb);

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    // Allow selection even for locked modules - renderPage shows unlock UI
    if (pressed && !selected) {
        tabs[tab_num].next_subtab = num;
        content_anim_dst = 0.f;
    }

    struct subtab_state {
        float anim;
        float hover;
        ImColor col;
    };
    auto& obj = anim_obj(name, 0, subtab_state{0.f, 0.f, 0});

    obj.anim = animate(obj.anim, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);

    // Hover background
    if (obj.hover > 0.01f && !selected) {
        window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColor(ImGuiCol_FrameBgHovered, obj.hover * 0.3f), s(4));
    }

    // Center text vertically
    float textY = bb.GetCenter().y - name_size.y / 2;

    if (isLocked) {
        // Grayed out look for locked modules
        ImU32 grayColor = IM_COL32(100, 100, 100, 150);
        window->DrawList->AddText(ImVec2(bb.Min.x, textY), grayColor, name, FindRenderedTextEnd(name));
    } else {
        obj.col = col_anim(col_anim(GetColor(ImGuiCol_TextDisabled), GetColor(ImGuiCol_Text), obj.hover),
                           GetSchemeColor(), obj.anim);
        window->DrawList->AddText(ImVec2(bb.Min.x, textY), obj.col, name, FindRenderedTextEnd(name));
    }
}

// ==================== APPLY STYLE ====================

void ui::apply_style() {
    theme::apply();
}
