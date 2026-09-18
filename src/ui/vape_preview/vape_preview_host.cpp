#include "vape_preview_host.h"
#include "vape_preview_assets.h"

#include "../../util/logger.hpp"

#include <unknwn.h>
#include <wrl.h>
#include "../../../infra/ext/webview2/include/WebView2.h"

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace VapePreviewHost {
namespace {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

enum class HostState {
    Stopped,
    Starting,
    Ready,
    Failed,
    Stopping,
};

constexpr UINT kApplyStateMessage = WM_APP + 0x37B;
constexpr UINT kShutdownMessage = WM_APP + 0x37C;
constexpr wchar_t kVirtualHost[] = L"cobble-vape-ui.local";
constexpr wchar_t kVirtualUrl[] = L"https://cobble-vape-ui.local/index.html";

// This changes only the standalone preview harness. The 800x480 #client and
// every one of its rules, templates and transitions still come directly from
// the original checked-in files.
// The page ships a strict CSP (style-src 'self'), which blocks any injected
// <style> element. So every adjustment is done through the CSSOM
// (element.style.setProperty), which the CSP does not restrict. This only
// hides the standalone preview harness (ambient scene, safety ribbon, hint) and
// pins the client to its true 800x480 layout — no original rule, template or
// animation of #client is modified.
constexpr wchar_t kEmbeddedMountScript[] = LR"JS(
(() => {
  const setImp = (el, prop, val) => { if (el) el.style.setProperty(prop, val, "important"); };

  const applyHarness = () => {
    const html = document.documentElement;
    const body = document.body;
    for (const el of [html, body]) {
      setImp(el, "width", "800px");
      setImp(el, "height", "480px");
      setImp(el, "min-width", "800px");
      setImp(el, "min-height", "480px");
      setImp(el, "margin", "0");
      setImp(el, "overflow", "hidden");
      setImp(el, "background", "transparent");
    }
    setImp(document.querySelector(".ambient"), "display", "none");
    setImp(document.querySelector(".safety-ribbon"), "display", "none");
    setImp(document.querySelector(".preview-hint"), "display", "none");
    const shell = document.querySelector(".preview-shell");
    setImp(shell, "display", "block");
    setImp(shell, "width", "800px");
    setImp(shell, "height", "480px");
    setImp(shell, "min-height", "480px");
    setImp(shell, "padding", "0");
    setImp(shell, "margin", "0");
    const viewport = document.querySelector(".client-viewport");
    setImp(viewport, "width", "800px");
    setImp(viewport, "height", "480px");
  };

  // fitClient() scales #client only so the harness fits inside a browser tab.
  // Inside Minecraft it must render at its true 800x480, so hold transform at
  // none and reassert it whenever app.js sets an inline scale again.
  const clampClientScale = () => {
    const client = document.querySelector("#client");
    if (client && client.style.transform !== "none")
      client.style.setProperty("transform", "none", "important");
  };

  const mount = () => {
    applyHarness();
    clampClientScale();
    const client = document.querySelector("#client");
    if (client) {
      new MutationObserver(clampClientScale).observe(client, {
        attributes: true,
        attributeFilter: ["style"]
      });
    }
    // app.js re-renders sections into #content; reassert the harness for a short
    // while so nothing reintroduces the preview chrome or a fit scale.
    let ticks = 0;
    const interval = setInterval(() => {
      applyHarness();
      if (++ticks > 30) clearInterval(interval);
    }, 100);
    window.addEventListener("resize", () => window.requestAnimationFrame(() => {
      applyHarness();
      clampClientScale();
    }), true);
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", mount, { once: true });
  } else {
    mount();
  }

  // Let the original preview consume Escape when a sidecar is open. With no
  // sidecar, return it to Cobble's existing mouse/key lifecycle.
  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") return;
    const sidecarOpen = document.querySelector("#client")?.classList.contains("is-sidecar-open");
    if (sidecarOpen) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    window.chrome?.webview?.postMessage("close-menu");
  }, true);
})();
)JS";

