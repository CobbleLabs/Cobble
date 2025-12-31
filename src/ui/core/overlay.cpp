#include "overlay.h"
#include "../hud/notifications.h"
#include "../hud/arraylist.h"
#include "../menu/menu.h"
#include "../widgets/widgets.hpp"
#include "../widgets/icons.hpp"
#include "../../util/logger.hpp"
#include "../../sdk/render_data.h"
#include "../../sdk/minecraft.h"
#include "../../module/module_manager.h"

// Embedded font data
#include "bytes.hpp"

#include <gl/GL.h>
#include <mutex>
#include <atomic>
#include <thread>

#include "../../../ext/minhook/minhook.h"
#include "../../../ext/imgui/imgui.h"
#include "../../../ext/imgui/imgui_impl_win32.h"
#include "../../../ext/imgui/imgui_impl_opengl2.h"
#include "../../util/math/worldToScreen.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
	std::atomic<bool> initStarted{false};
	std::atomic<bool> hookInstalled{false};
	std::atomic<bool> shutdownRequested{false};
	
	HGLRC lastOriginalContext = nullptr;
	HDC lastHDC = nullptr;
	
	typedef BOOL(__stdcall* wglSwapBuffers_t)(HDC hdc);
	wglSwapBuffers_t originalSwapBuffers = nullptr;
	
	WNDPROC originalWndProc = nullptr;
	
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
		
		ImFontConfig fontConfig;
		fontConfig.OversampleH = 3;
		fontConfig.OversampleV = 3;
		
		static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		icons_config.PixelSnapH = true;
		icons_config.FontDataOwnedByAtlas = false;
		icons_config.GlyphMinAdvanceX = 16.0f;
		
		const char* boldFontPath = "C:\\Windows\\Fonts\\segoeuib.ttf";
		
		// 0: Bold 18px + FontAwesome
		if (io.Fonts->AddFontFromFileTTF(boldFontPath, 18.0f, &fontConfig) == nullptr) {
			io.Fonts->AddFontDefault();
		}
		icons_config.GlyphMinAdvanceX = 18.0f;
		io.Fonts->AddFontFromMemoryTTF((void*)font_awesome_bin, sizeof(font_awesome_bin), 18.0f, &icons_config, icon_ranges);
		
		// 1: Bold 18px
		io.Fonts->AddFontFromFileTTF(boldFontPath, 18.0f, &fontConfig);
		
		// 2: Bold 22px
		io.Fonts->AddFontFromFileTTF(boldFontPath, 22.0f, &fontConfig);
		
		// 3: Bold 22px
		io.Fonts->AddFontFromFileTTF(boldFontPath, 22.0f, &fontConfig);
		
		// 4: Notifications
		Overlay::notificationFont = io.Fonts->AddFontFromFileTTF(boldFontPath, 18.0f, &fontConfig);
		
		defaultFont = io.Fonts->Fonts[0];
		
		ImVec4* colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.0f);
		
		Menu::ApplyStyle();
		
		ImGui_ImplWin32_Init(Overlay::gameWindow);
		ImGui_ImplOpenGL2_Init();
		
		RenderData::screenWidth = viewport[2];
		RenderData::screenHeight = viewport[3];
		
		Overlay::initialized = true;
	}
	
	LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_KEYDOWN && wParam == VK_INSERT) {
			Menu::isOpen = !Menu::isOpen;
			
			if (Menu::isOpen) {
				mc.releaseMovementKeys();
			} else {
				Menu::moduleToBind = nullptr;
				mc.restoreMovementKeys();
			}
		}
		
		if (Menu::isOpen) {
			if (Menu::moduleToBind != nullptr) {
				if (msg == WM_KEYDOWN) {
					if (wParam == VK_ESCAPE) {
						Menu::moduleToBind->keybind = 0;
						Menu::moduleToBind = nullptr;
					} else if (wParam != VK_INSERT) {
						Menu::moduleToBind->keybind = (int)wParam;
						Menu::moduleToBind = nullptr;
					}
					return 1;
				}
				switch (msg) {
					case WM_LBUTTONDOWN:
						Menu::moduleToBind->keybind = VK_LBUTTON;
						Menu::moduleToBind = nullptr;
						return 1;
					case WM_RBUTTONDOWN:
						Menu::moduleToBind->keybind = VK_RBUTTON;
						Menu::moduleToBind = nullptr;
						return 1;
					case WM_MBUTTONDOWN:
						Menu::moduleToBind->keybind = VK_MBUTTON;
						Menu::moduleToBind = nullptr;
						return 1;
					case WM_XBUTTONDOWN: {
						int xbutton = GET_XBUTTON_WPARAM(wParam);
						Menu::moduleToBind->keybind = (xbutton == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
						Menu::moduleToBind = nullptr;
						return 1;
					}
				}
			}
			
			if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
				Menu::isOpen = false;
				Menu::moduleToBind = nullptr;
				ImGui::GetIO().MouseDrawCursor = false;
				return 1;
			}
			
			ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
			return 1;
		}

		return CallWindowProcA(originalWndProc, hWnd, msg, wParam, lParam);
	}
	
	BOOL __stdcall HookedSwapBuffers(HDC hdc)
	{
		if (shutdownRequested) return originalSwapBuffers(hdc);
		
		__try {
			if (!hdc) return originalSwapBuffers(hdc);
			
			Overlay::deviceContext = hdc;
			Overlay::gameWindow = WindowFromDC(hdc);
			HGLRC currentOriginalContext = wglGetCurrentContext();
			
			if (!currentOriginalContext) return originalSwapBuffers(hdc);
			
			bool contextChanged = (lastOriginalContext != nullptr && lastOriginalContext != currentOriginalContext);
			bool hdcChanged = (lastHDC != nullptr && lastHDC != hdc);
			
			if ((contextChanged || hdcChanged) && Overlay::initialized) {
				log_debug("Overlay: GL context/HDC changed, reinitializing...");
				
				Menu::isOpen = false;
				Menu::moduleToBind = nullptr;
				
				if (Overlay::overlayContext) {
					wglMakeCurrent(hdc, Overlay::overlayContext);
					ImGui_ImplOpenGL2_Shutdown();
					ImGui_ImplWin32_Shutdown();
					ImGui::DestroyContext();
					wglDeleteContext(Overlay::overlayContext);
					Overlay::overlayContext = nullptr;
				}
				Overlay::initialized = false;
				originalWndProc = nullptr;
			}
			lastOriginalContext = currentOriginalContext;
			lastHDC = hdc;
			Overlay::originalContext = currentOriginalContext;
			
			if (!Overlay::initialized) {
				SetupImGui();
				if (Overlay::initialized && !originalWndProc) {
					originalWndProc = (WNDPROC)SetWindowLongPtrA(Overlay::gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
					log_ok("Overlay rendering ready");
				}
			}
			
			if (!Overlay::initialized) return originalSwapBuffers(hdc);
			
			RECT clientRect;
			GetClientRect(Overlay::gameWindow, &clientRect);
			int newWidth = clientRect.right - clientRect.left;
			int newHeight = clientRect.bottom - clientRect.top;
			
			if (newWidth > 0 && newHeight > 0 && 
				(newWidth != RenderData::screenWidth || newHeight != RenderData::screenHeight)) {
				RenderData::screenWidth = newWidth;
				RenderData::screenHeight = newHeight;
			}
			
			wglMakeCurrent(Overlay::deviceContext, Overlay::overlayContext);
			
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, RenderData::screenWidth, RenderData::screenHeight, 0, 1, -1);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2((float)RenderData::screenWidth, (float)RenderData::screenHeight);
			io.MouseDrawCursor = Menu::isOpen;
			
			ImGui_ImplOpenGL2_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			
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
			
			Overlay::Render();
			
			ImGui::End();
			
			Menu::Render();
			
			ImGui::EndFrame();
			ImGui::Render();
			ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
			
			wglMakeCurrent(Overlay::deviceContext, Overlay::originalContext);
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
		}
		
		return originalSwapBuffers(hdc);
	}
}

void Overlay::Init()
{
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
	shutdownRequested = true;
	
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	
	if (hookInstalled) {
		MH_DisableHook(MH_ALL_HOOKS);
		MH_RemoveHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		hookInstalled = false;
	}
	
	if (originalWndProc && gameWindow) {
		SetWindowLongPtrA(gameWindow, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
		originalWndProc = nullptr;
	}
	
	initialized = false;
	initStarted = false;
}

void Overlay::Render()
{
	moduleManager->onRender();
	ArrayListRenderer::render();
	NotificationManager::update();
	NotificationManager::render();
}
