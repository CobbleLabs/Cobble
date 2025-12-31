#include "widgets.hpp"
#include "theme.hpp"
#include "icons.hpp"
#include <cmath>
#include <Windows.h>

using namespace ImGui;

// ==================== GLOBAL SCHEME COLOR ====================
float g_SchemeColor[4] = { 0.843f, 0.376f, 0.078f, 1.0f };

inline ImU32 GetSchemeColor(float alpha) {
    ImVec4 col = ImVec4(g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], g_SchemeColor[3]);
    col.w *= alpha * ImGui::GetStyle().Alpha * ui::alpha_mod;
    return ImGui::ColorConvertFloat4ToU32(col);
}

// ==================== TAB DEFINITIONS ====================
std::vector<ui::tab_t> ui::tabs = {
    { ICON_FA_CROSSHAIRS, "Combat", { "AutoClicker", "AimAssist", "AutoRefill", "Velocity", "Reach" } },
    { ICON_FA_EYE, "Visuals", { "NameTags", "ESP", "TestModule" } },
    { ICON_FA_ROCKET, "Movement", { "NoJumpDelay" } },
    { ICON_FA_GEAR, "Settings", { } }
};

// ==================== SCALING ====================

void ui::updateScale() {
    float screenHeight = ImGui::GetIO().DisplaySize.y;
    scale = screenHeight / baseHeight;
    if (scale < 0.6f) scale = 0.6f;
    if (scale > 2.0f) scale = 2.0f;
}

float ui::s(float value) { 
    return value * scale; 
}