// offsetWidth/offsetHeight reflect the layout box (unaffected by the harness'
// transform), so they confirm the untouched CSS still lays #client out at
// 800x480. transform must resolve to none so what is painted is 1:1.
constexpr wchar_t kVerifyGeometryScript[] = LR"JS(
(() => {
  const client = document.querySelector("#client");
  if (!client) return JSON.stringify({ ok: false, reason: "no-client" });
  const cs = getComputedStyle(client);
  const okLayout = client.offsetWidth === 800 && client.offsetHeight === 480;
  const okTransform = cs.transform === "none";
  return JSON.stringify({
    ok: okLayout && okTransform,
    offsetW: client.offsetWidth,
    offsetH: client.offsetHeight,
    transform: cs.transform,
    dpr: window.devicePixelRatio
  });
})()
)JS";

using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);

std::mutex g_lifecycleMutex;
std::condition_variable g_shutdownCondition;
bool g_shutdownInProgress = false;
std::thread g_hostThread;
std::atomic<HostState> g_state{HostState::Stopped};
std::atomic<DWORD> g_hostThreadId{0};
std::atomic<bool> g_acceptUiWork{true};
std::atomic<bool> g_stopRequested{false};
std::atomic<bool> g_desiredVisible{false};
std::atomic<int> g_parentWidth{kClientWidth};
std::atomic<int> g_parentHeight{kClientHeight};
std::atomic<int> g_menuKey{VK_INSERT};
std::atomic<HWND> g_requestedParent{nullptr};
std::atomic<HWND> g_controlWindow{nullptr};

constexpr wchar_t kControlWindowClass[] = L"CobbleVapePreviewControl";
constexpr wchar_t kOverlayWindowClass[] = L"CobbleVapePreviewOverlay";
constexpr UINT_PTR kRepositionTimerId = 1;

// The WebView lives in this host-owned top-level window, not as a child of the
// Minecraft HWND. That avoids cross-thread reparenting when Minecraft recreates
// its window (fullscreen<->windowed) and lets the UI float above the game.
HWND g_overlayWindow = nullptr;
HMODULE g_loaderModule = nullptr;
CreateEnvironmentFn g_createEnvironment = nullptr;
std::filesystem::path g_sessionRoot;
std::filesystem::path g_assetsFolder;
std::filesystem::path g_profileFolder;
std::filesystem::path g_loaderPath;

ComPtr<ICoreWebView2Environment> g_environment;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;
bool g_currentVisible = false;
RECT g_lastBounds{0, 0, 0, 0};
int g_pendingCreations = 0;  // Accessed only by the WebView STA thread.

std::string toUtf8(const wchar_t* text) {
    if (!text)
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

// Control messages go to a dedicated message-only window rather than the thread
// queue: PostThreadMessage messages have no target HWND and can be dropped when
// WebView2 pumps a nested/modal loop, which would strand shutdown.
void postHostMessage(UINT message) {
    const HWND control = g_controlWindow.load(std::memory_order_acquire);
    if (control)
        PostMessageW(control, message, 0, 0);
}

void applyControllerState();

// Runs on the STA thread when an async environment/controller creation returns.
// WebView2 creation cannot be cancelled, so shutdown must wait for it; re-posting
// the shutdown message once the last one finishes lets the control window quit
// the loop instead of stranding the join.
void notePendingCreationFinished() {
    if (g_pendingCreations > 0)
        --g_pendingCreations;
    if (g_pendingCreations == 0 && g_stopRequested.load(std::memory_order_acquire))
        postHostMessage(kShutdownMessage);
}

void repositionOverlayWindow();

LRESULT CALLBACK ControlWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kApplyStateMessage) {
        applyControllerState();
        return 0;
    }
    if (message == kShutdownMessage) {
        // Wait for any uncancelable WebView2 creation to finish; the completion
        // handler re-posts this message once the last one lands.
        if (g_pendingCreations <= 0)
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK OverlayWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // While visible, keep tracking Minecraft's position/size (it can move in
    // windowed mode) and stay on top.
    if (message == WM_TIMER && wParam == kRepositionTimerId) {
        if (g_currentVisible)
            repositionOverlayWindow();
        return 0;
    }
    // Never take activation away from the game.
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}

