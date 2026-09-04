#include "overlay.h"
#include "../../config/config.hpp"
#include "../../module/combat/piercing.h"
#include "../../module/module_manager.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../util/logger.hpp"
#include "../../util/team/friends_manager.h"
#include "../../util/textures/item_textures.h"
#include "../hud/arraylist.h"
#include "../hud/notifications.h"
#include "../hud/watermark.h"
#include "../menu/menu.h"
#include "../widgets/icons.hpp"
#include "../widgets/widgets.hpp"
#include "hud_fonts.h"
#include "../../jni/apollo/apollo_handshake.hpp"

// Embedded font data
#include "bytes.hpp"
#include "fonts/inter_bold.hpp"
#include "fonts/inter_regular.hpp"

#include <gl/GL.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui_impl_opengl2.h"
#include "../../../infra/ext/imgui/imgui_impl_win32.h"
#include "../../../infra/ext/minhook/minhook.h"
#include "../../util/math/worldToScreen.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// hud_fonts — pickable HUD families (see ui/core/hud_fonts.h for the contract).
// Lives here because the atlas is built here and this TU is already compiled.
// ---------------------------------------------------------------------------
namespace hud_fonts {
namespace {

struct SysCandidate {
    int id;                // stable, persisted in configs — never renumber
    const char* label;
    const wchar_t* file;   // relative to %WINDIR%\Fonts
};

// Faces that ship with Windows, so nothing has to be embedded or redistributed.
// Weight-heavy variants first: a HUD reads better in semibold/bold than in a
// text weight. Missing files are skipped silently.
constexpr SysCandidate kSysCandidates[] = {
    {2, "Segoe UI Semibold", L"seguisb.ttf"},
    {3, "Segoe UI Bold", L"segoeuib.ttf"},
    {4, "Segoe UI", L"segoeui.ttf"},
    {5, "Verdana Bold", L"verdanab.ttf"},
    {6, "Tahoma Bold", L"tahomabd.ttf"},
    {7, "Trebuchet MS Bold", L"trebucbd.ttf"},
    {8, "Arial Bold", L"arialbd.ttf"},
    {9, "Calibri Bold", L"calibrib.ttf"},
    {10, "Franklin Gothic", L"framd.ttf"},
    {11, "Consolas Bold", L"consolab.ttf"},
    {12, "Impact", L"impact.ttf"},
};

struct Entry {
    int id;
    const char* label;
    ImFont* font;
};

std::vector<Entry> g_entries;
std::vector<const char*> g_names;

// Rasterised once at 26px: the ArrayList draws at 24 and the Watermark at 19, so
// both downscale (soft) instead of upscaling (blurry). Horizontal oversampling
// only — vertical costs atlas area for no visible gain at these sizes.
constexpr float kExtraPixelSize = 26.0f;

bool fileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<char> toUtf8(const std::wstring& w) {
    std::vector<char> out;
    const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return out;
    out.resize((size_t)need);
    if (WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), need, nullptr, nullptr) <= 0)
        out.clear();
    return out;
}

}  // namespace

void Register(ImFontAtlas* atlas) {
    if (!atlas)
        return;

    // Always rebuilt, never skipped when non-empty: the overlay tears the ImGui
    // context down and calls SetupImGui again when the GL context changes, and the
    // ImFont pointers from the old atlas are dangling the moment that happens.
    g_entries.clear();
    g_names.clear();

    // The embedded faces are already in the atlas; entries 0/1 must stay these
    // two so previously saved ArrayList font values keep their meaning.
    ImFont* interRegular = atlas->Fonts.Size > 5 ? atlas->Fonts[5] : nullptr;
    ImFont* interBold = atlas->Fonts.Size > 2 ? atlas->Fonts[2] : nullptr;
    g_entries.push_back({kInterRegular, "Inter Regular", interRegular ? interRegular : interBold});
    g_entries.push_back({kInterBold, "Inter Bold", interBold});

    wchar_t winDir[MAX_PATH] = {};
    const UINT n = GetWindowsDirectoryW(winDir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        const std::wstring fontDir = std::wstring(winDir) + L"\\Fonts\\";

        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        // Left at the default (true): the atlas allocated this file's bytes and
        // must free them after the build, unlike the embedded static arrays.
        const ImWchar* ranges = atlas->GetGlyphRangesDefault();

        for (const SysCandidate& c : kSysCandidates) {
            const std::wstring path = fontDir + c.file;
            // Probed first because ImGui asserts on a font file it can't open.
            if (!fileExists(path))
                continue;
            std::vector<char> utf8 = toUtf8(path);
            if (utf8.empty())
                continue;
            if (ImFont* f = atlas->AddFontFromFileTTF(utf8.data(), kExtraPixelSize, &cfg, ranges))
                g_entries.push_back({c.id, c.label, f});
        }
    }

    g_names.clear();
    g_names.reserve(g_entries.size());
    for (const Entry& e : g_entries)
        g_names.push_back(e.label);
}

