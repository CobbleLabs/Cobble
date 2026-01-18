#include "loader_window.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <Windows.h>
#include <windowsx.h>
#include <cmath>
#include <string>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui_impl_dx11.h"
#include "../../../infra/ext/imgui/imgui_impl_win32.h"
#include "../../../infra/ext/imgui/imgui_internal.h"
#include "inter_bold.hpp"
#include "inter_regular.hpp"
#include "../util/aes_string.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
HWND g_hwnd = nullptr;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_initialized = false;
bool g_shouldClose = false;

// Animation State Machine
enum AnimStage {
    STAGE_INTRO = 0,      // Logo fade in
    STAGE_LOGIN = 1,      // Login form
    STAGE_WAITING = 2,    // Waiting for MC
    STAGE_INJECTING = 3,  // Injecting
    STAGE_SUCCESS = 4,    // Success animation
    STAGE_EXIT = 5        // Fade out
};

AnimStage g_animStage = STAGE_INTRO;
float g_stageTime = 0.0f;
float g_fadeAlpha = 0.0f;

// UI State
LoaderUI::State g_currentState = LoaderUI::State::INITIALIZING;
std::string g_message;
std::string g_errorMessage;
std::string g_licensePlan;
std::string g_licenseExpiry;

char g_username[256] = "";
char g_password[256] = "";
bool g_loginButtonPressed = false;

// Colors
const ImU32 COL_BG = IM_COL32(11, 11, 13, 255);
const ImU32 COL_CARD = IM_COL32(18, 18, 20, 255);
const ImU32 COL_BORDER = IM_COL32(30, 30, 34, 255);
const ImU32 COL_INPUT = IM_COL32(15, 15, 18, 255);
const ImU32 COL_ORANGE = IM_COL32(255, 122, 24, 255);
const ImU32 COL_ORANGE_LIGHT = IM_COL32(255, 159, 28, 255);
const ImU32 COL_TEXT = IM_COL32(255, 255, 255, 255);
const ImU32 COL_TEXT_DIM = IM_COL32(150, 150, 150, 255);
const ImU32 COL_ERROR = IM_COL32(230, 80, 80, 255);
const ImU32 COL_SUCCESS = IM_COL32(80, 200, 120, 255);
const ImU32 COL_SUCCESS_PREMIUM = IM_COL32(255, 180, 90, 255);  // warm success tone that keeps brand heat

// UI Animation states
static float g_userFocusAnim = 0.0f;
static float g_passFocusAnim = 0.0f;
static float g_userUnderlineAnim = 0.0f;
static float g_passUnderlineAnim = 0.0f;
static float g_userTextAnim = 0.0f;
static float g_passTextAnim = 0.0f;
static float g_buttonPressAnim = 0.0f;
static float g_loginTime = 0.0f;
static float g_spinnerTime = 0.0f;
static float g_userClickPulse = 0.0f;
static float g_passClickPulse = 0.0f;
static float g_userUnderlineVel = 0.0f;
static float g_passUnderlineVel = 0.0f;
static float g_injectProgress = 0.0f;
static float g_injectPulseTime = 0.0f;
static bool g_focusUserRequested = false;
static bool g_focusPassRequested = false;
static float g_injectTargetSpeed = 0.2f;  // Variable speed for current session
// Easing functions
float easeOutCubic(float x) {
    return 1.0f - pow(1.0f - x, 3.0f);
}

float easeInOutCubic(float x) {
    return x < 0.5f ? 4.0f * x * x * x : 1.0f - powf(-2.0f * x + 2.0f, 3.0f) / 2.0f;
}
float easeOutBack(float x) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(x - 1.0f, 3.0f) + c1 * powf(x - 1.0f, 2.0f);
}
float smoothTo(float current, float target, float speed, float dt) {
    // Critically damped response so transitions feel smooth but responsive.
    return current + (target - current) * (1.0f - expf(-speed * dt));
}
void springTo(float target, float dt, float stiffness, float damping, float& value, float& velocity) {
    // Simple damped spring integration (semi-implicit) to add physical feel without wobble.
    float accel = (target - value) * stiffness;
    velocity = (velocity + accel * dt) * expf(-damping * dt);
    value += velocity * dt;

    // Clamp to valid range and kill velocity when settled to prevent oscillation
    value = ImClamp(value, 0.0f, 1.0f);
    if (fabsf(value - target) < 0.01f && fabsf(velocity) < 0.5f) {
        value = target;
        velocity = 0.0f;
    }
}