void postParentMessage(UINT message) {
    const HWND parent = g_requestedParent.load(std::memory_order_acquire);
    if (IsWindow(parent))
        PostMessageW(parent, message, 0, 0);
}

void failHost(const char* stage, HRESULT result) {
    if (g_controller)
        g_controller->put_IsVisible(FALSE);
    g_currentVisible = false;
    if (g_state.load(std::memory_order_acquire) != HostState::Stopping)
        g_state.store(HostState::Failed, std::memory_order_release);
    postParentMessage(kRestoreGameFocusMessage);
    log_err("Vape Preview: %s failed (HRESULT=0x%08lX)", stage, static_cast<unsigned long>(result));
}

bool fileMatches(const std::filesystem::path& path, VapePreviewAssets::AssetView asset) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return false;

    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) != asset.size)
        return false;

    input.seekg(0, std::ios::beg);
    constexpr size_t kChunkSize = 64 * 1024;
    char buffer[kChunkSize];
    size_t offset = 0;
    while (offset < asset.size) {
        const size_t remaining = asset.size - offset;
        const size_t amount = remaining < kChunkSize ? remaining : kChunkSize;
        input.read(buffer, static_cast<std::streamsize>(amount));
        if (input.gcount() != static_cast<std::streamsize>(amount) ||
            std::memcmp(buffer, asset.data + offset, amount) != 0) {
            return false;
        }
        offset += amount;
    }
    return true;
}

bool writeAssetFile(VapePreviewAssets::AssetView asset, const std::filesystem::path& destination) {
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char*>(asset.data), static_cast<std::streamsize>(asset.size));
    output.flush();
    output.close();
    return fileMatches(destination, asset);
}

bool prepareRuntimeFiles() {
    wchar_t localAppData[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, _countof(localAppData));
    if (!length || length >= _countof(localAppData))
        return false;

    GUID sessionId{};
    if (FAILED(CoCreateGuid(&sessionId)))
        return false;
    wchar_t sessionName[128] = {};
    swprintf_s(sessionName, L"session-%lu-%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
               GetCurrentProcessId(), sessionId.Data1, sessionId.Data2, sessionId.Data3, sessionId.Data4[0],
               sessionId.Data4[1], sessionId.Data4[2], sessionId.Data4[3], sessionId.Data4[4], sessionId.Data4[5],
               sessionId.Data4[6], sessionId.Data4[7]);

    const std::filesystem::path root = std::filesystem::path(localAppData) / L"Cobble" / L"WebView2";
    g_sessionRoot = root / sessionName;
    g_assetsFolder = g_sessionRoot / L"assets";
    g_profileFolder = g_sessionRoot / L"profile";
    g_loaderPath = g_sessionRoot / L"WebView2Loader.dll";

    std::error_code error;
    std::filesystem::create_directories(g_assetsFolder, error);
    if (error)
        return false;
    std::filesystem::create_directories(g_profileFolder, error);
    if (error)
        return false;

    return writeAssetFile(VapePreviewAssets::IndexHtml(), g_assetsFolder / L"index.html") &&
           writeAssetFile(VapePreviewAssets::StylesCss(), g_assetsFolder / L"styles.css") &&
           writeAssetFile(VapePreviewAssets::AppJs(), g_assetsFolder / L"app.js") &&
           writeAssetFile(VapePreviewAssets::WebView2Loader(), g_loaderPath);
}

void cleanupSessionFiles() {
    if (g_sessionRoot.empty())
        return;

    std::error_code error;
    for (int attempt = 0; attempt < 20; ++attempt) {
        error.clear();
        std::filesystem::remove_all(g_sessionRoot, error);
        if (!error && !std::filesystem::exists(g_sessionRoot))
            break;
        Sleep(25);
    }
    if (std::filesystem::exists(g_sessionRoot))
        log_warn("Vape Preview: WebView2 session directory is still in use");

    g_sessionRoot.clear();
    g_assetsFolder.clear();
    g_profileFolder.clear();
    g_loaderPath.clear();
}

