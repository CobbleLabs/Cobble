#pragma once

// ============================================================================
// Cobble Menu - Click GUI Framework
// ============================================================================

struct Module;

namespace Menu {
    inline bool isOpen = false;
    inline float openAnim = 0.f;
    inline Module* moduleToBind = nullptr;
    
    void ApplyStyle();
    void Render();
    void Hotkey(const char* label, Module* module);
}
