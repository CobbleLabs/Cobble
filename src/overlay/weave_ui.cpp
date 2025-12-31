#include "weave_ui.hpp"
#include <cmath>
#include <Windows.h>

// ==================== ANIMATION HANDLING ====================

static int rotation_start_index;

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

void ui::render_page() {
    if (cur_tab >= tabs.size()) return;
    if (tabs[cur_tab].pages.size() == 0 || tabs[cur_tab].pages.size() <= tabs[cur_tab].cur_subtab)
        return;
    
    tabs[cur_tab].pages[tabs[cur_tab].cur_subtab]();
}

void ui::add_page(int tab_idx, std::function<void()> c) {
    if (tab_idx < tabs.size())
        tabs[tab_idx].pages.emplace_back(c);
}

// ==================== ROTATION HELPERS ====================

void ui::rotate_start() {
    rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
}

ImVec2 ui::rotation_center() {
    ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX);
    
    const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);
    
    return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2);
}

void ui::rotate_end(float rad, ImVec2 center) {
    float s = sin(rad), c = cos(rad);
    center = ImRotate(center, s, c) - center;
    
    auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
    for (int i = rotation_start_index; i < buf.Size; i++)
        buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
}

// ==================== TAB WIDGET ====================

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
    
    struct tab_state {
        float cur;
        float hover;
        float rad;
        ImColor col;
    };
    auto& obj = anim_obj(name, 0, tab_state{ 0.f, 0.f, 0.f, 0 });
    
    obj.cur = animate(obj.cur, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
    obj.rad = animate(obj.rad, 0.f, IM_PI, selected);
    obj.col = col_anim(col_anim(ui::GetColor(ImGuiCol_TextDisabled), ui::GetColor(ImGuiCol_Text, 0.75f), obj.hover), ui::GetColor(ImGuiCol_Text), obj.cur);
    
    // Draw icon and name
    window->DrawList->AddText({ bb.Min.x + s(15), bb.GetCenter().y - icon_size.y / 2 }, obj.col, icon, FindRenderedTextEnd(icon));
    window->DrawList->AddText(font(2 + (selected ? 1 : 0)), font(2 + (selected ? 1 : 0))->FontSize, { bb.Min.x + s(40), bb.GetCenter().y - name_size.y / 2 }, obj.col, name, FindRenderedTextEnd(name));
    
    // Draw arrow if has subtabs
    if (tabs[num].m_subtabs.size() > 0) {
        ui::rotate_start();
        RenderArrow(window->DrawList, { bb.Max.x - s(22), bb.GetCenter().y - s(5) }, ui::GetColor(ImGuiCol_Text), ImGuiDir_Down, 0.65f);
        ui::rotate_end(obj.rad, ui::rotation_center());
    }
    
    // Draw separator line
    window->DrawList->AddLine({ bb.Min.x + s(40), window->DC.CursorPos.y }, { bb.Max.x, window->DC.CursorPos.y }, ui::GetColor(ImGuiCol_Border, 0.5f));
    
    // Render subtabs directly below (only when selected)
    if (tabs[num].m_subtabs.size() > 0 && selected) {
        PushStyleVar(ImGuiStyleVar_ItemSpacing, { s(8), s(6) });
        Dummy({ 0, s(2) }); // Small spacing
        
        for (size_t i = 0; i < tabs[num].m_subtabs.size(); ++i) {
            // Indent subtabs
            SetCursorPosX(GetCursorPosX() + s(40));
            ui::subtab((int)i, num);
        }
        
        Dummy({ 0, s(4) }); // Bottom spacing
        PopStyleVar();
    }
}

// ==================== SUBTAB WIDGET ====================

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
    
    struct s {
        float anim;
        float hover;
        ImColor col;
    };
    auto& obj = anim_obj(name, 0, s{ 0.f, 0.f, 0 });
    
    obj.anim = animate(obj.anim, 0.f, 1.f, selected);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
    obj.col = col_anim(col_anim(ui::GetColor(ImGuiCol_TextDisabled), ui::GetColor(ImGuiCol_Text), obj.hover), GetSchemeColor(), obj.anim);
    
    window->DrawList->AddText(bb.Min, obj.col, name, FindRenderedTextEnd(name));
}