HRESULT configureFixedGeometry() {
    ComPtr<ICoreWebView2Controller3> controller3;
    HRESULT result = g_controller.As(&controller3);
    if (FAILED(result))
        return result;

    if (FAILED(result = controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS)) ||
        FAILED(result = controller3->put_ShouldDetectMonitorScaleChanges(FALSE)) ||
        FAILED(result = controller3->put_RasterizationScale(1.0)) ||
        FAILED(result = g_controller->put_ZoomFactor(1.0)) ||
        FAILED(result = g_controller->put_IsVisible(FALSE))) {
        return result;
    }

    COREWEBVIEW2_BOUNDS_MODE boundsMode{};
    BOOL detectsScale = TRUE;
    double rasterizationScale = 0.0;
    double zoomFactor = 0.0;
    BOOL visible = TRUE;
    if (FAILED(result = controller3->get_BoundsMode(&boundsMode)) ||
        FAILED(result = controller3->get_ShouldDetectMonitorScaleChanges(&detectsScale)) ||
        FAILED(result = controller3->get_RasterizationScale(&rasterizationScale)) ||
        FAILED(result = g_controller->get_ZoomFactor(&zoomFactor)) ||
        FAILED(result = g_controller->get_IsVisible(&visible))) {
        return result;
    }

    if (boundsMode != COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS || detectsScale || visible ||
        std::abs(rasterizationScale - 1.0) > 0.000001 || std::abs(zoomFactor - 1.0) > 0.000001) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