ImVec2 ui::s(const ImVec2& v) { 
    return { v.x * scale, v.y * scale }; 
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

void ui::two_column(
    const char* leftTitle, std::function<void()> leftContent,
    const char* rightTitle, std::function<void()> rightContent
) {
    float halfWidth = (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2;
    
    ImGui::BeginGroup();
    {
        begin_child(leftTitle, { halfWidth, 0 });
        leftContent();
        end_child();
    }
    ImGui::EndGroup();
    
    ImGui::SameLine();
    
    ImGui::BeginGroup();
    {
        begin_child(rightTitle, { halfWidth, 0 });
        rightContent();
        end_child();
    }
    ImGui::EndGroup();
}

void ui::section(const char* title, std::function<void()> content) {
    float fullWidth = ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2;
    
    ImGui::BeginGroup();
    begin_child(title, { fullWidth, 0 });
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
        CalcItemSize(size, (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0), 
        0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetWindowPos(), 
        ImGui::GetWindowPos() + ImGui::GetWindowSize(), 
        GetColor(ImGuiCol_ChildBg), 
        GImGui->Style.ChildRounding
    );
    
    if (ImGui::CalcTextSize(name, 0, 1).x > 0) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetWindowPos(), 
            { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight }, 
            GetColor(ImGuiCol_Header), 
            GImGui->Style.ChildRounding, ImDrawFlags_RoundCornersTop
        );
        ImGui::GetWindowDrawList()->AddLine(
            { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + headerHeight }, 
            { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight }, 
            GetSchemeColor(0.5f)
        );
        ImGui::GetWindowDrawList()->AddText(
            font(1), font(1)->FontSize, 
            { ImGui::GetWindowPos().x + s(13), ImGui::GetWindowPos().y + headerHeight / 2 - font(1)->CalcTextSizeA(font(1)->FontSize, FLT_MAX, 0.f, name, ImGui::FindRenderedTextEnd(name)).y / 2 }, 
            GetColor(ImGuiCol_Text), name, ImGui::FindRenderedTextEnd(name)
        );
        ImGui::SetCursorPosY(headerHeight + 1);
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { s(13), s(15) });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { s(8), s(14) });
    ImGui::BeginChild(name, { -1, size.y == 0 ? size.y : -1 }, 0, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoBackground);
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
    ImRect bb(pos, { pos.x + square_sz, pos.y + square_sz });
    ImRect total_bb(pos, { pos.x + square_sz + name_size.x + GImGui->Style.ItemInnerSpacing.x, pos.y + square_sz });
    ItemAdd(total_bb, id);
    ItemSize(total_bb);
    
    bool hovered, held;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        MarkItemEdited(id);
    }
    
    struct cb_state { float anim; float hover; ImColor col; };
    auto& obj = anim_obj(label, 0, cb_state{ 0.f, 0.f, 0 });
    
    obj.anim = animate(obj.anim, 0.f, 1.f, *v);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered && !(*v));
    obj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.hover);
    
    draw->AddText({ total_bb.Max.x - name_size.x, bb.GetCenter().y - name_size.y / 2 }, GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddRectFilled(bb.Min, bb.Max, GetSchemeColor(obj.anim), style.FrameRounding);
    RenderCheckMark(draw, { bb.GetCenter().x - (checkSize * obj.anim) / 2, bb.GetCenter().y - (checkSize * obj.anim) / 2 }, GetColor(ImGuiCol_Text, obj.anim), checkSize * obj.anim);
    
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
    
    ImRect cbBox(pos, { pos.x + square_sz, pos.y + square_sz });
    ImRect cbTotal(pos, { pos.x + checkboxWidth, pos.y + height });
    
    bool cbHovered, cbHeld;
    ItemAdd(cbTotal, cbId);
    bool cbPressed = ButtonBehavior(cbTotal, cbId, &cbHovered, &cbHeld);
    if (cbPressed) {
        *v = !(*v);
        MarkItemEdited(cbId);
    }
    
    struct cb_s { float anim; float hover; ImColor col; };
    auto& cbObj = anim_obj(label, 0, cb_s{ 0.f, 0.f, 0 });
    cbObj.anim = animate(cbObj.anim, 0.f, 1.f, *v);
    cbObj.hover = animate(cbObj.hover, 0.f, 1.f, cbHovered && !(*v));
    cbObj.col = col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), cbObj.hover);
    
    draw->AddRectFilled(cbBox.Min, cbBox.Max, cbObj.col, style.FrameRounding);
    draw->AddRect(cbBox.Min, cbBox.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddRectFilled(cbBox.Min, cbBox.Max, GetSchemeColor(cbObj.anim), style.FrameRounding);
    RenderCheckMark(draw, { cbBox.GetCenter().x - (9 * cbObj.anim) / 2, cbBox.GetCenter().y - (9 * cbObj.anim) / 2 }, GetColor(ImGuiCol_Text, cbObj.anim), 9 * cbObj.anim);
    draw->AddText({ cbBox.Max.x + style.ItemInnerSpacing.x, cbBox.GetCenter().y - name_size.y / 2 }, GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));
    
    // Binder part
    std::string bindId = std::string(label) + "##bind";
    auto bId = window->GetID(bindId.c_str());
    
    ImRect bindBox(pos + ImVec2(availWidth - bindBoxWidth, (height - s(18.f)) / 2), pos + ImVec2(availWidth, (height + s(18.f)) / 2));
    ItemAdd(bindBox, bId);
    
    bool bindHovered = ItemHoverable(bindBox, bId, 0);
    bool bindActive = GetActiveID() == bId;
    
    struct bind_s { float anim; float hover; ImColor col; ImColor textCol; int framesSinceActive; };
    auto& bindObj = anim_obj(bindId.c_str(), 0, bind_s{ 0.f, 0.f, 0, 0, 0 });
    bindObj.anim = animate(bindObj.anim, 0.f, 1.f, bindActive);
    bindObj.hover = animate(bindObj.hover, 0.f, 1.f, bindHovered || bindActive);
    bindObj.col = col_anim(col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), bindObj.hover), GetColor(ImGuiCol_FrameBgActive), bindObj.anim);
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
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { *key = 0; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) { *key = VK_RBUTTON; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) { *key = VK_MBUTTON; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) { *key = VK_XBUTTON1; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) { *key = VK_XBUTTON2; ClearActiveID(); changed = true; }
            else {
                for (int vk = 0x08; vk <= 0x7B; vk++) {
                    if (vk == VK_ESCAPE) continue;
                    if (GetAsyncKeyState(vk) & 0x8000) { *key = vk; ClearActiveID(); changed = true; break; }
                }
            }
        }
    }
    
    const char* displayText = bindActive ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    draw->AddRectFilled(bindBox.Min, bindBox.Max, bindObj.col, style.FrameRounding);
    draw->AddRect(bindBox.Min, bindBox.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({ bindBox.GetCenter().x - textSize.x / 2, bindBox.GetCenter().y - textSize.y / 2 }, bindObj.textCol, displayText);
    
    ItemSize(ImVec2(availWidth, height));
    return cbPressed || changed;
}

// ==================== SLIDER FLOAT ====================