int Count() { return (int)g_entries.size(); }

const char* const* Names() { return g_names.empty() ? nullptr : g_names.data(); }

int IdAt(int comboIndex) {
    if (comboIndex < 0 || comboIndex >= (int)g_entries.size())
        return kInterRegular;
    return g_entries[(size_t)comboIndex].id;
}

int ComboIndexOf(int id) {
    for (size_t i = 0; i < g_entries.size(); ++i)
        if (g_entries[i].id == id)
            return (int)i;
    return 0;
}

ImFont* GetById(int id) {
    for (const Entry& e : g_entries)
        if (e.id == id && e.font)
            return e.font;
    for (const Entry& e : g_entries)
        if (e.font)
            return e.font;
    return nullptr;
}

}  // namespace hud_fonts

namespace {
std::atomic<bool> initStarted{false};
std::atomic<bool> hookInstalled{false};
std::atomic<bool> shutdownRequested{false};
std::atomic<bool> suppressPiercingRelease{false};

HGLRC lastOriginalContext = nullptr;
HDC lastHDC = nullptr;
HWND lastWindow = nullptr;
HWND targetWindow = nullptr;
HGLRC targetContext = nullptr;
int reinitCooldown = 0;
std::chrono::steady_clock::time_point overlaySuspendUntil{};
std::chrono::steady_clock::time_point contextChangeWindowStart{};
int contextChangeCount = 0;

typedef BOOL(__stdcall* wglSwapBuffers_t)(HDC hdc);
wglSwapBuffers_t originalSwapBuffers = nullptr;

WNDPROC originalWndProc = nullptr;

ImFont* defaultFont = nullptr;

#ifdef COBBLE_DEV_BUILD
// Persistent bottom-right notice so a development binary is never mistaken for a
// release one. Compiled out entirely of production (-Pack) builds.
void DrawDevBuildWarning() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl)
        return;
    const char* text = "THIS IS A DEV BUILD - DONT USE IN PRODUCTION";
    ImFont* f = ImGui::GetFont();
    if (!f)
        return;
    const float fs = ui::s(14.0f);
    const ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
    const float pad = ui::s(8.0f);
    const ImVec2 pos((float)RenderData::screenWidth - sz.x - pad,
                     (float)RenderData::screenHeight - sz.y - pad);
    dl->AddText(f, fs, ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 190), text);
    dl->AddText(f, fs, pos, IM_COL32(255, 80, 80, 235), text);
}
#endif

