#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>

#include <algorithm>
#include <functional>

#include "../infra/ext/imgui/imgui.h"
#include "../infra/ext/imgui/imgui_impl_dx11.h"
#include "../infra/ext/imgui/imgui_impl_win32.h"

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_target = nullptr;

static void CreateTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_target);
        backBuffer->Release();
    }
}

static void CleanupTarget() {
    if (g_target) {
        g_target->Release();
        g_target = nullptr;
    }
}

static bool CreateDevice(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                      D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &level,
                                      &g_context) != S_OK)
        return false;
    CreateTarget();
    return true;
}

static void CleanupDevice() {
    CleanupTarget();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT WINAPI WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return true;
    if (message == WM_SIZE && g_device && wParam != SIZE_MINIMIZED) {
        CleanupTarget();
        g_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
        CreateTarget();
        return 0;
    }
    if (message == WM_SYSCOMMAND && (wParam & 0xfff0) == SC_KEYMENU)
        return 0;
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

static void ApplyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.WindowPadding = {0, 0};
    style.FramePadding = {9, 6};
    style.ItemSpacing = {9, 10};
    style.Colors[ImGuiCol_WindowBg] = {0.045f, 0.05f, 0.06f, 1.0f};
    style.Colors[ImGuiCol_ChildBg] = {0.065f, 0.07f, 0.08f, 1.0f};
    style.Colors[ImGuiCol_FrameBg] = {0.09f, 0.095f, 0.11f, 1.0f};
    style.Colors[ImGuiCol_FrameBgHovered] = {0.13f, 0.135f, 0.15f, 1.0f};
    style.Colors[ImGuiCol_Header] = {0.10f, 0.105f, 0.12f, 1.0f};
    style.Colors[ImGuiCol_HeaderHovered] = {0.15f, 0.155f, 0.17f, 1.0f};
    style.Colors[ImGuiCol_CheckMark] = {0.94f, 0.47f, 0.055f, 1.0f};
    style.Colors[ImGuiCol_SliderGrab] = {0.94f, 0.47f, 0.055f, 1.0f};
    style.Colors[ImGuiCol_Border] = {1.0f, 1.0f, 1.0f, 0.08f};
}

static void Card(const char* title, const ImVec2& size, const std::function<void()>& content) {
    ImGui::BeginChild(title, size, ImGuiChildFlags_Borders);
    ImGui::TextColored({0.94f, 0.47f, 0.055f, 1.0f}, "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
    content();
    ImGui::EndChild();
}

static void DrawPreview() {
    static int tab = 2;
    static bool enabled = true;
    static bool movingOnly = true;
    static bool smoothRelease = true;
    static bool flushAttack = true;
    static bool serverPlayer = true;
    static float minDelay = 35.0f;
    static float maxDelay = 120.0f;
    static float distance = 1.8f;
    static float strength = 0.45f;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float scale = (std::clamp)((std::min)((display.x - 32.0f) / 960.0f,
                                                (display.y - 32.0f) / 640.0f),
                                     0.65f, 1.35f);
    const ImVec2 size{960.0f * scale, 640.0f * scale};
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("Cobble GUI Preview", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    const float navWidth = 176.0f * scale;
    ImGui::BeginChild("nav", {navWidth, 0}, ImGuiChildFlags_Borders);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextColored({0.94f, 0.47f, 0.055f, 1.0f}, "  COBBLE");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("  DEVELOPER PREVIEW");
    ImGui::Separator();
    ImGui::Spacing();
    const char* tabs[] = {"Combat", "Visuals", "Movement", "Utility", "Configs", "Settings"};
    for (int i = 0; i < 6; ++i) {
        if (ImGui::Selectable(tabs[i], tab == i, 0, {0, 38.0f * scale}))
            tab = i;
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 42.0f * scale);
    ImGui::Separator();
    ImGui::TextDisabled("  Responsive 960 x 640");
    ImGui::EndChild();
    ImGui::SameLine(0, 9.0f * scale);

    ImGui::BeginChild("content", {0, 0});
    ImGui::SetWindowFontScale(1.25f);
    ImGui::Text("Movement");
    ImGui::SameLine();
    ImGui::TextColored({0.94f, 0.47f, 0.055f, 1.0f}, "/ FakeLag");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Conservative packet buffering with smooth release");
    ImGui::Separator();
    ImGui::Spacing();

    const float cardWidth = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;
    Card("Fake Lag", {cardWidth, 0}, [&] {
        ImGui::Checkbox("Enabled", &enabled);
        const char* modes[] = {"Pulse", "Constant", "Distance", "Adaptive"};
        static int mode = 3;
        ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes));
        ImGui::SliderFloat("Minimum Delay", &minDelay, 20.0f, 250.0f, "%.0f ms");
        ImGui::SliderFloat("Maximum Delay", &maxDelay, 40.0f, 400.0f, "%.0f ms");
        ImGui::SliderFloat("Maximum Distance", &distance, 0.5f, 4.0f, "%.1f blocks");
        ImGui::SliderFloat("Adaptive Strength", &strength, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Moving Only", &movingOnly);
        ImGui::Checkbox("Smooth Release", &smoothRelease);
        ImGui::Checkbox("Flush On Attack", &flushAttack);
    });
    ImGui::SameLine();
    Card("Visuals", {cardWidth, 0}, [&] {
        ImGui::Checkbox("Render Server Player", &serverPlayer);
        static bool filled = true, line = true, status = true;
        ImGui::Checkbox("Filled Render", &filled);
        ImGui::Checkbox("Render Line", &line);
        ImGui::Checkbox("Show Status", &status);
        ImGui::Spacing();
        ImGui::TextDisabled("Preview notes");
        ImGui::Separator();
        ImGui::TextWrapped("The production GUI now keeps stable geometry during opening, scales from both display axes, and uses small rounded corners.");
    });
    ImGui::EndChild();
    ImGui::End();
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    WNDCLASSEX wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr,
                  _T("CobbleGuiPreview"), nullptr};
    RegisterClassEx(&wc);
    HWND window = CreateWindow(wc.lpszClassName, _T("Cobble GUI Preview"), WS_OVERLAPPEDWINDOW,
                               100, 100, 1120, 760, nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDevice(window)) {
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyStyle();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_device, g_context);

    MSG message{};
    while (message.message != WM_QUIT) {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            continue;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawPreview();
        ImGui::Render();
        const float clear[4] = {0.025f, 0.028f, 0.034f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_target, nullptr);
        g_context->ClearRenderTargetView(g_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(window);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}