bool ui::SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();
    
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = CalcTextSize(label, 0, 1);
    ImVec2 frame_size = ImVec2(CalcItemWidth(), s(6));
    ImRect total_bb(pos, pos + ImVec2(CalcItemWidth(), label_size.y + style.ItemInnerSpacing.y * 1.25f + frame_size.y));
    ImRect bb(ImVec2(total_bb.Min.x, total_bb.Max.y - frame_size.y), ImVec2(total_bb.Max.x, total_bb.Max.y));
    
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
    
    struct slider_state { float grab; float anim; ImColor col; };
    auto& obj = anim_obj(label, 0, slider_state{ 0.f, 0.f, 0 });
    
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
    
    char value_buf[64];
    snprintf(value_buf, sizeof(value_buf), format, *v);
    
    window->DrawList->AddRectFilled(bb.Min, bb.Max, obj.col, 100);
    window->DrawList->AddRectFilled(bb.Min, { bb.Min.x + obj.grab * anim, bb.Max.y }, GetSchemeColor(), 100);
    
    float grab_radius = s(active ? 7.f : (hovered ? 5.f : 6.f));
    window->DrawList->AddCircleFilled({ bb.Min.x + obj.grab * anim, bb.GetCenter().y }, grab_radius, GetColor(ImGuiCol_Text), 64);
    
    window->DrawList->AddText(total_bb.Min, GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));
    window->DrawList->AddText({ total_bb.Max.x - CalcTextSize(value_buf).x, total_bb.Min.y }, GetColor(ImGuiCol_Text), value_buf);
    
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
    static const char* keyNames[256] = {
        "NONE", "LMB", "RMB", "BREAK", "MMB", "MB4", "MB5", "", "BACK", "TAB", 
        "", "", "CLEAR", "ENTER", "", "", "SHIFT", "CTRL", "ALT", "PAUSE",
        "CAPS", "", "", "", "", "", "", "ESC", "", "", "", "", 
        "SPACE", "PGUP", "PGDN", "END", "HOME", "LEFT", "UP", "RIGHT", "DOWN", 
        "", "", "", "PRTSC", "INS", "DEL", "", 
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
        "", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", 
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "LWIN", "RWIN", "", "", "", 
        "NUM0", "NUM1", "NUM2", "NUM3", "NUM4", "NUM5", "NUM6", "NUM7", "NUM8", "NUM9",
        "MUL", "ADD", "", "SUB", "DEC", "DIV", 
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"
    };
    
    if (vk >= 0 && vk < 124) {
        const char* name = keyNames[vk];
        if (name && name[0]) return name;
    }
    return "NONE";
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
    
    ImRect bb(pos + ImVec2(availWidth - boxWidth, (rowHeight - boxHeight) / 2), pos + ImVec2(availWidth, (rowHeight + boxHeight) / 2));
    ImRect totalBb(pos, pos + ImVec2(availWidth, rowHeight));
    
    ItemAdd(bb, id);
    ItemSize(totalBb);
    
    bool hovered = ItemHoverable(bb, id, 0);
    bool active = GetActiveID() == id;
    
    struct binder_state { float anim; float hover; ImColor col; ImColor textCol; int framesSinceActive; };
    auto& obj = anim_obj(label, 0, binder_state{ 0.f, 0.f, 0, 0, 0 });
    
    obj.anim = animate(obj.anim, 0.f, 1.f, active);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered || active);
    obj.col = col_anim(col_anim(GetColor(ImGuiCol_FrameBg), GetColor(ImGuiCol_FrameBgHovered), obj.hover), GetColor(ImGuiCol_FrameBgActive), obj.anim);
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
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { *key = 0; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) { *key = VK_RBUTTON; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) { *key = VK_MBUTTON; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) { *key = VK_XBUTTON1; ClearActiveID(); changed = true; }
            else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) { *key = VK_XBUTTON2; ClearActiveID(); changed = true; }
            else {
                for (int vk = 0x08; vk <= 0x7B; vk++) {
                    if (vk == VK_ESCAPE) continue;
                    if (GetAsyncKeyState(vk) & 0x8000) { *key = vk; ClearActiveID(); changed = true; break; }
                }
            }
        }
    }
    
    draw->AddText({ pos.x, pos.y + rowHeight / 2 - labelSize.y / 2 }, GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));
    
    const char* displayText = active ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({ bb.GetCenter().x - textSize.x / 2, bb.GetCenter().y - textSize.y / 2 }, obj.textCol, displayText);
    
    return changed;
}

// ==================== COLOR PICKER ====================

