#pragma once

struct Module;

namespace Menu {
inline bool isOpen = false;
inline bool hidden = false;  // Temporarily hide menu visually (e.g. during HUD drag)
inline float openAnim = 0.f;
inline Module* moduleToBind = nullptr;
inline int menuKey = 0x2D;  // VK_INSERT (0x2D)

void ApplyStyle();
void Render();
void Hotkey(const char* label, Module* module);
}  // namespace Menu

// Module lookup by tab and subtab index (used by widgets for locked check)
Module* getModuleForSubtab(int tabIdx, int subtabIdx);
