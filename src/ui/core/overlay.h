#pragma once

// ============================================================================
// Overlay - Core rendering and hook management
// ============================================================================

#include <Windows.h>

namespace Overlay {
// Initialization and cleanup
void Init();
// False means a presentation callback could not be drained; caller must keep
// the DLL loaded rather than unloading code that may still be executing.
bool Shutdown();

// Render the overlay content (modules, HUD elements)
void Render();

// State
inline bool initialized = false;
inline HWND gameWindow = nullptr;
inline HDC deviceContext = nullptr;
inline HGLRC originalContext = nullptr;
inline HGLRC overlayContext = nullptr;

// Fonts
inline void* notificationFont = nullptr;
}  // namespace Overlay