// Center the 800x480 overlay window over Minecraft's client area, kept topmost
// without stealing activation so the game does not lose foreground/fullscreen.
void repositionOverlayWindow() {
    if (!g_overlayWindow)
        return;
    const HWND mcWindow = g_requestedParent.load(std::memory_order_acquire);
    int left = 0;
    int top = 0;
    if (IsWindow(mcWindow)) {
        RECT clientRect{};
        POINT topLeft{0, 0};
        if (GetClientRect(mcWindow, &clientRect) && ClientToScreen(mcWindow, &topLeft)) {
            const int clientW = clientRect.right - clientRect.left;
            const int clientH = clientRect.bottom - clientRect.top;
            left = topLeft.x + (clientW - kClientWidth) / 2;
            top = topLeft.y + (clientH - kClientHeight) / 2;
        }
    }
    SetWindowPos(g_overlayWindow, HWND_TOPMOST, left, top, kClientWidth, kClientHeight,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void applyControllerState() {
    if (!g_controller || !g_overlayWindow)
        return;

    const bool shouldShow = g_state.load(std::memory_order_acquire) == HostState::Ready &&
                            g_desiredVisible.load(std::memory_order_acquire);

    if (shouldShow) {
        repositionOverlayWindow();
        // SWP_NOACTIVATE keeps Minecraft as the foreground window, so the overlay
        // simply floats on top without knocking the game out of fullscreen.
        SetWindowPos(g_overlayWindow, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (!g_currentVisible) {
            const HRESULT visibilityResult = g_controller->put_IsVisible(TRUE);
            if (FAILED(visibilityResult)) {
                failHost("changing visibility", visibilityResult);
                return;
            }
            g_currentVisible = true;
        }
    } else {
        if (g_currentVisible) {
            g_controller->put_IsVisible(FALSE);
            g_currentVisible = false;
        }
        ShowWindow(g_overlayWindow, SW_HIDE);
        postParentMessage(kRestoreGameFocusMessage);
    }
}

HRESULT verifyDocumentGeometry() {
    if (!g_webView)
        return E_POINTER;
    return g_webView->ExecuteScript(
        kVerifyGeometryScript,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT result, LPCWSTR value) -> HRESULT {
                if (g_stopRequested.load(std::memory_order_acquire))
                    return S_OK;
                if (FAILED(result)) {
                    failHost("verifying rendered 800x480 UI", result);
                    return S_OK;
                }
                const std::string report = toUtf8(value);
                log_debug("[VapePreview] geometry probe: %s", report.c_str());
                // ExecuteScript returns the string result as JSON, so the object's
                // ok flag arrives with escaped quotes: \"ok\":true.
                if (report.find("\\\"ok\\\":true") == std::string::npos &&
                    report.find("\"ok\":true") == std::string::npos) {
                    failHost("verifying rendered 800x480 UI", E_FAIL);
                    return S_OK;
                }
                g_state.store(HostState::Ready, std::memory_order_release);
                applyControllerState();
                if (g_state.load(std::memory_order_acquire) == HostState::Ready)
                    log_ok("[VapePreview] UI ready inside host (verified 800x480, scale 1.0)");
                return S_OK;
            })
            .Get());
}

HRESULT onNavigationCompleted(ICoreWebView2NavigationCompletedEventArgs* args) {
    if (g_stopRequested.load(std::memory_order_acquire) ||
        g_state.load(std::memory_order_acquire) != HostState::Starting) {
        return S_OK;
    }

    BOOL succeeded = FALSE;
    if (!args || FAILED(args->get_IsSuccess(&succeeded)) || !succeeded) {
        failHost("loading embedded UI", E_FAIL);
        return S_OK;
    }

    log_ok("[VapePreview] navigation completed; verifying geometry");
    const HRESULT verifyResult = verifyDocumentGeometry();
    if (FAILED(verifyResult))
        failHost("starting rendered geometry verification", verifyResult);
    return S_OK;
}

HRESULT onControllerCreated(HRESULT result, ICoreWebView2Controller* controller) {
    notePendingCreationFinished();
    if (g_stopRequested.load(std::memory_order_acquire)) {
        if (controller)
            controller->Close();
        return S_OK;
    }
    if (FAILED(result) || !controller) {
        failHost("creating WebView2 controller", FAILED(result) ? result : E_POINTER);
        return S_OK;
    }

    g_controller = controller;
    result = g_controller->get_CoreWebView2(g_webView.ReleaseAndGetAddressOf());
    if (FAILED(result) || !g_webView) {
        failHost("acquiring WebView2", FAILED(result) ? result : E_POINTER);
        return S_OK;
    }

    result = configureFixedGeometry();
    if (FAILED(result)) {
        failHost("configuring fixed WebView2 geometry", result);
        return S_OK;
    }

    // The WebView fills the whole 800x480 overlay window and never moves within
    // it; the window itself is what gets repositioned over Minecraft.
    const RECT bounds{0, 0, kClientWidth, kClientHeight};
    result = g_controller->put_Bounds(bounds);
    if (FAILED(result)) {
        failHost("setting 800x480 bounds", result);
        return S_OK;
    }

    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(g_controller.As(&controller2))) {
        const COREWEBVIEW2_COLOR transparent{0, 0, 0, 0};
        if (FAILED(controller2->put_DefaultBackgroundColor(transparent)))
            log_warn("Vape Preview: transparent host background was unavailable");
    }

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(g_webView->get_Settings(settings.ReleaseAndGetAddressOf())) && settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
    }

    ComPtr<ICoreWebView2_3> webView3;
    result = g_webView.As(&webView3);
    if (FAILED(result) ||
        FAILED(result = webView3->SetVirtualHostNameToFolderMapping(
                   kVirtualHost, g_assetsFolder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
        failHost("mapping embedded UI resources", result);
        return S_OK;
    }

    EventRegistrationToken token{};
    result = g_webView->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                return onNavigationCompleted(args);
            })
            .Get(),
        &token);
    if (FAILED(result)) {
        failHost("registering navigation callback", result);
        return S_OK;
    }

    result = g_webView->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                if (!g_stopRequested.load(std::memory_order_acquire))
                    failHost("WebView2 process terminated", E_FAIL);
                return S_OK;
            })
            .Get(),
        &token);
    if (FAILED(result)) {
        failHost("registering process-failure fallback", result);
        return S_OK;
    }

    result = g_webView->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR message = nullptr;
                if (args && SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                    if (std::wcscmp(message, L"close-menu") == 0)
                        postParentMessage(kCloseMenuMessage);
                    CoTaskMemFree(message);
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (FAILED(result)) {
        failHost("registering UI message callback", result);
        return S_OK;
    }

    result = g_controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                if (!args)
                    return S_OK;
                COREWEBVIEW2_KEY_EVENT_KIND kind{};
                UINT virtualKey = 0;
                if (FAILED(args->get_KeyEventKind(&kind)) || FAILED(args->get_VirtualKey(&virtualKey)))
                    return S_OK;
                const bool isKeyDown = kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN ||
                                       kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN;
                const int menuKey = g_menuKey.load(std::memory_order_relaxed);
                if (isKeyDown && menuKey != 0 && virtualKey == static_cast<UINT>(menuKey)) {
                    args->put_Handled(TRUE);
                    postParentMessage(kCloseMenuMessage);
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (FAILED(result)) {
        failHost("registering menu key callback", result);
        return S_OK;
    }

    applyControllerState();
    result = g_webView->AddScriptToExecuteOnDocumentCreated(
        kEmbeddedMountScript,
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [](HRESULT scriptResult, LPCWSTR) -> HRESULT {
                if (g_stopRequested.load(std::memory_order_acquire))
                    return S_OK;
                if (FAILED(scriptResult)) {
                    failHost("mounting fixed-size UI", scriptResult);
                    return S_OK;
                }
                log_ok("[VapePreview] controller ready; navigating to embedded UI");
                const HRESULT navigateResult = g_webView ? g_webView->Navigate(kVirtualUrl) : E_POINTER;
                if (FAILED(navigateResult))
                    failHost("navigating to embedded UI", navigateResult);
                return S_OK;
            })
            .Get());
    if (FAILED(result))
        failHost("registering fixed-size mount", result);

    return S_OK;
}