// ==================== CHILD WIDGET ====================

void ui::begin_child(const char* name, ImVec2 size) {
    float headerHeight = s(33.f);
    
    ImGui::BeginChild(std::string(name).append("decor").c_str(), 
        CalcItemSize(size, (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 0), 
        0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), GetColor(ImGuiCol_ChildBg), GImGui->Style.ChildRounding);
    
    if (ImGui::CalcTextSize(name, 0, 1).x > 0) {
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight }, GetColor(ImGuiCol_Header), GImGui->Style.ChildRounding, ImDrawFlags_RoundCornersTop);
        ImGui::GetWindowDrawList()->AddLine({ ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + headerHeight }, { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + headerHeight }, GetSchemeColor(0.5f));
        ImGui::GetWindowDrawList()->AddText(font(1), font(1)->FontSize, { ImGui::GetWindowPos().x + s(13), ImGui::GetWindowPos().y + headerHeight / 2 - font(1)->CalcTextSizeA(font(1)->FontSize, FLT_MAX, 0.f, name, ImGui::FindRenderedTextEnd(name)).y / 2 }, GetColor(ImGuiCol_Text), name, ImGui::FindRenderedTextEnd(name));
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
    
    draw->AddText({ bb.Min.x, bb.GetCenter().y - label_size.y / 2 }, ui::GetColor(ImGuiCol_Text), name, FindRenderedTextEnd(name));
    
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

// ==================== CUSTOM CHECKBOX ====================

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
    
    struct s {
        float anim;
        float hover;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, s{ 0.f, 0.f, 0 });
    
    obj.anim = animate(obj.anim, 0.f, 1.f, *v);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered && !(*v));
    obj.col = col_anim(ui::GetColor(ImGuiCol_FrameBg), ui::GetColor(ImGuiCol_FrameBgHovered), obj.hover);
    
    // Draw label
    draw->AddText({ total_bb.Max.x - name_size.x, bb.GetCenter().y - name_size.y / 2 }, ui::GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));
    
    // Draw checkbox background
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, ui::GetColor(ImGuiCol_Border), style.FrameRounding);
    
    // Draw scheme overlay when checked
    draw->AddRectFilled(bb.Min, bb.Max, GetSchemeColor(obj.anim), style.FrameRounding);
    
    // Draw checkmark
    RenderCheckMark(draw, { bb.GetCenter().x - (checkSize * obj.anim) / 2, bb.GetCenter().y - (checkSize * obj.anim) / 2 }, ui::GetColor(ImGuiCol_Text, obj.anim), checkSize * obj.anim);
    
    return pressed;
}

// ==================== CHECKBOX WITH BIND ====================
// Checkbox "Enabled" + Binder button on the same line: [✓] Enabled      | R |

