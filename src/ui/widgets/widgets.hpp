#pragma once

// ============================================================================
// UI Widgets - Unified Header
// ============================================================================
// This file consolidates all UI widgets and layout helpers.
// Include this header anywhere you need to render the UI.
// ============================================================================

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui_internal.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ==================== FONT ACCESSOR ====================
// Font indices: 0=Medium14+FA, 1=Bold14, 2=Medium17, 3=Bold17, 4=Notifications
#define font(n) ImGui::GetIO().Fonts->Fonts[n]

// ==================== COLOR SCHEME ====================
extern float g_SchemeColor[4];

inline ImU32 GetSchemeColor(float alpha = 1.0f);

// ==================== UI NAMESPACE ====================
namespace ui {

// ==================== SCALING ====================
inline float scale = 1.0f;
inline float baseHeight = 1080.0f;
inline float alpha_mod = 1.0f;

void updateScale();
float s(float value);
ImVec2 s(const ImVec2& v);

// Helper to get ImGuiCol with alpha applied
ImU32 GetColor(ImGuiCol idx, float alpha_mult = 1.0f);

// ==================== ANIMATION SYSTEM ====================

// Animation object storage - use for persistent animation state
template <typename T>
T& anim_obj(const char* label, int id, T default_value) {
    static std::unordered_map<std::string, T> storage;
    std::string key = std::string(label) + std::to_string(id);
    auto it = storage.find(key);
    if (it == storage.end()) {
        storage[key] = default_value;
        it = storage.find(key);
    }
    return it->second;
}

// Smooth float animation
float animate(float current, float min, float max, bool state);

// Smooth color animation
ImColor col_anim(ImU32 from, ImU32 to, float t);

// ==================== LAYOUT HELPERS ====================

// Two-column layout helper - standard module settings layout
void two_column(const char* leftTitle, std::function<void()> leftContent, const char* rightTitle,
                std::function<void()> rightContent);

// Single section with header
void section(const char* title, std::function<void()> content);

// Full-width section (single column)
void full_width(const char* title, std::function<void()> content);

// ==================== CHILD CONTAINERS ====================
void begin_child(const char* name, ImVec2 size = {0, 0});
void end_child();

// ==================== WIDGETS ====================

// Checkbox with optional keybind button
bool Checkbox(const char* label, bool* v);
bool CheckboxBind(const char* label, bool* v, int* key);

// Sliders
bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f",
                 bool allowDirectInput = false);
// Slider with linked clamping - value is clamped to [clamp_min, clamp_max] DURING drag
bool SliderFloat(const char* label, float* v, float v_min, float v_max, float* clamp_min, float* clamp_max,
                 const char* format = "%.1f");
bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d");

// Keybind capture
bool Binder(const char* label, int* key);
const char* GetKeyName(int vk);

// Color picker
bool color(const char* name, float col[4], bool alpha = true);

// Combo/dropdown
bool Combo(const char* label, int* current, const char* const items[], int count);

// ==================== TAB SYSTEM ====================

struct tab_t {
    const char* m_icon;
    const char* m_name;
    int cur_subtab = 0;
    int next_subtab = 0;
    std::vector<const char*> m_subtabs;

    tab_t(const char* icon, const char* name, std::vector<const char*> subtabs)
        : m_icon(icon), m_name(name), m_subtabs(subtabs) {}
};

inline int cur_tab = 0;
inline int next_tab = 0;
extern std::vector<tab_t> tabs;

// Animation values
inline float anim = 1.f;
inline float anim_dst = 1.f;
inline float content_anim = 1.f;
inline float content_anim_dst = 1.f;

void handle_anims();
void tab(int num);
void subtab(int num, int tab_num);

// ==================== STYLE ====================
void apply_style();

// Base window size (larger for better 1080p visibility)
inline ImVec2 baseSize{750, 530};
inline ImVec2 size{750, 530};
}  // namespace ui