bool ui::color(const char* name, float col[4], bool alpha) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(name);
    auto draw = window->DrawList;
    auto pos = window->DC.CursorPos;
    
    ImVec2 size{ 16, 16 };
    auto label_size = CalcTextSize(name, 0, 1);
    
    ImRect bb(pos, { pos.x + CalcItemWidth() - size.x, pos.y + size.y });
    ItemAdd(bb, id);
    ItemSize(bb);
    
    draw->AddText({ bb.Min.x, bb.GetCenter().y - label_size.y / 2 }, GetColor(ImGuiCol_Text), name, FindRenderedTextEnd(name));
    
    bool value_changed = false;
    if (col) {
        pos = window->DC.CursorPos;
        window->DC.CursorPos = ImVec2{ bb.Max.x, bb.Min.y };
        
        ImGuiColorEditFlags flags = alpha 
            ? (ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar)
            : (ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
        
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
    ImVec2 name_size = font(2 + (selected ? 1 : 0))->CalcTextSizeA(font(2 + (selected ? 1 : 0))->FontSize, FLT_MAX, 0.f, name, FindRenderedTextEnd(name));
    
    ImVec2 tab_size{ window->Size.x, s(38) };
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + tab_size };
    ItemAdd(bb, id);
    ItemSize(bb);
    
    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    
    if (pressed && !selected) {
        ui::next_tab = num;
        ui::anim_dst = 0.f;
    }
    
    struct tab_state { float cur; float hover; float rad; ImColor col; };
    auto& obj = anim_obj(name, 0, tab_state{ 0.f, 0.f, 0.f, 0 });
    
    obj.cur = animate(obj.cur, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
    obj.rad = animate(obj.rad, 0.f, IM_PI, selected);
    obj.col = col_anim(col_anim(GetColor(ImGuiCol_TextDisabled), GetColor(ImGuiCol_Text, 0.75f), obj.hover), GetColor(ImGuiCol_Text), obj.cur);
    
    window->DrawList->AddText({ bb.Min.x + s(15), bb.GetCenter().y - icon_size.y / 2 }, obj.col, icon, FindRenderedTextEnd(icon));
    window->DrawList->AddText(font(2 + (selected ? 1 : 0)), font(2 + (selected ? 1 : 0))->FontSize, { bb.Min.x + s(40), bb.GetCenter().y - name_size.y / 2 }, obj.col, name, FindRenderedTextEnd(name));
    
    if (tabs[num].m_subtabs.size() > 0) {
        rotate_start();
        RenderArrow(window->DrawList, { bb.Max.x - s(22), bb.GetCenter().y - s(5) }, GetColor(ImGuiCol_Text), ImGuiDir_Down, 0.65f);
        rotate_end(obj.rad, rotation_center());
    }
    
    window->DrawList->AddLine({ bb.Min.x + s(40), window->DC.CursorPos.y }, { bb.Max.x, window->DC.CursorPos.y }, GetColor(ImGuiCol_Border, 0.5f));
    
    if (tabs[num].m_subtabs.size() > 0 && selected) {
        PushStyleVar(ImGuiStyleVar_ItemSpacing, { s(8), s(6) });
        Dummy({ 0, s(2) });
        
        for (size_t i = 0; i < tabs[num].m_subtabs.size(); ++i) {
            SetCursorPosX(GetCursorPosX() + s(40));
            ui::subtab((int)i, num);
        }
        
        Dummy({ 0, s(4) });
        PopStyleVar();
    }
}

void ui::subtab(int num, int tab_num) {
    auto name = tabs[tab_num].m_subtabs[num];
    auto name_size = ImGui::CalcTextSize(name, 0, 1);
    
    bool selected = tabs[tab_num].next_subtab == num;
    
    auto window = ImGui::GetCurrentWindow();
    auto id = window->GetID(name);
    
    ImRect bb{ window->DC.CursorPos, window->DC.CursorPos + name_size };
    ItemAdd(bb, id);
    ItemSize(bb);
    
    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    
    if (pressed && !selected) {
        tabs[tab_num].next_subtab = num;
        content_anim_dst = 0.f;
    }
    
    struct subtab_state { float anim; float hover; ImColor col; };
    auto& obj = anim_obj(name, 0, subtab_state{ 0.f, 0.f, 0 });
    
    obj.anim = animate(obj.anim, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
    obj.col = col_anim(col_anim(GetColor(ImGuiCol_TextDisabled), GetColor(ImGuiCol_Text), obj.hover), GetSchemeColor(), obj.anim);
    
    window->DrawList->AddText(bb.Min, obj.col, name, FindRenderedTextEnd(name));
}

// ==================== APPLY STYLE ====================

void ui::apply_style() {
    theme::apply();
}
