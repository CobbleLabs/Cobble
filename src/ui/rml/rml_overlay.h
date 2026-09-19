#pragma once

#include <Windows.h>

namespace RmlOverlay {

void Init(HWND hwnd, int width, int height);
void Shutdown();
void SetDimensions(int width, int height);
void Render();
void ProcessEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

}  // namespace RmlOverlay
