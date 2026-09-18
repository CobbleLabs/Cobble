#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace VapePreviewHost {
inline constexpr int kClientWidth = 800;
inline constexpr int kClientHeight = 480;
inline constexpr UINT kCloseMenuMessage = WM_APP + 0x37A;
inline constexpr UINT kRestoreGameFocusMessage = WM_APP + 0x37D;

// Starts (or reparents) the fixed-size WebView2 host. Safe to call repeatedly.
void Start(HWND parentWindow);

// Makes Start/SetVisible terminally reject new work before Overlay disables its
// hook. Shutdown then drains pending WebView creation callbacks and joins the STA.
void BeginShutdown();
void Shutdown();

// These functions only enqueue state for the WebView STA thread.
void SetVisible(bool visible);
void UpdateBounds(int parentWidth, int parentHeight);
void SetMenuKey(int virtualKey);

// The browser owns the menu only after the page has loaded and runtime geometry
// has been measured at exactly 800x480. Until then the existing menu is fallback.
bool HandlesMenu();
bool IsReady();
}  // namespace VapePreviewHost