ImFont* g_fontRegular = nullptr;
ImFont* g_fontMedium = nullptr;
ImFont* g_fontLogo = nullptr;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
        case WM_NCHITTEST: {
            // Allow dragging the window from edges (30px border margin)
            RECT rect;
            GetClientRect(hWnd, &rect);
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hWnd, &pt);

            // Exclude window controls area (top-right corner: minimize and close buttons)
            // Buttons are at x >= rect.right - 80 and y <= 40
            bool inControlsArea = (pt.x >= rect.right - 80) && (pt.y <= 40);
            if (inControlsArea) {
                break;  // Let ImGui handle clicks on buttons
            }

            const int borderWidth = 30;
            bool onLeftEdge = pt.x < borderWidth;
            bool onRightEdge = pt.x >= rect.right - borderWidth;
            bool onTopEdge = pt.y < borderWidth;
            bool onBottomEdge = pt.y >= rect.bottom - borderWidth;

            if (onLeftEdge || onRightEdge || onTopEdge || onBottomEdge) {
                return HTCAPTION;  // Allow dragging to move window
            }
            break;
        }
        case WM_SIZE:
            if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                if (g_mainRenderTargetView) {
                    g_mainRenderTargetView->Release();
                    g_mainRenderTargetView = nullptr;
                }
                g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                ID3D11Texture2D* pBackBuffer;
                g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
                g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
                pBackBuffer->Release();
            }
            return 0;
        case WM_CLOSE:
            g_shouldClose = true;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void DrawSpinner(ImDrawList* dl, ImVec2 center, float radius, float thickness, float phase, float globalAlpha = 1.0f) {
    const int segments = 32;             // Reduced for performance
    float start = phase * 2.0f * IM_PI;  // phase is 0..1, convert to radians

    for (int i = 0; i < segments; i++) {
        float t0 = (float)i / segments;
        float t1 = (float)(i + 1) / segments;

        float a0 = start + t0 * 2.0f * IM_PI;
        float a1 = start + t1 * 2.0f * IM_PI;

        float fade = t0 * t0;  // quadratic easing

        ImU32 col = IM_COL32(255, 122, 24, (int)(fade * 255 * globalAlpha));

        dl->AddLine({center.x + cosf(a0) * radius, center.y + sinf(a0) * radius},
                    {center.x + cosf(a1) * radius, center.y + sinf(a1) * radius}, col, thickness);
    }
}

void DrawGradientButton(ImDrawList* dl, ImVec2 min, ImVec2 max, const char* text, bool hovered, bool* clicked) {
    float lift = hovered ? 2.0f : 0.0f;
    min.y -= lift;
    max.y -= lift;

    // Shadow
    dl->AddRectFilled(ImVec2(min.x + 4, min.y + 6), ImVec2(max.x - 4, max.y + 6), IM_COL32(255, 122, 24, 40), 10.0f);

    // Gradient fill
    dl->AddRectFilledMultiColor(min, max, COL_ORANGE, COL_ORANGE_LIGHT, COL_ORANGE_LIGHT, COL_ORANGE);
    dl->AddRect(min, max, IM_COL32(255, 255, 255, 30), 10.0f);

    // Text
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 textPos = ImVec2(min.x + (max.x - min.x - textSize.x) * 0.5f, min.y + (max.y - min.y - textSize.y) * 0.5f);
    dl->AddText(textPos, COL_TEXT, text);

    if (hovered && ImGui::IsMouseClicked(0))
        *clicked = true;
}
}  // namespace

namespace LoaderUI {
bool Init() {
    if (g_initialized)
        return true;

    // Enable DPI awareness for sharp rendering
    SetProcessDPIAware();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CobbleLoader";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int windowW = 800, windowH = 480;

    g_hwnd = CreateWindowExW(0, L"CobbleLoader", L"Cobble", WS_POPUP, (screenW - windowW) / 2, (screenH - windowH) / 2,
                             windowW, windowH, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd)
        return false;

    // Create D3D11 device and swap chain
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = windowW;
    sd.BufferDesc.Height = windowH;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = 0;  // No special flags - ALLOW_MODE_SWITCH can cause stutter
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // Modern swap effect for Windows 10+

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
                                                featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                                &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    // Create render target
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // Load Inter fonts from embedded data
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    g_fontRegular = io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 17.0f, &fontConfig);
    fontConfig.FontDataOwnedByAtlas = false;
    g_fontMedium = io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 24.0f, &fontConfig);
    fontConfig.FontDataOwnedByAtlas = false;
    g_fontLogo = io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf_data, inter_bold_ttf_size, 50.0f, &fontConfig);

    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImColor(11, 11, 13, 255);
    ImGui::GetStyle().WindowPadding = ImVec2(0, 0);
    ImGui::GetStyle().WindowBorderSize = 0;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Don't show window yet - allow pre-rendering offscreen first
    g_initialized = true;
    return true;
}

void Show() {
    if (g_hwnd)
        ShowWindow(g_hwnd, SW_SHOW);
}

void Shutdown() {
    if (!g_initialized)
        return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
    if (g_pSwapChain) {
        g_pSwapChain->SetFullscreenState(FALSE, nullptr);
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_hwnd)
        DestroyWindow(g_hwnd);
    g_initialized = false;
}