bool ui::CheckboxBind(const char* label, bool* v, int* key) {
    auto window = GetCurrentWindow();
    auto style = GetStyle();
    auto draw = window->DrawList;
    
    float availWidth = CalcItemWidth();
    float bindBoxWidth = s(50.0f);
    float spacing = s(8.0f);
    float checkboxWidth = availWidth - bindBoxWidth - spacing;
    
    ImVec2 pos = window->DC.CursorPos;
    float height = s(20.f);  // Increased height for both checkbox and binder
    float square_sz = s(18.f);
    
    // === CHECKBOX PART ===
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
    cbObj.col = col_anim(ui::GetColor(ImGuiCol_FrameBg), ui::GetColor(ImGuiCol_FrameBgHovered), cbObj.hover);
    
    // Draw checkbox
    draw->AddRectFilled(cbBox.Min, cbBox.Max, cbObj.col, style.FrameRounding);
    draw->AddRect(cbBox.Min, cbBox.Max, ui::GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddRectFilled(cbBox.Min, cbBox.Max, GetSchemeColor(cbObj.anim), style.FrameRounding);
    RenderCheckMark(draw, { cbBox.GetCenter().x - (9 * cbObj.anim) / 2, cbBox.GetCenter().y - (9 * cbObj.anim) / 2 }, ui::GetColor(ImGuiCol_Text, cbObj.anim), 9 * cbObj.anim);
    
    // Draw label next to checkbox
    draw->AddText({ cbBox.Max.x + style.ItemInnerSpacing.x, cbBox.GetCenter().y - name_size.y / 2 }, ui::GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));
    
    // === BINDER PART ===
    std::string bindId = std::string(label) + "##bind";
    auto bId = window->GetID(bindId.c_str());
    
    const char* keyName = GetKeyName(*key);
    ImVec2 keySize = CalcTextSize(keyName, 0, 1);
    
    ImRect bindBox(pos + ImVec2(availWidth - bindBoxWidth, (height - s(18.f)) / 2), pos + ImVec2(availWidth, (height + s(18.f)) / 2));
    
    // Must add item for proper hit testing
    ItemAdd(bindBox, bId);
    
    bool bindHovered = ItemHoverable(bindBox, bId, 0);
    bool bindActive = GetActiveID() == bId;
    
    struct bind_s { float anim; float hover; ImColor col; ImColor textCol; int framesSinceActive; };
    auto& bindObj = anim_obj(bindId.c_str(), 0, bind_s{ 0.f, 0.f, 0, 0, 0 });
    bindObj.anim = animate(bindObj.anim, 0.f, 1.f, bindActive);
    bindObj.hover = animate(bindObj.hover, 0.f, 1.f, bindHovered || bindActive);
    bindObj.col = col_anim(col_anim(ui::GetColor(ImGuiCol_FrameBg), ui::GetColor(ImGuiCol_FrameBgHovered), bindObj.hover), ui::GetColor(ImGuiCol_FrameBgActive), bindObj.anim);
    bindObj.textCol = col_anim(ui::GetColor(ImGuiCol_Text), GetSchemeColor(), bindObj.anim);
    
    // Handle bind click - activate on click
    if (bindHovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        SetActiveID(bId, window);
        FocusWindow(window);
        bindObj.framesSinceActive = 0;
    }
    
    // Cancel if clicking elsewhere
    if (bindActive && !bindHovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }
    
    bool changed = false;
    if (bindActive) {
        bindObj.framesSinceActive++;
        
        // Wait a few frames before capturing keys to avoid initial click
        if (bindObj.framesSinceActive > 5) {
            // Check escape to unbind
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { 
                *key = 0; 
                ClearActiveID(); 
                changed = true; 
            }
            // Check mouse buttons (but not left since that's for clicking)
            else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) { 
                *key = VK_RBUTTON; 
                ClearActiveID(); 
                changed = true; 
            }
            else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) { 
                *key = VK_MBUTTON; 
                ClearActiveID(); 
                changed = true; 
            }
            else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) { 
                *key = VK_XBUTTON1; 
                ClearActiveID(); 
                changed = true; 
            }
            else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) { 
                *key = VK_XBUTTON2; 
                ClearActiveID(); 
                changed = true; 
            }
            // Check keyboard keys
            else {
                for (int vk = 0x08; vk <= 0x7B; vk++) {
                    if (vk == VK_ESCAPE) continue; // Already handled
                    if (GetAsyncKeyState(vk) & 0x8000) { 
                        *key = vk; 
                        ClearActiveID(); 
                        changed = true; 
                        break; 
                    }
                }
            }
        }
    }
    
    // Draw bind button
    const char* displayText = bindActive ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    draw->AddRectFilled(bindBox.Min, bindBox.Max, bindObj.col, style.FrameRounding);
    draw->AddRect(bindBox.Min, bindBox.Max, ui::GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({ bindBox.GetCenter().x - textSize.x / 2, bindBox.GetCenter().y - textSize.y / 2 }, bindObj.textCol, displayText);
    
    // Size the whole row
    ItemSize(ImVec2(availWidth, height));
    
    return cbPressed || changed;
}