HRESULT onEnvironmentCreated(HRESULT result, ICoreWebView2Environment* environment) {
    notePendingCreationFinished();
    if (g_stopRequested.load(std::memory_order_acquire))
        return S_OK;
    if (FAILED(result) || !environment) {
        failHost("creating WebView2 environment", FAILED(result) ? result : E_POINTER);
        return S_OK;
    }

    g_environment = environment;
    log_ok("[VapePreview] WebView2 environment created");
    if (!IsWindow(g_overlayWindow)) {
        failHost("validating overlay window", HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
        return S_OK;
    }

    ++g_pendingCreations;
    result = g_environment->CreateCoreWebView2Controller(
        g_overlayWindow,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                return onControllerCreated(controllerResult, controller);
            })
            .Get());
    if (FAILED(result)) {
        --g_pendingCreations;
        failHost("starting WebView2 controller creation", result);
    }
    return S_OK;
}

void pumpPendingMessages(int iterations) {
    MSG message{};
    for (int i = 0; i < iterations; ++i) {
        bool drainedAny = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            drainedAny = true;
        }
        if (!drainedAny)
            Sleep(2);
    }
}

void releaseWebView() {
    log_debug("[VapePreview] teardown: closing controller");
    g_currentVisible = false;
    if (g_controller) {
        g_controller->put_IsVisible(FALSE);
        g_controller->Close();
    }
    g_webView.Reset();
    g_controller.Reset();
    g_environment.Reset();
    if (g_overlayWindow)
        ShowWindow(g_overlayWindow, SW_HIDE);

    // Closing the controller finishes asynchronously; the WebView2 runtime needs
    // the STA to keep pumping briefly or COM teardown/CoUninitialize can stall.
    pumpPendingMessages(50);

    // The loader stays resident on purpose. Calling FreeLibrary while the
    // Chromium browser/GPU processes are still exiting can block the STA, and a
    // resident loader is harmless and lets a later re-init reuse it.
    g_createEnvironment = nullptr;
    cleanupSessionFiles();
    log_debug("[VapePreview] teardown: controller released");
}

