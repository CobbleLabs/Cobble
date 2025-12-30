#pragma once
/*
 * Weave-style UI System for Cobble
 * Provides animation helpers and themed widgets
 */

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_internal.h"
#include "imgui_tricks.hpp"
#include "hashes.hpp"

#include <string>
#include <vector>
#include <functional>

using namespace ImGui;

// ==================== FONT ACCESSOR ====================
// Font indices: 0=Medium14+FA, 1=Bold14, 2=Medium17, 3=Bold17, 4=Notifications
#define font(n) ImGui::GetIO().Fonts->Fonts[n]

// ==================== COLOR SCHEME ====================
// Global scheme color (orange)
inline float g_SchemeColor[4] = { 0.843f, 0.376f, 0.078f, 1.0f }; // ImColor(215, 96, 20) - Orange

namespace ui { inline float alpha_mod = 1.0f; }

inline ImU32 GetSchemeColor(float alpha = 1.0f) {
    ImVec4 col = ImVec4(g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], g_SchemeColor[3]);
    col.w *= alpha * ImGui::GetStyle().Alpha * ui::alpha_mod; // Apply style alpha and global mod
    return ImGui::ColorConvertFloat4ToU32(col);
}

inline ImU32 GetSchemeColor(ImVec4 col) {
    col.w *= ImGui::GetStyle().Alpha;
    return ImGui::ColorConvertFloat4ToU32(col);
}



// ==================== TAB STRUCTURE ====================
struct tab_t {
    const char* m_icon;
    const char* m_name;
    
    int cur_subtab = 0;
    int next_subtab = 0;
    std::vector<const char*> m_subtabs;
    std::vector<std::function<void()>> pages;
    
    tab_t(const char* icon, const char* name, std::vector<const char*> subtabs) {
        m_icon = icon;
        m_name = name;
        m_subtabs = subtabs;
    }
};

// ==================== UI NAMESPACE ====================
namespace ui {

    // Scaling system - base resolution is 1080p
    inline float scale = 1.0f;
    inline float baseHeight = 1080.0f;
    
    // Global alpha modifier for manual animations (controlled by menu open/close)

    
    // Helper to get ImGuiCol with alpha applied from style AND global modifier
    inline ImU32 GetColor(ImGuiCol idx, float alpha_mult = 1.0f) {
        ImVec4 col = ImGui::GetStyle().Colors[idx];
        col.w *= alpha_mult * ImGui::GetStyle().Alpha * alpha_mod;
        return ImGui::ColorConvertFloat4ToU32(col);
    }
    
    // Calculate scale based on screen height (call once per frame or on resolution change)
    inline void updateScale() {
        float screenHeight = ImGui::GetIO().DisplaySize.y;
        scale = screenHeight / baseHeight;
        // Clamp to reasonable range
        if (scale < 0.6f) scale = 0.6f;
        if (scale > 2.0f) scale = 2.0f;
    }
    
    // Scaled value helper
    inline float s(float value) { return value * scale; }
    inline ImVec2 s(const ImVec2& v) { return { v.x * scale, v.y * scale }; }
    
    // Base window size (at 1080p)
    inline ImVec2 baseSize{ 670, 478 };
    inline ImVec2 size{ 670, 478 }; // Updated each frame with scaling
    
    // Animation values
    inline float anim = 1.f;
    inline float anim_dst = 1.f;
    inline float content_anim = 1.f;
    inline float content_anim_dst = 1.f;
    void handle_anims();
    
    // Page system
    void render_page();
    void add_page(int tab_idx, std::function<void()> c);
    
    // Rotation helpers
    void rotate_start();
    ImVec2 rotation_center();
    void rotate_end(float rad, ImVec2 center);
    
    // Tab state
    inline int cur_tab = 0;
    inline int next_tab = 0;
    inline std::vector<tab_t> tabs = {
        { ICON_FA_CROSSHAIRS, "Combat", { "AutoClicker", "AimAssist", "AutoRefill", "HitSelect" } },
        { ICON_FA_EYE, "Visuals", { "NameTags", "ESP", "World" } },
        { ICON_FA_GEAR, "Settings", { } }
    };
    
    // Widgets
    void tab(int num);
    void subtab(int num, int tab_num);
    void begin_child(const char* name, ImVec2 size);
    void end_child();
    bool color(const char* name, float col[4], bool alpha = true);
    
    // Custom styled widgets (weave theme overrides)
    bool Checkbox(const char* label, bool* v);
    bool CheckboxBind(const char* label, bool* v, int* key); // Checkbox + Binder in same line
    bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f");
    bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d");
    
    // Binder widget - for keybinds (weave style)
    bool Binder(const char* label, int* key);
    
    // Helper to get key name from virtual key code
    const char* GetKeyName(int vk);
    
    // Apply weave style to ImGui
    void apply_style();
}