// ==================== CUSTOM SLIDER FLOAT ====================

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
    
    // Release active state when mouse released
    if (active && IsMouseReleased(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }
    
    // Animation state
    struct slider_state {
        float grab;
        float anim;
        ImColor col;
    };
    auto& obj = anim_obj(label, 0, slider_state{ 0.f, 0.f, 0 });
    
    // Calculate slider position from current value
    float t = (*v - v_min) / (v_max - v_min);
    t = ImClamp(t, 0.0f, 1.0f);
    float grab_pos = t * bb.GetWidth();
    
    // Handle drag - update value from mouse position
    if (active) {
        float mouse_rel = ImClamp((GetMousePos().x - bb.Min.x) / bb.GetWidth(), 0.0f, 1.0f);
        *v = v_min + mouse_rel * (v_max - v_min);
        MarkItemEdited(id);
        t = (*v - v_min) / (v_max - v_min);
        grab_pos = t * bb.GetWidth();
        
        // Smooth animation while dragging
        obj.grab = ImLerp(obj.grab, grab_pos, 24.f * GetIO().DeltaTime);
    } else {
        // Snap directly to position when not dragging (no trailing animation)
        obj.grab = grab_pos;
    }
    
    obj.anim = animate(obj.anim, 0.f, 1.f, hovered || active);
    obj.col = col_anim(ui::GetColor(ImGuiCol_FrameBg), ui::GetColor(ImGuiCol_FrameBgHovered), obj.anim);
    
    // Format value text
    char value_buf[64];
    snprintf(value_buf, sizeof(value_buf), format, *v);
    
    // Draw
    window->DrawList->AddRectFilled(bb.Min, bb.Max, obj.col, 100);
    window->DrawList->AddRectFilled(bb.Min, { bb.Min.x + obj.grab * anim, bb.Max.y }, GetSchemeColor(), 100);
    
    float grab_radius = s(active ? 7.f : (hovered ? 5.f : 6.f));
    window->DrawList->AddCircleFilled({ bb.Min.x + obj.grab * anim, bb.GetCenter().y }, grab_radius, ui::GetColor(ImGuiCol_Text), 64);
    
    window->DrawList->AddText(total_bb.Min, ui::GetColor(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));
    window->DrawList->AddText({ total_bb.Max.x - CalcTextSize(value_buf).x, total_bb.Min.y }, ui::GetColor(ImGuiCol_Text), value_buf);
    
    return active;
}

// ==================== CUSTOM SLIDER INT ====================

bool ui::SliderInt(const char* label, int* v, int v_min, int v_max, const char* format) {
    float val = (float)*v;
    // Fix: Underlying SliderFloat uses snprintf with float value. 
    // Passing %d (int format) causes undefined behavior/garbage output.
    // Use %.0f to display integer value correctly from float.
    bool changed = ui::SliderFloat(label, &val, (float)v_min, (float)v_max, "%.0f");
    *v = (int)val;
    return changed;
}

// ==================== BINDER WIDGET ====================

// Key name lookup table
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