void hostThreadMain() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        failHost("initializing COM apartment", comResult);
        return;
    }

    // A message-only control window receives Start/Visible/Shutdown posts. It is
    // owned by this STA and must be created before the thread id is published.
    WNDCLASSW controlClass{};
    controlClass.lpfnWndProc = ControlWndProc;
    controlClass.hInstance = GetModuleHandleW(nullptr);
    controlClass.lpszClassName = kControlWindowClass;
    RegisterClassW(&controlClass);  // ERROR_CLASS_ALREADY_EXISTS on re-init is fine.
    const HWND controlWindow = CreateWindowExW(0, kControlWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                               nullptr, controlClass.hInstance, nullptr);
    if (!controlWindow) {
        failHost("creating host control window", HRESULT_FROM_WIN32(GetLastError()));
        CoUninitialize();
        return;
    }
    g_controlWindow.store(controlWindow, std::memory_order_release);

    // The visible 800x480 overlay that hosts the WebView. Topmost + no-activate
    // so it floats over Minecraft (windowed / borderless) without taking focus.
    WNDCLASSW overlayClass{};
    overlayClass.lpfnWndProc = OverlayWndProc;
    overlayClass.hInstance = GetModuleHandleW(nullptr);
    overlayClass.lpszClassName = kOverlayWindowClass;
    overlayClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&overlayClass);
    g_overlayWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kOverlayWindowClass,
                                      L"", WS_POPUP, 0, 0, kClientWidth, kClientHeight, nullptr, nullptr,
                                      overlayClass.hInstance, nullptr);
    if (!g_overlayWindow) {
        failHost("creating overlay window", HRESULT_FROM_WIN32(GetLastError()));
        DestroyWindow(controlWindow);
        g_controlWindow.store(nullptr, std::memory_order_release);
        CoUninitialize();
        return;
    }
    SetTimer(g_overlayWindow, kRepositionTimerId, 50, nullptr);
    g_hostThreadId.store(GetCurrentThreadId(), std::memory_order_release);

    MSG message{};
    if (!g_stopRequested.load(std::memory_order_acquire)) {
        if (!prepareRuntimeFiles()) {
            const DWORD error = GetLastError();
            failHost("extracting embedded UI", error ? HRESULT_FROM_WIN32(error) : E_FAIL);
        } else {
            g_loaderModule = LoadLibraryExW(g_loaderPath.c_str(), nullptr,
                                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!g_loaderModule) {
                failHost("loading WebView2Loader", HRESULT_FROM_WIN32(GetLastError()));
            } else {
                g_createEnvironment = reinterpret_cast<CreateEnvironmentFn>(
                    GetProcAddress(g_loaderModule, "CreateCoreWebView2EnvironmentWithOptions"));
                if (!g_createEnvironment) {
                    failHost("resolving WebView2 entry point", HRESULT_FROM_WIN32(GetLastError()));
                } else {
                    const auto handler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                        [](HRESULT environmentResult, ICoreWebView2Environment* environment) -> HRESULT {
                            return onEnvironmentCreated(environmentResult, environment);
                        });
                    ++g_pendingCreations;
                    const HRESULT createResult =
                        g_createEnvironment(nullptr, g_profileFolder.c_str(), nullptr, handler.Get());
                    if (FAILED(createResult)) {
                        --g_pendingCreations;
                        failHost("starting WebView2 environment", createResult);
                    }
                }
            }
        }
    }

    // ControlWndProc dispatches Start/Visible posts and calls PostQuitMessage
    // when shutdown is requested and no creation is still in flight.
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    releaseWebView();
    g_hostThreadId.store(0, std::memory_order_release);
    g_controlWindow.store(nullptr, std::memory_order_release);
    if (g_overlayWindow) {
        KillTimer(g_overlayWindow, kRepositionTimerId);
        DestroyWindow(g_overlayWindow);
        g_overlayWindow = nullptr;
    }
    DestroyWindow(controlWindow);
    UnregisterClassW(kOverlayWindowClass, GetModuleHandleW(nullptr));
    log_debug("[VapePreview] teardown: CoUninitialize");
    CoUninitialize();
    log_debug("[VapePreview] host thread exited");
}

}  // namespace

