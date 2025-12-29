#pragma once

#include <Windows.h>

struct ImFont;  // Forward declaration

// Overlay system for rendering ImGui over the game
// Uses wglSwapBuffers hook for OpenGL rendering
namespace Overlay
{
	void Init();
	void Shutdown();
	
	// Called from the hook to render modules
	void Render();
	
	// State
	inline bool initialized = false;
	inline HWND gameWindow = nullptr;
	inline HDC deviceContext = nullptr;
	inline HGLRC originalContext = nullptr;
	inline HGLRC overlayContext = nullptr;
	
	// Fonts
	inline ImFont* notificationFont = nullptr;
}