bool ui::Binder(const char* label, int* key) {
    auto window = GetCurrentWindow();
    auto id = window->GetID(label);
    auto style = GetStyle();
    auto draw = window->DrawList;
    
    const char* keyName = GetKeyName(*key);
    ImVec2 keyNameSize = CalcTextSize(keyName, 0, 1);
    ImVec2 labelSize = CalcTextSize(label, 0, 1);
    
    // Same height and width as CheckboxBind binder
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
    
    // Animation state
    struct binder_state {
        float anim;
        float hover;
        ImColor col;
        ImColor textCol;
        int framesSinceActive;
    };
    auto& obj = anim_obj(label, 0, binder_state{ 0.f, 0.f, 0, 0, 0 });
    
    obj.anim = animate(obj.anim, 0.f, 1.f, active);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered || active);
    obj.col = col_anim(col_anim(ui::GetColor(ImGuiCol_FrameBg), ui::GetColor(ImGuiCol_FrameBgHovered), obj.hover), ui::GetColor(ImGuiCol_FrameBgActive), obj.anim);
    obj.textCol = col_anim(ui::GetColor(ImGuiCol_Text), GetSchemeColor(), obj.anim);
    
    // Handle click to start binding
    if (hovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        SetActiveID(id, window);
        FocusWindow(window);
        obj.framesSinceActive = 0;
    }
    
    // Cancel if clicking elsewhere
    if (active && !hovered && IsMouseClicked(ImGuiMouseButton_Left)) {
        ClearActiveID();
    }
    
    bool changed = false;
    
    // Capture key when active
    if (active) {
        obj.framesSinceActive++;
        
        // Wait a few frames before capturing keys to avoid initial click
        if (obj.framesSinceActive > 5) {
            // Check escape to unbind
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                *key = 0;
                ClearActiveID();
                changed = true;
            }
            // Check mouse buttons
            else if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                *key = VK_RBUTTON;
                ClearActiveID();
                changed = true;
            } 
            else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                *key = VK_MBUTTON;
                ClearActiveID();
                changed = true;
            }
            else if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) {
                *key = VK_XBUTTON1;
                ClearActiveID();
                changed = true;
            }
            else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) {
                *key = VK_XBUTTON2;
                ClearActiveID();
                changed = true;
            }
            // Check keyboard keys
            else {
                for (int vk = 0x08; vk <= 0x7B; vk++) {
                    if (vk == VK_ESCAPE) continue;
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        *key = vk;
                        ClearActiveID();
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
    
    // Draw label on left
    draw->AddText({ pos.x, pos.y + rowHeight / 2 - labelSize.y / 2 }, ui::GetColor(ImGuiCol_Text), label, FindRenderedTextEnd(label));
    
    // Draw binder button on right
    const char* displayText = active ? "..." : GetKeyName(*key);
    ImVec2 textSize = CalcTextSize(displayText, 0, 1);
    
    draw->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding);
    draw->AddRect(bb.Min, bb.Max, ui::GetColor(ImGuiCol_Border), style.FrameRounding);
    draw->AddText({ bb.GetCenter().x - textSize.x / 2, bb.GetCenter().y - textSize.y / 2 }, obj.textCol, displayText);
    
    return changed;
}

// ==================== APPLY STYLE ====================

void ui::apply_style() {
    auto& style = ImGui::GetStyle();
    
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 4.0f;
    
    style.WindowPadding = { 0, 0 };
    style.FramePadding = { 8, 6 };
    style.ItemSpacing = { 8, 8 };
    style.ItemInnerSpacing = { 8, 4 };
    
    ImVec4* colors = style.Colors;
    
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);           // White text
    colors[ImGuiCol_TextDisabled] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);   // 100/255 gray
    colors[ImGuiCol_WindowBg] = ImVec4(0.063f, 0.063f, 0.063f, 1.00f);    // 16/255 almost black
    colors[ImGuiCol_ChildBg] = ImVec4(0.078f, 0.078f, 0.078f, 1.00f);     // 20/255 dark gray
    colors[ImGuiCol_PopupBg] = ImVec4(0.122f, 0.122f, 0.122f, 1.00f);     // 31/255
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);         // Subtle white border
    colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);   // Border shadow
    
    colors[ImGuiCol_FrameBg] = ImVec4(0.063f, 0.063f, 0.063f, 1.00f);     // 16/255
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.082f, 0.082f, 0.082f, 1.00f); // 21/255
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);  // 26/255
    
    colors[ImGuiCol_Header] = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);      // 26/255
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.122f, 0.122f, 0.122f, 1.00f); // 31/255  
    colors[ImGuiCol_HeaderActive] = ImVec4(0.141f, 0.141f, 0.141f, 1.00f);  // 36/255
    
    colors[ImGuiCol_Button] = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);      // 26/255
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.122f, 0.122f, 0.122f, 1.00f); // 31/255
    colors[ImGuiCol_ButtonActive] = ImVec4(0.141f, 0.141f, 0.141f, 1.00f);  // 36/255
    
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);        // Transparent
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.110f, 0.110f, 0.110f, 1.00f); // 28/255
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.129f, 0.129f, 0.129f, 1.00f); // 33/255
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.149f, 0.149f, 0.149f, 1.00f);  // 38/255
    
    // Scheme color (orange) for checkmarks, sliders, etc.
    colors[ImGuiCol_CheckMark] = ImVec4(g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], 1.0f);
}