bool Render() {
    if (!g_initialized || g_shouldClose)
        return false;

    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT)
            return false;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;

    // Only clamp extreme spikes, don't cap high-Hz monitors
    if (dt > 0.1f)
        dt = 0.1f;
    g_userClickPulse = ImMax(0.0f, g_userClickPulse - dt * 3.0f);
    g_passClickPulse = ImMax(0.0f, g_passClickPulse - dt * 3.0f);

    g_stageTime += dt;
    g_spinnerTime += dt;  // Accumulate for spinner animation
    if (g_animStage == STAGE_INJECTING) {
        g_injectPulseTime += dt;

        // Organic speed variation with noise
        // Base is g_injectTargetSpeed, plus slight sine wave fluctuation
        float noise = sinf(g_injectPulseTime * 3.5f) * 0.03f;
        float currentSpeed = g_injectTargetSpeed + noise;

        // If we are actually finished, ensure we don't stall at 100% too long,
        // but NEVER accelerate unnaturally. The user wants "weight".
        // If we are NOT finished yet, clamp at 98% to wait for reality.

        float limit = (g_currentState == State::INJECTING_SUCCESS) ? 1.0f : 0.98f;
        g_injectProgress = ImClamp(g_injectProgress + dt * currentSpeed, 0.0f, limit);

    } else {
        g_injectPulseTime = 0.0f;
        if (g_animStage != STAGE_INJECTING)
            g_injectProgress = (g_animStage < STAGE_INJECTING) ? 0.0f : g_injectProgress;
    }
    // Simple spinner phase - linear continuous rotation
    float spinnerPhase = fmodf(g_spinnerTime * 0.8f, 1.0f);

    // Update stage based on state
    if (g_currentState == State::LOGIN_REQUIRED || g_currentState == State::ERROR_STATE) {
        if (g_animStage < STAGE_LOGIN) {
            g_animStage = STAGE_LOGIN;
            g_loginTime = 0.0f;  // Reset login animations
        }
    } else if (g_currentState == State::AUTHENTICATING) {
        if (g_animStage != STAGE_INJECTING) {
            g_animStage = STAGE_INJECTING;
            g_stageTime = 0.0f;
            g_injectProgress = 0.0f;
            g_injectPulseTime = 0.0f;
            // Randomize speed: 4.0s to 6.0s duration
            // Speed = 1/Duration. Range [0.166, 0.25]
            float rnd = fmodf((float)ImGui::GetTime() * 13.0f, 1.0f);
            g_injectTargetSpeed = 0.166f + rnd * (0.25f - 0.166f);
        }
    } else if (g_currentState == State::INJECTING_SUCCESS) {
        if (g_animStage < STAGE_INJECTING) {
            g_animStage = STAGE_INJECTING;
            g_stageTime = 0.0f;
            g_injectProgress = 0.0f;
            g_injectPulseTime = 0.0f;
            // Randomize speed same as above
            float rnd = fmodf((float)ImGui::GetTime() * 17.0f, 1.0f);
            g_injectTargetSpeed = 0.166f + rnd * (0.25f - 0.166f);
        }
    } else if (g_currentState == State::LICENSE_VALID || g_currentState == State::WAITING_MINECRAFT) {
        if (g_animStage < STAGE_WAITING) {
            g_animStage = STAGE_WAITING;
            g_stageTime = 0;
        }
    }

    // Check for completion AFTER state initialization to ensure proper animation
    if (g_currentState == State::INJECTING_SUCCESS && g_animStage == STAGE_INJECTING && g_injectProgress >= 0.999f &&
        g_stageTime > 0.8f) {
        g_animStage = STAGE_SUCCESS;
        g_stageTime = 0.0f;
    }

    // Update login time tracker - only when crossfade has started (formAlpha > 0)
    // This is calculated after formAlpha below, so we defer the update

    // Fade animation with smooth crossfade
    // Use absolute time-based crossfade for seamless transition
    float spinnerAlpha = 0.0f;
    float formAlpha = 0.0f;

    if (g_animStage == STAGE_INTRO) {
        // Skip spinner, go directly to login with simple fade-in
        g_fadeAlpha = ImClamp(g_stageTime / 0.4f, 0.0f, 1.0f);
        spinnerAlpha = 0.0f;
        formAlpha = g_fadeAlpha;  // Form fades in directly
        if (g_stageTime > 0.4f) {
            g_animStage = STAGE_LOGIN;
        }
    } else if (g_animStage == STAGE_LOGIN) {
        g_fadeAlpha = 1.0f;
        spinnerAlpha = 0.0f;
        formAlpha = 1.0f;
    } else if (g_animStage == STAGE_EXIT) {
        // Hardware window fade-out
        static bool s_exitStyleSet = false;

        float duration = 0.4f;
        float t = ImClamp(g_stageTime / duration, 0.0f, 1.0f);
        float alpha = 1.0f - easeOutCubic(t);  // Non-linear fade for premium feel

        if (!s_exitStyleSet) {
            SetWindowLong(g_hwnd, GWL_EXSTYLE, GetWindowLong(g_hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
            s_exitStyleSet = true;
        }

        SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)(255 * alpha), LWA_ALPHA);

        if (g_stageTime > duration + 0.1f) {
            g_shouldClose = true;
            return false;
        }

        // Keep content opaque compliant with the window alpha
        g_fadeAlpha = 1.0f;
        spinnerAlpha = 0.0f;
        formAlpha = 1.0f;
    } else {
        g_fadeAlpha = 1.0f;
        spinnerAlpha = 0.0f;
        formAlpha = 1.0f;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_fadeAlpha);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 winSize = io.DisplaySize;
    float centerX = winSize.x * 0.5f;
    float centerY = winSize.y * 0.5f;

    // Base background
    dl->AddRectFilled(ImVec2(0, 0), winSize, IM_COL32(11, 11, 13, 255));

    // Large soft orange glow (bottom-left, spread)
    ImVec2 glowCenter(winSize.x * 0.20f, winSize.y * 0.80f);
    float maxR = winSize.x * 1.2f;  // que se expanda en serio
    float glowPulse = 1.0f;
    if (g_animStage == STAGE_INJECTING && g_injectProgress < 0.999f) {
        glowPulse = 1.0f + 0.35f * (0.5f + 0.5f * sinf(g_injectPulseTime * 2.2f));
    }

    for (int i = 0; i < 40; i++) {
        float t = (float)i / 80.0f;
        float r = t * maxR;

        // easing fuerte para que muera suave
        float alpha = 1.0f - t;
        alpha = alpha * alpha * alpha;

        ImU32 col = IM_COL32(255, 122, 24, (int)(alpha * 18 * glowPulse));  // alpha BAJO
        dl->AddCircleFilled(glowCenter, r, col, 40);
    }

    // Subtle vignette to darken edges
    dl->AddRectFilledMultiColor(ImVec2(0, 0), winSize, IM_COL32(0, 0, 0, 80),  // TL
                                IM_COL32(0, 0, 0, 80),                         // TR
                                IM_COL32(0, 0, 0, 120),                        // BR
                                IM_COL32(0, 0, 0, 120)                         // BL
    );

    // Window border
    dl->AddRect(ImVec2(0, 0), winSize, IM_COL32(40, 40, 45, 255), 0.0f);

    // Window controls (top right) - Minimize and Close
    float btnSize = 20.0f;
    float ctrlY = 15.0f;

    ImVec2 minPos(winSize.x - 70, ctrlY);
    bool minHovered = ImGui::IsMouseHoveringRect(minPos, ImVec2(minPos.x + btnSize, minPos.y + btnSize));
    dl->AddText(ImVec2(minPos.x + 3, minPos.y - 2), minHovered ? COL_TEXT : COL_TEXT_DIM, "-");
    if (minHovered && ImGui::IsMouseClicked(0))
        ShowWindow(g_hwnd, SW_MINIMIZE);

    ImVec2 closePos(winSize.x - 40, ctrlY);
    bool closeHovered = ImGui::IsMouseHoveringRect(closePos, ImVec2(closePos.x + btnSize, closePos.y + btnSize));
    dl->AddText(ImVec2(closePos.x + 2, closePos.y - 2), closeHovered ? COL_TEXT : COL_TEXT_DIM, "x");
    if (closeHovered && ImGui::IsMouseClicked(0))
        g_shouldClose = true;

    // Premium timeline-based animations (no cheap staggers)
    float uiIn = easeOutCubic(ImClamp(g_loginTime / 0.6f, 0.0f, 1.0f));
    float inputsIn = easeOutCubic(ImClamp((g_loginTime - 0.15f) / 0.5f, 0.0f, 1.0f));

    // Title position
    float titleY = centerY - 130;

    // Form area (no card, just centered content)
    float formW = 340.0f;
    float formX = centerX - formW * 0.5f;
    float formY = titleY + 90;

    // Premium offset (subtle drift, not slide)
    float formOffset = (1.0f - inputsIn) * 12.0f;

    // Render Spinner (if intro or fading out)
    if (spinnerAlpha > 0.01f) {
        float spinScale = 0.9f + 0.1f * spinnerAlpha;
        float phase = spinnerPhase;
        DrawSpinner(dl, ImVec2(centerX, formY + 60), 20.0f * spinScale, 2.5f, phase, spinnerAlpha);
    }

    // Login form with crossfade alpha - ALL text goes here
    if (g_animStage == STAGE_LOGIN && formAlpha > 0.01f) {
        // Update login time only when form is visible
        g_loginTime += dt;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, formAlpha);

        // Title "Cobble" - LANDS with easeOutBack (premium settle effect)
        ImGui::PushFont(g_fontLogo);
        const char* title = "Cobble";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        float titleIn = easeOutBack(uiIn);
        float titleYPos = titleY + (1.0f - titleIn) * 18.0f;
        float titleAlpha = uiIn * formAlpha;
        dl->AddText(ImVec2(centerX - titleSize.x * 0.5f, titleYPos), IM_COL32(255, 255, 255, (int)(255 * titleAlpha)),
                    title);
        ImGui::PopFont();

        // Subtitle - DRIFTS (not appears)
        ImGui::PushFont(g_fontRegular);
        const char* subtitle = "Welcome back";
        ImVec2 subSize = ImGui::CalcTextSize(subtitle);
        float subIn = easeOutCubic(ImClamp((g_loginTime - 0.1f) / 0.5f, 0.0f, 1.0f));
        float subY = titleY + 45 + (1.0f - subIn) * 12.0f;
        float subAlpha = subIn * 0.7f * formAlpha;
        dl->AddText(ImVec2(centerX - subSize.x * 0.5f, subY), IM_COL32(150, 150, 150, (int)(255 * subAlpha)), subtitle);
        ImGui::PopFont();

        float inputY = formY + formOffset;

        // Check if we serve a blocking error that shouldn't show login fields
        bool showLoginFields = (g_errorMessage != "Already injected");

        if (showLoginFields) {
            ImGui::PushFont(g_fontRegular);

            // ========== USERNAME INPUT ==========
            // Floating label animation
            float userHasText = (g_username[0] != 0) ? 1.0f : 0.0f;
            float userFocus = g_userFocusAnim;
            float userLabelT = easeOutCubic(ImMax(userHasText, userFocus));

            // Label floats up and shrinks on focus/text
            float userLabelY = inputY + 15 - userLabelT * 14.0f;
            float userLabelScale = (1.0f - userLabelT * 0.15f) * (1.0f + g_userClickPulse * 0.08f);

            ImVec2 userLabelSize = ImGui::CalcTextSize("Username");
            ImVec2 userLabelSizeScaled = ImVec2(userLabelSize.x * userLabelScale, userLabelSize.y * userLabelScale);
            ImVec2 userLabelMin(formX, userLabelY - 2.0f);
            ImVec2 userLabelMax(userLabelMin.x + userLabelSizeScaled.x, userLabelMin.y + userLabelSizeScaled.y + 4.0f);

            ImGui::SetCursorPos(userLabelMin);
            ImGui::InvisibleButton("##user_label",
                                   ImVec2(userLabelMax.x - userLabelMin.x, userLabelMax.y - userLabelMin.y));
            bool userLabelClicked = ImGui::IsItemClicked();
            bool userLabelHovered = ImGui::IsItemHovered();
            if (userLabelClicked) {
                g_userClickPulse = 1.0f;
                g_focusUserRequested = true;
                g_userFocusAnim = ImClamp(g_userFocusAnim + 0.35f, 0.0f, 1.0f);
            }

            float userLabelAlpha = ImClamp(
                0.6f + 0.4f * userLabelT + 0.3f * g_userClickPulse + (userLabelHovered ? 0.1f : 0.0f), 0.0f, 1.1f);
            ImU32 userLabelCol = IM_COL32(180, 180, 185, (int)(200 * inputsIn * userLabelAlpha));

            ImGui::SetWindowFontScale(userLabelScale);
            dl->AddText(ImVec2(formX, userLabelY), userLabelCol, "Username");
            ImGui::SetWindowFontScale(1.0f);

            // Input field
            ImGui::SetCursorPos(ImVec2(formX, inputY + 20));
            if (g_focusUserRequested) {
                ImGui::SetKeyboardFocusHere();
                g_focusUserRequested = false;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

            g_userTextAnim += ((g_username[0] != 0) ? 1.0f : -1.0f) * dt * 20.0f;
            g_userTextAnim = ImClamp(g_userTextAnim, 0.0f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, inputsIn));
            ImGui::PushItemWidth(formW);
            ImGui::InputText("##user", g_username, 256);

            bool userActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
            g_userFocusAnim = smoothTo(g_userFocusAnim, userActive ? 1.0f : 0.0f, 14.0f, dt);
            float userUnderlineTarget = (userActive || g_focusUserRequested) ? 1.0f : 0.0f;
            springTo(userUnderlineTarget, dt, 120.0f, 12.0f, g_userUnderlineAnim, g_userUnderlineVel);

            ImGui::PopItemWidth();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            // Underline - grows from LEFT (not center) with direction
            float lineY = inputY + 42;
            float u = easeOutCubic(ImClamp(g_userUnderlineAnim + g_userClickPulse * 0.35f, 0.0f, 1.1f));
            float underlineW = formW * u;

            // Grey baseline
            dl->AddLine(ImVec2(formX, lineY), ImVec2(formX + formW, lineY), IM_COL32(70, 70, 75, (int)(255 * inputsIn)),
                        1.0f);

            // Orange active line (from left)
            if (u > 0.01f) {
                dl->AddLine(ImVec2(formX, lineY), ImVec2(formX + underlineW, lineY),
                            IM_COL32(255, 122, 24, (int)(255 * inputsIn)), 1.0f + u);
            }

            // ========== PASSWORD INPUT ==========
            float passY = inputY + 65;

            // Floating label for password
            float passHasText = (g_password[0] != 0) ? 1.0f : 0.0f;
            float passFocus = g_passFocusAnim;
            float passLabelT = easeOutCubic(ImMax(passHasText, passFocus));

            float passLabelY = passY + 15 - passLabelT * 14.0f;
            float passLabelScale = (1.0f - passLabelT * 0.15f) * (1.0f + g_passClickPulse * 0.08f);

            ImVec2 passLabelSize = ImGui::CalcTextSize("Password");
            ImVec2 passLabelSizeScaled = ImVec2(passLabelSize.x * passLabelScale, passLabelSize.y * passLabelScale);
            ImVec2 passLabelMin(formX, passLabelY - 2.0f);
            ImVec2 passLabelMax(passLabelMin.x + passLabelSizeScaled.x, passLabelMin.y + passLabelSizeScaled.y + 4.0f);

            ImGui::SetCursorPos(passLabelMin);
            ImGui::InvisibleButton("##pass_label",
                                   ImVec2(passLabelMax.x - passLabelMin.x, passLabelMax.y - passLabelMin.y));
            bool passLabelClicked = ImGui::IsItemClicked();
            bool passLabelHovered = ImGui::IsItemHovered();
            if (passLabelClicked) {
                g_passClickPulse = 1.0f;
                g_focusPassRequested = true;
                g_passFocusAnim = ImClamp(g_passFocusAnim + 0.35f, 0.0f, 1.0f);
            }

            float passLabelAlpha = ImClamp(
                0.6f + 0.4f * passLabelT + 0.3f * g_passClickPulse + (passLabelHovered ? 0.1f : 0.0f), 0.0f, 1.1f);
            ImU32 passLabelCol = IM_COL32(180, 180, 185, (int)(200 * inputsIn * passLabelAlpha));

            ImGui::SetWindowFontScale(passLabelScale);
            dl->AddText(ImVec2(formX, passLabelY), passLabelCol, "Password");
            ImGui::SetWindowFontScale(1.0f);

            // Input field
            ImGui::SetCursorPos(ImVec2(formX, passY + 20));
            if (g_focusPassRequested) {
                ImGui::SetKeyboardFocusHere();
                g_focusPassRequested = false;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

            g_passTextAnim += ((g_password[0] != 0) ? 1.0f : -1.0f) * dt * 20.0f;
            g_passTextAnim = ImClamp(g_passTextAnim, 0.0f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, inputsIn));
            ImGui::PushItemWidth(formW);
            ImGui::InputText("##pass", g_password, 256, ImGuiInputTextFlags_Password);

            bool passActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
            g_passFocusAnim = smoothTo(g_passFocusAnim, passActive ? 1.0f : 0.0f, 14.0f, dt);
            float passUnderlineTarget = (passActive || g_focusPassRequested) ? 1.0f : 0.0f;
            springTo(passUnderlineTarget, dt, 120.0f, 12.0f, g_passUnderlineAnim, g_passUnderlineVel);

            ImGui::PopItemWidth();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            // Password underline - same premium treatment
            float lineY2 = passY + 42;
            float u2 = easeOutCubic(ImClamp(g_passUnderlineAnim + g_passClickPulse * 0.35f, 0.0f, 1.1f));
            float underlineW2 = formW * u2;

            // Grey baseline
            dl->AddLine(ImVec2(formX, lineY2), ImVec2(formX + formW, lineY2),
                        IM_COL32(70, 70, 75, (int)(255 * inputsIn)), 1.0f);

            // Orange active line (from left)
            if (u2 > 0.01f) {
                dl->AddLine(ImVec2(formX, lineY2), ImVec2(formX + underlineW2, lineY2),
                            IM_COL32(255, 122, 24, (int)(255 * inputsIn)), 1.0f + u2);
            }

            ImGui::PopFont();
        }  // End showLoginFields

        // Error message location
        // Dynamic layout for error and button
        float currentY;
        float btnBaseY;

        if (showLoginFields) {
            // Original positioning logic for error
            currentY = inputY + 65 + 55.0f;
            // Button flows naturally after error - add extra space when error shown
            btnBaseY = currentY + (!g_errorMessage.empty() ? 35.0f : 10.0f);
        } else {
            // Centered error for 'Already injected'
            // Header Ends at ~formY - 30.
            // We want button at ~formY + 130.
            // Midpoint ~formY + 50.
            currentY = formY + 50.0f;
            btnBaseY = formY + 130.0f;
        }

        if (!g_errorMessage.empty()) {
            ImGui::PushFont(g_fontRegular);

            float errScale = showLoginFields ? 1.0f : 1.2f;  // Slightly larger for standalone error
            if (errScale != 1.0f)
                ImGui::SetWindowFontScale(errScale);

            ImVec2 errSize = ImGui::CalcTextSize(g_errorMessage.c_str());

            // CalcTextSize ALREADY includes the font scale if SetWindowFontScale is active.
            dl->AddText(ImVec2(centerX - errSize.x * 0.5f, currentY), COL_ERROR, g_errorMessage.c_str());

            if (errScale != 1.0f)
                ImGui::SetWindowFontScale(1.0f);
            ImGui::PopFont();
        }

        // Button - Premium, solid, restful
        float btnAlpha = inputsIn;
        float btnH = 46.0f;
        float btnRadius = 4.0f;

        ImVec2 btnMin(formX, btnBaseY);
        ImVec2 btnMax(formX + formW, btnBaseY + btnH);
        bool btnHovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);

        // Press animation (sink)
        bool pressed = btnHovered && ImGui::IsMouseDown(0);
        g_buttonPressAnim += (pressed ? 1.0f : -1.0f) * dt * 12.0f;
        g_buttonPressAnim = ImClamp(g_buttonPressAnim, 0.0f, 1.0f);

        // Hover animation (lift + brighten)
        static float hoverAnim = 0.0f;
        hoverAnim += (btnHovered ? 1.0f : -1.0f) * dt * 12.0f;
        hoverAnim = ImClamp(hoverAnim, 0.0f, 1.0f);

        // Subtle lift on hover, sink on press
        float lift = hoverAnim * 2.0f * (1.0f - g_buttonPressAnim);
        float sink = g_buttonPressAnim * 1.5f;
        btnMin.y = btnMin.y - lift + sink;
        btnMax.y = btnMax.y - lift + sink;

        // Colors - muted orange, not screaming
        ImVec4 colBase = ImVec4(200 / 255.f, 90 / 255.f, 25 / 255.f, btnAlpha);
        ImVec4 colHover = ImVec4(220 / 255.f, 100 / 255.f, 30 / 255.f, btnAlpha);
        ImVec4 colPress = ImVec4(170 / 255.f, 75 / 255.f, 20 / 255.f, btnAlpha);

        ImVec4 colFinal = ImLerp(colBase, colHover, hoverAnim);
        colFinal = ImLerp(colFinal, colPress, g_buttonPressAnim);

        // Diffuse shadow - soft, not sharp
        if (btnAlpha > 0.1f) {
            float shadowAlpha = 25.0f * btnAlpha * (1.0f - g_buttonPressAnim * 0.5f);
            dl->AddRectFilled(ImVec2(btnMin.x + 3, btnMin.y + 5), ImVec2(btnMax.x - 3, btnMax.y + 7),
                              IM_COL32(0, 0, 0, (int)(shadowAlpha)), btnRadius);
            dl->AddRectFilled(ImVec2(btnMin.x + 1, btnMin.y + 3), ImVec2(btnMax.x - 1, btnMax.y + 5),
                              IM_COL32(0, 0, 0, (int)(shadowAlpha * 0.6f)), btnRadius);
        }

        // Main button fill
        dl->AddRectFilled(btnMin, btnMax, ImGui::ColorConvertFloat4ToU32(colFinal), btnRadius);

        // Button text
        ImGui::PushFont(g_fontRegular);
        const char* btnText = showLoginFields ? "Sign In" : "Close";
        ImVec2 btnTextSize = ImGui::CalcTextSize(btnText);
        float btnTextX = btnMin.x + (btnMax.x - btnMin.x - btnTextSize.x) * 0.5f;
        float btnTextY = btnMin.y + (btnMax.y - btnMin.y - btnTextSize.y) * 0.5f;
        dl->AddText(ImVec2(btnTextX, btnTextY), IM_COL32(255, 255, 255, (int)(255 * btnAlpha)), btnText);
        ImGui::PopFont();

        if (btnHovered && ImGui::IsMouseClicked(0)) {
            if (showLoginFields) {
                g_loginButtonPressed = true;
            } else {
                g_shouldClose = true;  // Just close if it's the Close button
            }
        }

        ImGui::PopStyleVar();  // End formAlpha
    } else if (g_animStage == STAGE_WAITING) {
        // Title "Cobble" - same landing animation as login
        ImGui::PushFont(g_fontLogo);
        const char* title = "Cobble";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(centerX - titleSize.x * 0.5f, titleY), IM_COL32(255, 255, 255, 255), title);
        ImGui::PopFont();

        // Subtitle "Ready to Inject"
        ImGui::PushFont(g_fontRegular);
        std::string readyTextStr = OBFUSCATE_STRING("Ready to Inject");
        ImVec2 readySize = ImGui::CalcTextSize(readyTextStr.c_str());
        dl->AddText(ImVec2(centerX - readySize.x * 0.5f, titleY + 45), IM_COL32(150, 150, 150, 180),
                    readyTextStr.c_str());
        ImGui::PopFont();

        // Spinner - smooth continuous rotation
        float phase = spinnerPhase;
        DrawSpinner(dl, ImVec2(centerX, formY + 40), 22.0f, 2.5f, phase);

        ImGui::PushFont(g_fontRegular);
        std::string subTextStr = OBFUSCATE_STRING("Open Minecraft to continue");
        const char* subText = subTextStr.c_str();
        ImVec2 subSize2 = ImGui::CalcTextSize(subText);
        dl->AddText(ImVec2(centerX - subSize2.x * 0.5f, formY + 80), COL_TEXT_DIM, subText);
        ImGui::PopFont();
    } else if (g_animStage == STAGE_INJECTING) {
        ImGui::PushFont(g_fontLogo);
        const char* title = "Cobble";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(centerX - titleSize.x * 0.5f, titleY), IM_COL32(255, 255, 255, 255), title);
        ImGui::PopFont();

        ImGui::PushFont(g_fontRegular);
        const char* injText = "Injecting...";
        ImVec2 injSize = ImGui::CalcTextSize(injText);
        dl->AddText(ImVec2(centerX - injSize.x * 0.5f, titleY + 45), IM_COL32(200, 200, 200, 210), injText);
        ImGui::PopFont();

        float barW = 300.0f;
        float barH = 2.0f;
        float barX = centerX - barW * 0.5f;
        float barY = formY + 55;  // Slightly lower for better spacing

        // Smoother easing (Quad)
        float p = g_injectProgress;
        float visualP = 1.0f - (1.0f - p) * (1.0f - p);
        float fillW = ImClamp(barW * visualP, 0.0f, barW);

        // Track (very subtle)
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(255, 255, 255, 10), 0.0f);

        if (fillW > 0.5f) {
            // Clean Gradient Bar - minimal glow
            dl->AddRectFilledMultiColor(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH),
                                        IM_COL32(255, 100, 0, 180),   // Darker start
                                        IM_COL32(255, 160, 40, 255),  // Bright end
                                        IM_COL32(255, 160, 40, 255), IM_COL32(255, 100, 0, 180));

            // Minimal lead point - no massive halo
            // Just a small bright dot at the tip
            ImVec2 headPos(barX + fillW, barY + barH * 0.5f);
            dl->AddCircleFilled(headPos, 1.5f, IM_COL32(255, 255, 255, 255));

            // Extremely subtle glow only around the tip
            dl->AddCircleFilled(headPos, 6.0f, IM_COL32(255, 160, 40, 40));
        }

        ImGui::PushFont(g_fontRegular);
        const char* subText = (g_injectProgress >= 0.99f) ? "Finishing..." : "Do not close this window";
        ImVec2 subSize2 = ImGui::CalcTextSize(subText);
        dl->AddText(ImVec2(centerX - subSize2.x * 0.5f, barY + 20), COL_TEXT_DIM, subText);
        ImGui::PopFont();
    } else if (g_animStage == STAGE_SUCCESS) {
        // Minimalist Success: Sleek Checkmark, No Burst/Ring
        float t = ImClamp(g_stageTime / 0.5f, 0.0f, 1.0f);
        float ease = easeOutCubic(t);

        // HIGHER position as requested (was +55)
        float checkY = formY + 40.0f;

        // Checkmark Geometry
        ImVec2 center(centerX, checkY);
        // LARGER scale as requested (was 1.0)
        float scale = 1.5f;

        // Points for checkmark (centered visually)
        ImVec2 p1(centerX - 9.0f * scale, checkY + 1.0f * scale);
        ImVec2 p2(centerX - 2.0f * scale, checkY + 8.0f * scale);
        ImVec2 p3(centerX + 10.0f * scale, checkY - 7.0f * scale);

        // Animate stroke drawing
        // Split into two segments: p1->p2 (35% of time), p2->p3 (65% of time)
        float drawT = ease;  // 0..1

        // Draw
        float strokeWidth = 3.0f;                       // Slightly thicker for larger scale
        ImU32 checkCol = IM_COL32(255, 255, 255, 255);  // Pure white

        if (drawT > 0.0f) {
            float t1 = ImClamp(drawT / 0.35f, 0.0f, 1.0f);
            if (t1 > 0.0f) {
                dl->AddLine(p1, ImLerp(p1, p2, t1), checkCol, strokeWidth);
            }

            if (drawT > 0.35f) {
                float t2 = ImClamp((drawT - 0.35f) / 0.65f, 0.0f, 1.0f);
                dl->AddLine(p2, ImLerp(p2, p3, t2), checkCol, strokeWidth);
            }
        }

        // Optional: Very subtle shadow for depth
        if (drawT > 0.1f) {
            ImU32 shadowCol = IM_COL32(0, 0, 0, 40);
            ImVec2 off(0, 2);
            if (drawT > 0.0f) {
                float t1 = ImClamp(drawT / 0.35f, 0.0f, 1.0f);
                if (t1 > 0.0f) {
                    ImVec2 s1(p1.x + off.x, p1.y + off.y);
                    ImVec2 tVal = ImLerp(p1, p2, t1);
                    ImVec2 s2(tVal.x + off.x, tVal.y + off.y);
                    dl->AddLine(s1, s2, shadowCol, strokeWidth);
                }
                if (drawT > 0.35f) {
                    float t2 = ImClamp((drawT - 0.35f) / 0.65f, 0.0f, 1.0f);
                    ImVec2 s1(p2.x + off.x, p2.y + off.y);
                    ImVec2 tVal = ImLerp(p2, p3, t2);
                    ImVec2 s2(tVal.x + off.x, tVal.y + off.y);
                    dl->AddLine(s1, s2, shadowCol, strokeWidth);
                }
            }
        }

        // Minimal Text "Done"
        // Fades in slightly after checkmark starts
        float textAlpha = easeOutCubic(ImClamp((g_stageTime - 0.2f) / 0.5f, 0.0f, 1.0f));
        if (textAlpha > 0.0f) {
            ImGui::PushFont(g_fontMedium);
            const char* successText = "Done";  // Changed from Active
            ImVec2 successSize = ImGui::CalcTextSize(successText);
            // Slight drift up
            float driftY = (1.0f - textAlpha) * 5.0f;
            // Adjust text Y position relative to larger checkmark
            dl->AddText(ImVec2(centerX - successSize.x * 0.5f, checkY + 35.0f * scale + driftY - 15.0f),
                        IM_COL32(200, 200, 200, (int)(255 * textAlpha)), successText);
            ImGui::PopFont();
        }

        if (g_stageTime > 2.2f) {
            g_animStage = STAGE_EXIT;
            g_stageTime = 0;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Render();

    // Set viewport for proper clipping
    D3D11_VIEWPORT vp = {};
    vp.Width = io.DisplaySize.x;
    vp.Height = io.DisplaySize.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pd3dDeviceContext->RSSetViewports(1, &vp);

    const float clear_color[4] = {0.043f, 0.043f, 0.051f, 1.0f};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(0, 0);  // Present without vsync
    return true;
}

void SetState(State state) {
    g_currentState = state;
    if (state == State::LICENSE_VALID || state == State::WAITING_MINECRAFT) {
        g_animStage = STAGE_WAITING;
        g_stageTime = 0;
    } else if (state == State::AUTHENTICATING) {
        g_animStage = STAGE_INJECTING;
        g_stageTime = 0;
        g_injectProgress = 0.0f;
        g_injectPulseTime = 0.0f;
        // Randomize speed: 4.0s to 6.0s duration
        // Speed = 1/Duration. Range [0.166, 0.25]
        float rnd = fmodf((float)ImGui::GetTime() * 13.0f, 1.0f);
        g_injectTargetSpeed = 0.166f + rnd * (0.25f - 0.166f);
    } else if (state == State::INJECTING_SUCCESS) {
        g_animStage = STAGE_INJECTING;
        g_stageTime = 0;
        g_injectProgress = 0.0f;
        g_injectPulseTime = 0.0f;
        // Randomize speed same as above
        float rnd = fmodf((float)ImGui::GetTime() * 17.0f, 1.0f);
        g_injectTargetSpeed = 0.166f + rnd * (0.25f - 0.166f);
    }
}

void SetMessage(const std::string& msg) {
    g_message = msg;
}
void SetError(const std::string& error) {
    g_errorMessage = error;
    g_currentState = State::ERROR_STATE;
    g_animStage = STAGE_LOGIN;
}
void SetLicenseInfo(const std::string& plan, const std::string& expiry) {
    g_licensePlan = plan;
    g_licenseExpiry = expiry;
}
std::string GetUsername() {
    return std::string(g_username);
}
std::string GetPassword() {
    return std::string(g_password);
}
bool IsLoginButtonPressed() {
    return g_loginButtonPressed;
}
void ClearLoginButton() {
    g_loginButtonPressed = false;
}
}  // namespace LoaderUI