void Start(HWND parentWindow) {
    if (!IsWindow(parentWindow) || !g_acceptUiWork.load(std::memory_order_acquire))
        return;

    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    if (!g_acceptUiWork.load(std::memory_order_acquire))
        return;

    g_requestedParent.store(parentWindow, std::memory_order_release);
    if (g_hostThread.joinable()) {
        postHostMessage(kApplyStateMessage);
        return;
    }
    if (g_state.load(std::memory_order_acquire) != HostState::Stopped)
        return;

    g_stopRequested.store(false, std::memory_order_release);
    g_state.store(HostState::Starting, std::memory_order_release);

    RECT clientRect{};
    if (GetClientRect(parentWindow, &clientRect)) {
        g_parentWidth.store(clientRect.right - clientRect.left, std::memory_order_relaxed);
        g_parentHeight.store(clientRect.bottom - clientRect.top, std::memory_order_relaxed);
    }

    try {
        g_hostThread = std::thread(hostThreadMain);
    } catch (...) {
        g_state.store(HostState::Failed, std::memory_order_release);
        log_err("Vape Preview: failed to create host thread");
    }
}

void BeginShutdown() {
    g_acceptUiWork.store(false, std::memory_order_release);
    g_desiredVisible.store(false, std::memory_order_release);
    const HostState state = g_state.load(std::memory_order_acquire);
    if (state != HostState::Stopped && state != HostState::Failed)
        g_state.store(HostState::Stopping, std::memory_order_release);
    postHostMessage(kApplyStateMessage);
}

void Shutdown() {
    std::thread threadToJoin;
    {
        std::unique_lock<std::mutex> lock(g_lifecycleMutex);
        g_acceptUiWork.store(false, std::memory_order_release);
        g_desiredVisible.store(false, std::memory_order_release);

        if (g_shutdownInProgress) {
            g_shutdownCondition.wait(lock, [] { return !g_shutdownInProgress; });
            return;
        }
        if (!g_hostThread.joinable()) {
            g_state.store(HostState::Stopped, std::memory_order_release);
            return;
        }

        g_shutdownInProgress = true;
        g_state.store(HostState::Stopping, std::memory_order_release);
        g_stopRequested.store(true, std::memory_order_release);

        for (int attempt = 0; attempt < 100 && !g_hostThreadId.load(std::memory_order_acquire); ++attempt)
            Sleep(1);
        postHostMessage(kShutdownMessage);
        threadToJoin = std::move(g_hostThread);
    }

    if (threadToJoin.joinable())
        threadToJoin.join();

    {
        std::lock_guard<std::mutex> lock(g_lifecycleMutex);
        g_requestedParent.store(nullptr, std::memory_order_release);
        g_lastBounds = RECT{0, 0, 0, 0};
        g_pendingCreations = 0;
        g_state.store(HostState::Stopped, std::memory_order_release);
        g_shutdownInProgress = false;
    }
    g_shutdownCondition.notify_all();
}

void SetVisible(bool visible) {
    if (!g_acceptUiWork.load(std::memory_order_acquire))
        visible = false;
    if (g_desiredVisible.exchange(visible, std::memory_order_acq_rel) != visible)
        postHostMessage(kApplyStateMessage);
}

void UpdateBounds(int parentWidth, int parentHeight) {
    if (parentWidth <= 0 || parentHeight <= 0 || !g_acceptUiWork.load(std::memory_order_acquire))
        return;
    const bool widthChanged = g_parentWidth.exchange(parentWidth, std::memory_order_acq_rel) != parentWidth;
    const bool heightChanged = g_parentHeight.exchange(parentHeight, std::memory_order_acq_rel) != parentHeight;
    if (widthChanged || heightChanged)
        postHostMessage(kApplyStateMessage);
}

void SetMenuKey(int virtualKey) { g_menuKey.store(virtualKey, std::memory_order_release); }

bool HandlesMenu() { return g_state.load(std::memory_order_acquire) == HostState::Ready; }

bool IsReady() { return HandlesMenu(); }

}  // namespace VapePreviewHost
