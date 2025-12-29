#include "overlay.h"
#include "notifications.h"
#include "../util/logger.hpp"
#include "../sdk/render_data.h"
#include "../module/module_manager.h"

#include <gl/GL.h>
#include <mutex>
#include <atomic>

#include "../../ext/minhook/minhook.h"
#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_impl_win32.h"
#include "../../ext/imgui/imgui_impl_opengl2.h"
#include "../util/math/worldToScreen.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
	std::once_flag setupFlag;
	std::atomic<bool> initStarted{false};
	std::atomic<bool> hookInstalled{false};
	
	// Original functions
	typedef BOOL(__stdcall* wglSwapBuffers_t)(HDC hdc);
	wglSwapBuffers_t originalSwapBuffers = nullptr;
	
	WNDPROC originalWndProc = nullptr;
	
	// ImGui font
	ImFont* defaultFont = nullptr;
	
	void SetupImGui()
	{
		Overlay::overlayContext = wglCreateContext(Overlay::deviceContext);
		if (!Overlay::overlayContext) {
			log_err("Failed to create overlay GL context");
			return;
		}
		
		wglMakeCurrent(Overlay::deviceContext, Overlay::overlayContext);
		
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		
		glOrtho(0, viewport[2], viewport[3], 0, 1, -1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glClearColor(0, 0, 0, 0);
		
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		io.MouseDrawCursor = false;
		io.WantCaptureMouse = false;
		io.WantCaptureKeyboard = false;
		
		// Load system font (Segoe UI - Windows default, clean and modern)
		const char* systemFontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
		Overlay::notificationFont = io.Fonts->AddFontFromFileTTF(systemFontPath, 18.0f);
		if (!Overlay::notificationFont) {
			// Fallback to default font
			Overlay::notificationFont = io.Fonts->AddFontDefault();
		}
		
		defaultFont = io.Fonts->AddFontDefault();
		
		// Dark theme
		ImVec4* colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.0f);
		
		ImGui_ImplWin32_Init(Overlay::gameWindow);
		ImGui_ImplOpenGL2_Init();
		
		// Store screen dimensions
		RenderData::screenWidth = viewport[2];
		RenderData::screenHeight = viewport[3];
		
		Overlay::initialized = true;
	}
	
	LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return CallWindowProcA(originalWndProc, hWnd, msg, wParam, lParam);
	}
	
	BOOL __stdcall HookedSwapBuffers(HDC hdc)
	{
		__try {
			if (!hdc) return originalSwapBuffers(hdc);
			
			Overlay::deviceContext = hdc;
			Overlay::gameWindow = WindowFromDC(hdc);
			Overlay::originalContext = wglGetCurrentContext();
			
			if (!Overlay::originalContext) return originalSwapBuffers(hdc);
			
			std::call_once(setupFlag, [&] {
				SetupImGui();
				if (Overlay::initialized) {
					originalWndProc = (WNDPROC)SetWindowLongPtrA(Overlay::gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
					log_ok("Overlay rendering ready");
				}
			});
			
			if (!Overlay::initialized) return originalSwapBuffers(hdc);
			
			// Get window size BEFORE switching context
			RECT clientRect;
			GetClientRect(Overlay::gameWindow, &clientRect);
			int newWidth = clientRect.right - clientRect.left;
			int newHeight = clientRect.bottom - clientRect.top;
			
			// Detect resolution/fullscreen change
			if (newWidth > 0 && newHeight > 0 && 
				(newWidth != RenderData::screenWidth || newHeight != RenderData::screenHeight)) {
				char buf[64];
				snprintf(buf, sizeof(buf), "Overlay: resolution changed to %dx%d", newWidth, newHeight);
				log_debug(buf);
				
				RenderData::screenWidth = newWidth;
				RenderData::screenHeight = newHeight;
			}
			
			wglMakeCurrent(Overlay::deviceContext, Overlay::overlayContext);
			
			// Update GL projection to match current screen size
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, RenderData::screenWidth, RenderData::screenHeight, 0, 1, -1);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			
			// Force ImGui to use current screen size
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2((float)RenderData::screenWidth, (float)RenderData::screenHeight);
			
			// Begin ImGui frame
			ImGui_ImplOpenGL2_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			
			// Create fullscreen overlay window using current display size
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2((float)RenderData::screenWidth, (float)RenderData::screenHeight));
			ImGui::Begin("##overlay", nullptr,
				ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoInputs |
				ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoBackground);
			
			// Call module render functions
			Overlay::Render();
			
			ImGui::End();
			
			// Finish ImGui frame
			ImGui::EndFrame();
			ImGui::Render();
			ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
			
			wglMakeCurrent(Overlay::deviceContext, Overlay::originalContext);
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			// Silently continue if rendering fails
		}
		
		return originalSwapBuffers(hdc);
	}
}

void Overlay::Init()
{
	// Prevent double init
	bool expected = false;
	if (!initStarted.compare_exchange_strong(expected, true)) {
		log_warn("Overlay::Init() already called, skipping");
		return;
	}
	
	MH_STATUS status = MH_Initialize();
	if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
		log_err("Failed to initialize MinHook");
		return;
	}
	
	LPVOID swapBuffers = (LPVOID)GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglSwapBuffers");
	if (!swapBuffers) {
		log_err("Failed to find wglSwapBuffers");
		return;
	}
	
	if (MH_CreateHook(swapBuffers, (LPVOID)HookedSwapBuffers, (LPVOID*)&originalSwapBuffers) != MH_OK) {
		log_err("Failed to create wglSwapBuffers hook");
		return;
	}
	
	if (MH_EnableHook(swapBuffers) != MH_OK) {
		log_err("Failed to enable wglSwapBuffers hook");
		return;
	}
	
	hookInstalled = true;
	log_ok("Overlay hooks installed");
}

void Overlay::Shutdown()
{
	if (hookInstalled) {
		MH_DisableHook(MH_ALL_HOOKS);
		MH_RemoveHook(MH_ALL_HOOKS);
		MH_Uninitialize();
	}
	
	if (initialized) {
		wglMakeCurrent(deviceContext, overlayContext);
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		wglMakeCurrent(deviceContext, originalContext);
		wglDeleteContext(overlayContext);
	}
	
	if (originalWndProc && gameWindow) {
		SetWindowLongPtrA(gameWindow, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
	}
	
	RenderData::Destroy();
	
	initialized = false;
	hookInstalled = false;
	initStarted = false;
}

void Overlay::Render()
{
	// Modules with onRender() will be called here
	moduleManager.onRender();
	
	// Render notifications
	NotificationManager::update();
	NotificationManager::render();
}