void SetupImGui() {
    Overlay::overlayContext = wglGetCurrentContext();
    if (!Overlay::overlayContext) {
        log_err("No GL context available for overlay");
        return;
    }

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
    fontConfig.FontDataOwnedByAtlas = false;  // Important: we own the data in static memory

    static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.FontDataOwnedByAtlas = false;
    icons_config.GlyphMinAdvanceX = 16.0f;

    // 0: Bold 18px + FontAwesome (Main Interface)
    io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 18.0f, &fontConfig);

    icons_config.GlyphMinAdvanceX = 18.0f;
    io.Fonts->AddFontFromMemoryTTF((void*)font_awesome_bin, sizeof(font_awesome_bin), 18.0f, &icons_config,
                                   icon_ranges);

    // 1: Regular 18px (Secondary Text)
    io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 18.0f, &fontConfig);

    // 2: Bold 22px (Headers)
    io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 22.0f, &fontConfig);

    // 3: Regular 22px
    io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 22.0f, &fontConfig);

    // 4: Notifications (Bold 18px)
    Overlay::notificationFont =
        io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 18.0f, &fontConfig);

    // 5: ArrayList Regular (Inter Regular 22px) - the clean geometric-sans look;
    //    the ArrayList's font picker swaps between this (Regular) and slot 2 (Bold).
    io.Fonts->AddFontFromMemoryTTF((void*)inter_regular_ttf_data, inter_regular_ttf_size, 22.0f, &fontConfig);

    // Slots 0-5 above are addressed by hardcoded index across the UI and must stay
    // exactly where they are. Everything the user can pick for a HUD element is
    // appended after them, from the system font directory.
    hud_fonts::Register(io.Fonts);

    defaultFont = io.Fonts->Fonts[0];

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.0f);

    Menu::ApplyStyle();

    ImGui_ImplWin32_Init(Overlay::gameWindow);
    ImGui_ImplOpenGL2_Init();

    RenderData::screenWidth = viewport[2];
    RenderData::screenHeight = viewport[3];

    // Warm-up frame to preload font textures
    {
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Menu::ApplyStyle();
        ImGui::Begin("##warmup", nullptr,
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav);
        ImGui::End();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    }

    Overlay::initialized = true;
    
    // Initialize item textures (embedded in DLL, no path needed)
    ItemTextureManager::get().init("");
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!Overlay::initialized || !originalWndProc) {
        return originalWndProc ? CallWindowProcA(originalWndProc, hWnd, msg, wParam, lParam)
                               : DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    // Don't toggle menu if a keybind binder is active (user is selecting a new key)
    if (msg == WM_KEYDOWN && wParam == Menu::menuKey && !ui::IsBinderActive()) {
        Menu::isOpen = !Menu::isOpen;

        if (Menu::isOpen) {
            mc.releaseMovementKeys();
            mc.ungrabMouse();  // Release mouse at LWJGL level to prevent camera movement
        } else {
            Menu::moduleToBind = nullptr;
            mc.restoreMovementKeys();
            // Only re-grab mouse if Minecraft doesn't have a GUI open
            // Otherwise we'd override the inventory/escape menu mouse state
            if (!mc.isInGui()) {
                mc.grabMouse();
            }
            // Save config when menu closes (debounced sync to server)
            ConfigManager::get().onSettingChanged();
        }
    }

    // Quick-add friend feature (when menu is closed and not in a GUI)
    if (!Menu::isOpen && friendsManager && friendsManager->quickAddEnabled) {
        bool quickAddPressed = false;
        int quickKey = friendsManager->quickAddKey;

        // Detect key/mouse press based on VK code
        if (quickKey == VK_MBUTTON && msg == WM_MBUTTONDOWN && (GetAsyncKeyState(VK_SHIFT) & 0x8000))
            quickAddPressed = true;
        else if (quickKey == VK_XBUTTON1 && msg == WM_XBUTTONDOWN && GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
            quickAddPressed = true;
        else if (quickKey == VK_XBUTTON2 && msg == WM_XBUTTONDOWN && GET_XBUTTON_WPARAM(wParam) == XBUTTON2)
            quickAddPressed = true;
        else if (msg == WM_KEYDOWN && (int)wParam == quickKey)
            quickAddPressed = true;

        if (quickAddPressed) {
            if (!mc.isInGui()) {
                // Get targeted player info
                PlayerInfo targetInfo = mc.getTargetedPlayerInfo();
                if (!targetInfo.uuid.empty()) {
                    // Check if already a friend
                    if (!friendsManager->isFriendByUUID(targetInfo.uuid)) {
                        // Use profileName if available, else name
                        std::string displayName =
                            targetInfo.profileName.empty() ? targetInfo.name : targetInfo.profileName;
                        friendsManager->addFriendByUUID(targetInfo.uuid, displayName);
                        // Save config
                        ConfigManager::get().onSettingChanged();
                    } else {
                        std::string displayName = friendsManager->getFriendDisplayName(targetInfo.uuid);
                        friendsManager->removeFriendByUUID(targetInfo.uuid);
                        NotificationManager::addSystem("Removed " + displayName);
                        ConfigManager::get().onSettingChanged();
                    }
                }
            }
        }
    }

    if (Menu::isOpen) {
        if (Menu::moduleToBind != nullptr) {
            // Persist the keybind the moment it's assigned/cleared. Previously
            // this only hit disk on menu close; a hard close (or a main-thread
            // freeze that stops INSERT/ESC from being processed) would lose it.
            if (msg == WM_KEYDOWN) {
                bool bound = false;
                if (wParam == VK_ESCAPE) {
                    Menu::moduleToBind->keybind = 0;
                    Menu::moduleToBind = nullptr;
                    bound = true;
                } else if (wParam != Menu::menuKey) {
                    Menu::moduleToBind->keybind = (int)wParam;
                    Menu::moduleToBind = nullptr;
                    bound = true;
                }
                if (bound)
                    ConfigManager::get().onSettingChanged();
                return 1;
            }
            switch (msg) {
                case WM_LBUTTONDOWN:
                    Menu::moduleToBind->keybind = VK_LBUTTON;
                    Menu::moduleToBind = nullptr;
                    ConfigManager::get().onSettingChanged();
                    return 1;
                case WM_RBUTTONDOWN:
                    Menu::moduleToBind->keybind = VK_RBUTTON;
                    Menu::moduleToBind = nullptr;
                    ConfigManager::get().onSettingChanged();
                    return 1;
                case WM_MBUTTONDOWN:
                    Menu::moduleToBind->keybind = VK_MBUTTON;
                    Menu::moduleToBind = nullptr;
                    ConfigManager::get().onSettingChanged();
                    return 1;
                case WM_XBUTTONDOWN: {
                    int xbutton = GET_XBUTTON_WPARAM(wParam);
                    int vk = (xbutton == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
                    Menu::moduleToBind->keybind = vk;
                    Menu::moduleToBind = nullptr;
                    ConfigManager::get().onSettingChanged();
                    return 1;
                }
            }
        }

        // ESC closes menu (but not if a keybind widget is active)
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && !ui::IsBinderActive()) {
            Menu::isOpen = false;
            Menu::moduleToBind = nullptr;
            ImGui::GetIO().MouseDrawCursor = false;
            mc.restoreMovementKeys();
            // Only re-grab mouse if Minecraft doesn't have a GUI open
            if (!mc.isInGui()) {
                mc.grabMouse();
            }
            ConfigManager::get().onSettingChanged();
            return 1;
        }

        // Let close/quit messages through (Alt+F4, close button, etc.)
        if (msg == WM_CLOSE || msg == WM_DESTROY || msg == WM_QUIT ||
            (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_CLOSE) || (msg == WM_SYSKEYDOWN && wParam == VK_F4)) {
            return CallWindowProcA(originalWndProc, hWnd, msg, wParam, lParam);
        }

        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return 1;
    }

    if (msg == WM_LBUTTONDOWN && piercing && piercing->handleAttackInput()) {
        suppressPiercingRelease = true;
        return 1;
    }
    if (msg == WM_LBUTTONUP && suppressPiercingRelease.exchange(false))
        return 1;

    return CallWindowProcA(originalWndProc, hWnd, msg, wParam, lParam);
}

BOOL __stdcall HookedSwapBuffers(HDC hdc) {
    if (shutdownRequested)
        return originalSwapBuffers(hdc);

    // Track whether the previous frame opened ImGui state but failed to close
    // it (typically because Overlay::Render or Menu::Render faulted and the
    // SEH below swallowed the exception). If so, drain it before NewFrame()
    // or ImGui asserts in Debug ("Forgot to call Render() or EndFrame()...")
    // and silently mis-tracks frame counts in Release.
    static bool s_frameLeaked = false;
    if (s_frameLeaked && Overlay::initialized) {
        COBBLE_TRACE("HookedSwapBuffers: draining leaked frame");
        __try {
            ImGui::EndFrame();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        s_frameLeaked = false;
    }

    bool frameStarted = false;
    __try {
        if (!hdc)
            return originalSwapBuffers(hdc);

        Overlay::deviceContext = hdc;
        Overlay::gameWindow = WindowFromDC(hdc);
        HGLRC currentOriginalContext = wglGetCurrentContext();

        if (!currentOriginalContext)
            return originalSwapBuffers(hdc);

        auto now = std::chrono::steady_clock::now();
        if (overlaySuspendUntil.time_since_epoch().count() != 0 && now < overlaySuspendUntil) {
            return originalSwapBuffers(hdc);
        }

        if (targetWindow && Overlay::gameWindow != targetWindow) {
            if (!IsWindow(targetWindow)) {
                HWND oldWindow = targetWindow;
                targetWindow = nullptr;
                targetContext = nullptr;
                lastOriginalContext = nullptr;
                lastHDC = nullptr;
                lastWindow = nullptr;

                if (originalWndProc && oldWindow) {
                    SetWindowLongPtrA(oldWindow, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
                    originalWndProc = nullptr;
                }

                if (Overlay::initialized) {
                    ImGui_ImplOpenGL2_Shutdown();
                    ImGui_ImplWin32_Shutdown();
                    ImGui::DestroyContext();
                    Overlay::overlayContext = nullptr;
                }
                Overlay::initialized = false;
            } else {
                return originalSwapBuffers(hdc);
            }
        }

        bool contextChanged = (targetContext != nullptr && targetContext != currentOriginalContext);

        if (contextChanged && Overlay::initialized) {
            log_debug("Overlay: GL context changed (old=0x%p new=0x%p), reinitializing...",
                      (void*)targetContext, (void*)currentOriginalContext);

            Menu::isOpen = false;
            Menu::moduleToBind = nullptr;

            ImGui_ImplOpenGL2_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            Overlay::overlayContext = nullptr;
            Overlay::initialized = false;
            reinitCooldown = 5;
            targetContext = nullptr;

            if (contextChangeWindowStart.time_since_epoch().count() == 0 ||
                std::chrono::duration_cast<std::chrono::milliseconds>(now - contextChangeWindowStart).count() > 1000) {
                contextChangeWindowStart = now;
                contextChangeCount = 0;
            }
            if (++contextChangeCount >= 3) {
                overlaySuspendUntil = now + std::chrono::milliseconds(2500);
                contextChangeCount = 0;
                log_warn("Overlay: frequent context switches, suspending overlay for 2.5s");
                return originalSwapBuffers(hdc);
            }

            // Give the driver time to stabilize the new context (fullscreen transitions)
            return originalSwapBuffers(hdc);
        }
        lastOriginalContext = currentOriginalContext;
        lastHDC = hdc;
        Overlay::originalContext = currentOriginalContext;

        if (!Overlay::initialized) {
            log_debug("Overlay: attempting SetupImGui (ctx=0x%p, wnd=0x%p)",
                      (void*)currentOriginalContext, (void*)Overlay::gameWindow);
            SetupImGui();
            if (Overlay::initialized && (!originalWndProc || lastWindow != Overlay::gameWindow)) {
                originalWndProc =
                    (WNDPROC)SetWindowLongPtrA(Overlay::gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
                lastWindow = Overlay::gameWindow;
                targetWindow = Overlay::gameWindow;
                log_ok("Overlay rendering ready");
            }
            if (Overlay::initialized && !targetContext) {
                targetContext = currentOriginalContext;
            }
        }

        if (!Overlay::initialized)
            return originalSwapBuffers(hdc);
        if (reinitCooldown > 0) {
            --reinitCooldown;
            return originalSwapBuffers(hdc);
        }

        // Validate GL context is still healthy before rendering
        GLenum glErr = glGetError();
        if (glErr != GL_NO_ERROR) {
            log_warn("Overlay: GL error 0x%X before render, skipping frame", glErr);
            return originalSwapBuffers(hdc);
        }

        RECT clientRect;
        GetClientRect(Overlay::gameWindow, &clientRect);
        int newWidth = clientRect.right - clientRect.left;
        int newHeight = clientRect.bottom - clientRect.top;

        if (newWidth > 0 && newHeight > 0 &&
            (newWidth != RenderData::screenWidth || newHeight != RenderData::screenHeight)) {
            RenderData::screenWidth = newWidth;
            RenderData::screenHeight = newHeight;
        }

        bool hasRenderWork = moduleManager && moduleManager->hasRenderWork();
        bool hasNotifications = NotificationManager::hasActive();
        bool hasArrayList = ArrayListRenderer::hasActive();
        // Watermark is an independent HUD element with its own on/off toggle.
        bool hasWatermark = Watermark::enabled;
        bool menuAnimating = Menu::openAnim > 0.01f;
        // Dev builds always render so the "dev build" warning is never hidden.
#ifdef COBBLE_DEV_BUILD
        constexpr bool hasDevWarning = true;
#else
        constexpr bool hasDevWarning = false;
#endif
        if (!Menu::isOpen && !menuAnimating && !hasRenderWork && !hasNotifications && !hasArrayList &&
            !hasWatermark && !hasDevWarning) {
            return originalSwapBuffers(hdc);
        }

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, RenderData::screenWidth, RenderData::screenHeight, 0, 1, -1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)RenderData::screenWidth, (float)RenderData::screenHeight);
        io.MouseDrawCursor = Menu::isOpen;

        // Auto-close menu on alt-tab. The watermark's file dialog runs on its own
        // thread and legitimately takes the foreground, so it is not an alt-tab:
        // closing the menu under it would drop the pick the user is about to make.
        if (Menu::isOpen && GetForegroundWindow() != Overlay::gameWindow && !Watermark::pickerBusy()) {
            Menu::isOpen = false;
            Menu::openAnim = 0.f;  // Skip close animation
            Menu::moduleToBind = nullptr;
            io.MouseDrawCursor = false;
            mc.restoreMovementKeys();
            ConfigManager::get().onSettingChanged();
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        frameStarted = true;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)RenderData::screenWidth, (float)RenderData::screenHeight));
        ImGui::Begin("##overlay", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoBackground);

        RenderData::Update();
        Overlay::Render();

        ImGui::End();

        Menu::Render();

        ImGui::EndFrame();
        frameStarted = false;
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // SEH unwinding can't run C++ destructors. Just record that the frame
        // is mid-flight; next invocation drains it before NewFrame() so ImGui
        // doesn't assert (Debug) or silently corrupt FrameCount (Release).
        if (frameStarted) {
            COBBLE_TRACE("HookedSwapBuffers: SEH caught mid-frame, marking leak");
            s_frameLeaked = true;
        } else {
            COBBLE_TRACE("HookedSwapBuffers: SEH caught pre-frame");
        }
    }

    return originalSwapBuffers(hdc);
}
}  // namespace

void Overlay::Init() {
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

void Overlay::Shutdown() {
    shutdownRequested = true;

    // Wait for any in-flight SwapBuffers calls to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Restore window procedure FIRST to stop input interception
    if (originalWndProc && (lastWindow || gameWindow)) {
        HWND targetWnd = lastWindow ? lastWindow : gameWindow;
        if (IsWindow(targetWnd)) {
            SetWindowLongPtrA(targetWnd, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
        }
        originalWndProc = nullptr;
        lastWindow = nullptr;
    }

    // Disable hooks before ImGui cleanup
    if (hookInstalled) {
        MH_DisableHook(MH_ALL_HOOKS);
        hookInstalled = false;
    }

    // Wait for hook to be fully disabled
    std::this_thread::sleep_for(std::chrono::milliseconds(16));

    if (initialized) {
        ImGui_ImplOpenGL2_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        overlayContext = nullptr;
    }

    MH_RemoveHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    initialized = false;
    initStarted = false;
}

void Overlay::Render() {
    moduleManager->onRender();
    ArrayListRenderer::render();
    Watermark::render();
    NotificationManager::update();
    NotificationManager::render();
#ifdef COBBLE_DEV_BUILD
    DrawDevBuildWarning();
#endif
}
