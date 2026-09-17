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
#include <utility>
#include <vector>

// ==================== FONT ACCESSOR ====================
// Font indices: 0=Medium14+FA, 1=Bold14, 2=Medium17, 3=Bold17, 4=Notifications
#define font(n) ImGui::GetIO().Fonts->Fonts[n]

// ==================== COLOR SCHEME ====================
extern float (&g_SchemeColor)[4];

inline ImU32 GetSchemeColor(float alpha = 1.0f);
ImU32 GetAccentRGB();  // accent (or rainbow) at full opacity, no fade applied

// ==================== UI NAMESPACE ====================
namespace ui {

// ==================== SCALING ====================
inline float scale = 1.0f;
inline float alpha_mod = 1.0f;

void updateScale();
float s(float value);
ImVec2 s(const ImVec2& v);

// Helper to get ImGuiCol with alpha applied
ImU32 GetColor(ImGuiCol idx, float alpha_mult = 1.0f);

// ==================== ANIMATION SYSTEM ====================

// Animation object storage - use for persistent animation state.
//
// Originally backed by std::unordered_map, but on the GL render thread inside
// a DLL injected via JNI we hit consistent SEH faults inside find() on a
// freshly-default-constructed map. The hashtable internals (buckets / proxy
// nodes / hash) misbehave in this environment — possibly a magic-statics /
// _Init_thread_header timing issue across threads, possibly something in
// MSVC's static-CRT STL that doesn't survive the manual-loader env we run in.
// Switched to a plain vector of pairs: O(N) lookup, but N is the total widget
// count per UI screen (small, < 100) and we only call this once per widget per
// frame, so the constant-factor cost is irrelevant compared to the actual
// rendering. Vector default-ctor is empty and allocates nothing, sidestepping
// whatever unordered_map's first-call init was tripping on.
template <typename T>
T& anim_obj(const char* label, int id, T default_value) {
    static std::vector<std::pair<std::string, T>> storage;
    std::string key = std::string(label) + std::to_string(id);
    for (auto& entry : storage) {
        if (entry.first == key) return entry.second;
    }
    storage.emplace_back(std::move(key), std::move(default_value));
    return storage.back().second;
}

// Smooth float animation
float animate(float current, float min, float max, bool state);

// Smooth color animation
ImColor col_anim(ImU32 from, ImU32 to, float t);

// ==================== LAYOUT HELPERS ====================

// Hover-description context. The menu sets this to the currently rendered
// module so repeated labels such as "Mode" and "Range" remain specific.
//
// allowTips also decides whether hover tips draw at all: only pages that are a
// module's own settings pass true. Pages that are chrome (the module grid, Settings,
// Configs) still set a context — labels resolve — but stay silent on hover.
void SetDescriptionContext(const char* context, bool allowTips = false);
const char* GetModuleDescription(const char* module);
void HoverDescription(const char* key, bool hovered, const char* explicitText = nullptr, bool rainbow = false);
void DescribeLastItem(const char* key, const char* explicitText = nullptr, bool rainbow = false);

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
bool IsBinderActive();    // Returns true if any keybind widget is currently capturing input
void ResetBinderState();  // Call at start of each frame

// Color picker
bool color(const char* name, float col[4], bool alpha = true);

// Combo/dropdown
bool Combo(const char* label, int* current, const char* const items[], int count);

// Hover tooltip explaining tick timing. Call immediately after any widget whose
// value is measured in (or per) game ticks, so users know what a tick is.
inline void TickTooltip() {
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Measured in game ticks.\n"
                          "The game runs at 20 ticks per second, so 1 tick = 50 ms.\n"
                          "e.g. 4 ticks = 200 ms, 20 ticks = 1 second.");
    }
}

// ==================== TAB SYSTEM ====================

// Per-category view state: a Module_Category either shows its module list or a
// selected module's config page. Animated identically to cur/next_subtab via
// content_anim (committed at the animation midpoint in handle_anims).
enum class CategoryView { List, Config };

struct tab_t {
    const char* m_icon;
    const char* m_name;
    int cur_subtab = 0;
    int next_subtab = 0;

    // Per-category view state (List vs Config). In-class default initializers
    // keep the existing constructor signature unchanged.
    CategoryView cur_view = CategoryView::List;
    CategoryView next_view = CategoryView::List;

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

// ui::tab() publishes the selected row's screen rect so the sidebar can draw
// one rail that glides between rows instead of each row owning a highlight.
inline ImVec2 nav_sel_min{0, 0};
inline ImVec2 nav_sel_max{0, 0};
inline bool nav_sel_valid = false;

void apply_style();

inline ImVec2 baseSize{745, 520};
inline ImVec2 size{745, 520};
}  // namespace ui
