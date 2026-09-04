#include "test26.hpp"
#include <Windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui_impl_dx11.h"
#include "../../../infra/ext/imgui/imgui_impl_win32.h"
#include "../../../infra/ext/imgui/imgui_internal.h"
#include "../test6/test30.hpp"

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
HWND g_hwnd = nullptr;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_initialized = false;
bool g_shouldClose = false;

LoaderUI::State g_currentState = LoaderUI::State::INITIALIZING;
std::string g_message = "Starting";
std::string g_errorMessage;
std::string g_licensePlan;
std::string g_licenseExpiry;

char g_username[256] = "";
char g_password[256] = "";
bool g_loginButtonPressed = false;

float g_time = 0.0f;
float g_viewTime = 0.0f;
float g_messageTime = 0.0f;
float g_userFocus = 0.0f;
float g_passFocus = 0.0f;
float g_buttonHover = 0.0f;
float g_buttonPress = 0.0f;
float g_closeHover = 0.0f;
float g_minHover = 0.0f;

ImFont* g_fontBody = nullptr;
ImFont* g_fontBodyStrong = nullptr;
ImFont* g_fontDisplay = nullptr;
ImFont* g_fontSmall = nullptr;

const ImU32 COL_BG = IM_COL32(9, 10, 12, 255);
const ImU32 COL_SURFACE = IM_COL32(15, 17, 20, 255);
const ImU32 COL_SURFACE_HOVER = IM_COL32(18, 20, 23, 255);
const ImU32 COL_LINE = IM_COL32(42, 45, 50, 255);
const ImU32 COL_LINE_SOFT = IM_COL32(31, 34, 38, 255);
const ImU32 COL_TEXT = IM_COL32(238, 239, 241, 255);
const ImU32 COL_MUTED = IM_COL32(137, 143, 151, 255);
const ImU32 COL_MUTED_2 = IM_COL32(92, 98, 106, 255);
const ImU32 COL_ACCENT = IM_COL32(238, 119, 45, 255);
const ImU32 COL_ACCENT_HOVER = IM_COL32(248, 133, 57, 255);
const ImU32 COL_ACCENT_SOFT = IM_COL32(238, 119, 45, 44);
const ImU32 COL_ERROR = IM_COL32(236, 94, 94, 255);
const ImU32 COL_SUCCESS = IM_COL32(111, 220, 160, 255);

float clamp01(float v) {
    return ImClamp(v, 0.0f, 1.0f);
}

float easeOutCubic(float x) {
    x = clamp01(x);
    return 1.0f - powf(1.0f - x, 3.0f);
}

float easeOutQuint(float x) {
    x = clamp01(x);
    return 1.0f - powf(1.0f - x, 5.0f);
}

float smoothTo(float current, float target, float speed, float dt) {
    return current + (target - current) * (1.0f - expf(-speed * dt));
}

ImU32 withAlpha(ImU32 color, float alpha) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
    c.w *= clamp01(alpha);
    return ImGui::ColorConvertFloat4ToU32(c);
}

void AddEllipse(ImDrawList* dl, ImVec2 center, float rx, float ry, ImU32 color, int segments = 80, float thickness = 1.0f) {
    ImVec2 prev(center.x + rx, center.y);
    for (int i = 1; i <= segments; ++i) {
        float a = (float)i / (float)segments * 2.0f * IM_PI;
        ImVec2 p(center.x + cosf(a) * rx, center.y + sinf(a) * ry);
        dl->AddLine(prev, p, color, thickness);
        prev = p;
    }
}

void AddArc(ImDrawList* dl, ImVec2 center, float radius, float a0, float a1, ImU32 color, float thickness, int segments = 24) {
    ImVec2 prev(center.x + cosf(a0) * radius, center.y + sinf(a0) * radius);
    for (int i = 1; i <= segments; ++i) {
        float t = (float)i / (float)segments;
        float a = a0 + (a1 - a0) * t;
        ImVec2 p(center.x + cosf(a) * radius, center.y + sinf(a) * radius);
        dl->AddLine(prev, p, color, thickness);
        prev = p;
    }
}

void DrawWindowControls(ImDrawList* dl, ImVec2 size, float dt) {
    const float y = 26.0f;
    const float closeX = size.x - 27.0f;
    const float minX = size.x - 61.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    bool minHover = mouse.x >= minX - 10.0f && mouse.x <= minX + 10.0f && mouse.y >= y - 10.0f && mouse.y <= y + 10.0f;
    bool closeHover = mouse.x >= closeX - 10.0f && mouse.x <= closeX + 10.0f && mouse.y >= y - 10.0f && mouse.y <= y + 10.0f;

    g_minHover = smoothTo(g_minHover, minHover ? 1.0f : 0.0f, 18.0f, dt);
    g_closeHover = smoothTo(g_closeHover, closeHover ? 1.0f : 0.0f, 18.0f, dt);

    ImU32 minCol = ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(COL_MUTED_2), ImGui::ColorConvertU32ToFloat4(COL_TEXT), g_minHover));
    ImU32 closeCol = ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(COL_MUTED_2), ImGui::ColorConvertU32ToFloat4(COL_ERROR), g_closeHover));

    dl->AddLine(ImVec2(minX - 5.0f, y), ImVec2(minX + 5.0f, y), minCol, 1.4f);
    dl->AddLine(ImVec2(closeX - 4.5f, y - 4.5f), ImVec2(closeX + 4.5f, y + 4.5f), closeCol, 1.4f);
    dl->AddLine(ImVec2(closeX + 4.5f, y - 4.5f), ImVec2(closeX - 4.5f, y + 4.5f), closeCol, 1.4f);

    if (minHover && ImGui::IsMouseClicked(0))
        ShowWindow(g_hwnd, SW_MINIMIZE);
    if (closeHover && ImGui::IsMouseClicked(0))
        g_shouldClose = true;
}

void DrawBackdrop(ImDrawList* dl, ImVec2 size) {
    dl->AddRectFilled(ImVec2(0, 0), size, COL_BG, 16.0f);
    dl->AddRect(ImVec2(0.5f, 0.5f), ImVec2(size.x - 0.5f, size.y - 0.5f), COL_LINE_SOFT, 16.0f, 0, 1.0f);

    const float sepX = 400.0f;
    dl->AddLine(ImVec2(sepX, 66.0f), ImVec2(sepX, size.y - 58.0f), COL_LINE_SOFT, 1.0f);

    for (int i = 0; i < 7; ++i) {
        float y = 94.0f + i * 48.0f;
        dl->AddLine(ImVec2(54.0f, y), ImVec2(348.0f, y), IM_COL32(255, 255, 255, i == 3 ? 10 : 6), 1.0f);
    }
}

void DrawBrandField(ImDrawList* dl, float alpha) {
    ImVec2 center(206.0f, 256.0f);
    float t = g_time;

    AddEllipse(dl, center, 112.0f, 112.0f, withAlpha(COL_LINE_SOFT, alpha * 0.85f), 90, 1.0f);
    AddEllipse(dl, center, 73.0f, 73.0f, withAlpha(COL_LINE, alpha * 0.62f), 72, 1.0f);
    AddEllipse(dl, center, 35.0f, 35.0f, withAlpha(COL_LINE, alpha * 0.44f), 52, 1.0f);

    for (int i = 0; i < 3; ++i) {
        float radius = 35.0f + i * 38.0f;
        float speed = 0.34f + i * 0.11f;
        float a = t * speed + i * 2.1f;
        ImVec2 p(center.x + cosf(a) * radius, center.y + sinf(a) * radius);
        dl->AddCircleFilled(p, 2.4f + i * 0.35f, withAlpha(COL_ACCENT, alpha));
        dl->AddCircle(p, 6.5f + i * 1.5f, withAlpha(COL_ACCENT_SOFT, alpha * 0.8f), 24, 1.0f);
    }

    float sweep = fmodf(t * 0.23f, 1.0f) * 2.0f * IM_PI;
    AddArc(dl, center, 112.0f, sweep, sweep + 0.72f, withAlpha(COL_ACCENT, alpha * 0.85f), 1.8f, 20);
    AddArc(dl, center, 73.0f, -sweep * 0.72f, -sweep * 0.72f + 0.46f, withAlpha(COL_TEXT, alpha * 0.38f), 1.2f, 16);

    float pulse = 0.5f + 0.5f * sinf(t * 2.1f);
    dl->AddCircleFilled(center, 4.0f + pulse * 1.5f, withAlpha(COL_TEXT, alpha * (0.78f + pulse * 0.22f)));

    ImGui::PushFont(g_fontDisplay);
    dl->AddText(ImVec2(64.0f, 58.0f), withAlpha(COL_TEXT, alpha), "Cobble");
    ImGui::PopFont();

    ImGui::PushFont(g_fontSmall);
    dl->AddText(ImVec2(65.0f, 103.0f), withAlpha(COL_MUTED, alpha), "Loader");
    ImGui::PopFont();
}

void DrawActivityMark(ImDrawList* dl, ImVec2 center, float alpha, bool calm) {
    float t = g_time;
    float speed = calm ? 0.52f : 1.15f;
    float pulse = 0.5f + 0.5f * sinf(t * (calm ? 1.6f : 2.7f));

    dl->AddCircle(center, 34.0f, withAlpha(COL_LINE, alpha), 64, 1.0f);
    dl->AddCircle(center, 19.0f, withAlpha(COL_LINE_SOFT, alpha), 48, 1.0f);

    for (int i = 0; i < 6; ++i) {
        float a = t * speed + i * (IM_PI / 3.0f);
        float len = 0.30f + 0.10f * sinf(t * 2.0f + i);
        float a0 = a;
        float a1 = a + len;
        float local = 0.34f + 0.66f * ((float)i / 5.0f);
        AddArc(dl, center, 34.0f, a0, a1, withAlpha(COL_ACCENT, alpha * local), 2.0f, 7);
    }

    float a = -t * speed * 0.76f;
    ImVec2 node(center.x + cosf(a) * 19.0f, center.y + sinf(a) * 19.0f);
    dl->AddCircleFilled(node, 2.2f, withAlpha(COL_TEXT, alpha));
    dl->AddCircleFilled(center, 2.6f + pulse, withAlpha(COL_TEXT, alpha * (0.72f + pulse * 0.28f)));
}

void DrawStatusText(ImDrawList* dl, ImVec2 center, const char* title, const char* detail, float alpha) {
    ImGui::PushFont(g_fontBodyStrong);
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(center.x - titleSize.x * 0.5f, center.y + 58.0f), withAlpha(COL_TEXT, alpha), title);
    ImGui::PopFont();

    if (detail && detail[0]) {
        ImGui::PushFont(g_fontBody);
        ImVec2 detailSize = ImGui::CalcTextSize(detail);
        dl->AddText(ImVec2(center.x - detailSize.x * 0.5f, center.y + 91.0f), withAlpha(COL_MUTED, alpha), detail);
        ImGui::PopFont();
    }
}

void DrawInput(const char* id, const char* label, char* value, size_t valueSize, ImVec2 pos, float width, bool password, float& focusAnim, float dt, bool* enterPressed = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushFont(g_fontSmall);
    dl->AddText(pos, COL_MUTED, label);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, 11.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(COL_SURFACE));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(COL_SURFACE_HOVER));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(COL_SURFACE_HOVER));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(COL_LINE));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(COL_TEXT));
    ImGui::PushFont(g_fontBody);
    ImGui::PushItemWidth(width);

    ImGuiInputTextFlags flags = password ? ImGuiInputTextFlags_Password : ImGuiInputTextFlags_None;
    if (enterPressed)
        flags |= ImGuiInputTextFlags_EnterReturnsTrue;
    bool enter = ImGui::InputText(id, value, valueSize, flags);
    bool active = ImGui::IsItemActive();
    focusAnim = smoothTo(focusAnim, active ? 1.0f : 0.0f, 18.0f, dt);

    ImGui::PopItemWidth();
    ImGui::PopFont();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);

    if (focusAnim > 0.01f) {
        dl->AddRect(ImVec2(pos.x - 0.5f, pos.y + 21.5f), ImVec2(pos.x + width + 0.5f, pos.y + 67.5f), withAlpha(COL_ACCENT, focusAnim), 9.5f, 0, 1.0f + focusAnim * 0.5f);
    }

    if (enterPressed)
        *enterPressed = enter;
}

bool DrawPrimaryButton(ImDrawList* dl, ImVec2 min, ImVec2 max, const char* label, float dt) {
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool hover = mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
    bool down = hover && ImGui::IsMouseDown(0);

    g_buttonHover = smoothTo(g_buttonHover, hover ? 1.0f : 0.0f, 16.0f, dt);
    g_buttonPress = smoothTo(g_buttonPress, down ? 1.0f : 0.0f, 22.0f, dt);

    float inset = g_buttonPress * 1.0f;
    ImVec2 a(min.x + inset, min.y + inset);
    ImVec2 b(max.x - inset, max.y - inset);
    ImVec4 c0 = ImGui::ColorConvertU32ToFloat4(COL_ACCENT);
    ImVec4 c1 = ImGui::ColorConvertU32ToFloat4(COL_ACCENT_HOVER);
    ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImLerp(c0, c1, g_buttonHover));
    dl->AddRectFilled(a, b, fill, 9.0f);

    float lineX = a.x + 14.0f + (b.x - a.x - 28.0f) * fmodf(g_time * 0.22f, 1.0f);
    if (hover)
        dl->AddLine(ImVec2(lineX, b.y - 3.0f), ImVec2(ImMin(lineX + 24.0f, b.x - 8.0f), b.y - 3.0f), IM_COL32(255, 255, 255, (int)(75 * g_buttonHover)), 1.0f);

    ImGui::PushFont(g_fontBodyStrong);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2((a.x + b.x - textSize.x) * 0.5f, (a.y + b.y - textSize.y) * 0.5f), IM_COL32(18, 12, 8, 255), label);
    ImGui::PopFont();

    return hover && ImGui::IsMouseClicked(0);
}

void DrawLoginView(ImDrawList* dl, ImVec2 size, float dt, float alpha) {
    float in = easeOutQuint(g_viewTime / 0.42f);
    float offset = (1.0f - in) * 18.0f;
    float x = 456.0f + offset;
    float y = 110.0f;
    float w = size.x - x - 62.0f;

    std::string alreadyInjected = OBFUSCATE_STRING("Already injected");
    bool closeOnly = g_errorMessage == alreadyInjected;

    ImGui::PushFont(g_fontDisplay);
    const char* title = closeOnly ? "Already running" : "Sign in";
    dl->AddText(ImVec2(x, y), withAlpha(COL_TEXT, alpha * in), title);
    ImGui::PopFont();

    ImGui::PushFont(g_fontBody);
    const char* sub = closeOnly ? "Cobble is already active in this session." : "Use your Cobble account to continue.";
    dl->AddText(ImVec2(x, y + 48.0f), withAlpha(COL_MUTED, alpha * in), sub);
    ImGui::PopFont();

    if (!closeOnly) {
        bool enter = false;
        DrawInput("##username", "Username", g_username, sizeof(g_username), ImVec2(x, y + 102.0f), w, false, g_userFocus, dt);
        DrawInput("##password", "Password", g_password, sizeof(g_password), ImVec2(x, y + 190.0f), w, true, g_passFocus, dt, &enter);

        if (!g_errorMessage.empty()) {
            ImGui::PushFont(g_fontSmall);
            dl->AddText(ImVec2(x, y + 275.0f), withAlpha(COL_ERROR, alpha * in), g_errorMessage.c_str());
            ImGui::PopFont();
        }

        bool click = DrawPrimaryButton(dl, ImVec2(x, y + 317.0f), ImVec2(x + w, y + 365.0f), "Continue", dt);
        if ((click || enter) && g_username[0] && g_password[0])
            g_loginButtonPressed = true;
        else if ((click || enter) && (!g_username[0] || !g_password[0]))
            g_errorMessage = OBFUSCATE_STRING("Enter username and password");
    } else {
        if (DrawPrimaryButton(dl, ImVec2(x, y + 180.0f), ImVec2(x + w, y + 228.0f), "Close", dt))
            g_shouldClose = true;
    }
}

void DrawBusyView(ImDrawList* dl, ImVec2 size, float alpha) {
    float in = easeOutQuint(g_viewTime / 0.34f);
    ImVec2 center((400.0f + size.x) * 0.5f, 222.0f);
    DrawActivityMark(dl, center, alpha * in, false);

    const char* title = g_message.empty() ? "Working" : g_message.c_str();
    DrawStatusText(dl, center, title, "Current task in progress", alpha * in);

    float left = 452.0f;
    float right = size.x - 62.0f;
    float y = 390.0f;
    float moving = fmodf(g_time * 0.22f, 1.0f);
    dl->AddLine(ImVec2(left, y), ImVec2(right, y), withAlpha(COL_LINE_SOFT, alpha * in), 1.0f);
    float x = left + (right - left) * moving;
    dl->AddCircleFilled(ImVec2(x, y), 2.2f, withAlpha(COL_ACCENT, alpha * in));
}

void DrawWaitingView(ImDrawList* dl, ImVec2 size, float alpha) {
    float in = easeOutQuint(g_viewTime / 0.34f);
    ImVec2 center((400.0f + size.x) * 0.5f, 218.0f);
    DrawActivityMark(dl, center, alpha * in, true);

    const char* title = g_message.empty() ? "Waiting for Minecraft" : g_message.c_str();
    DrawStatusText(dl, center, title, "Open Lunar Client to continue", alpha * in);

    float blink = 0.46f + 0.54f * (0.5f + 0.5f * sinf(g_time * 2.2f));
    dl->AddCircleFilled(ImVec2(center.x - 61.0f, center.y + 92.0f), 2.4f, withAlpha(COL_ACCENT, alpha * in * blink));
}

void DrawSuccessView(ImDrawList* dl, ImVec2 size, float alpha) {
    float in = easeOutQuint(g_viewTime / 0.28f);
    ImVec2 center((400.0f + size.x) * 0.5f, 218.0f);

    float ringT = easeOutCubic(g_viewTime / 0.38f);
    float radius = 25.0f + ringT * 13.0f;
    dl->AddCircle(center, radius, withAlpha(COL_SUCCESS, alpha * in * 0.82f), 64, 1.4f);

    ImVec2 p1(center.x - 11.0f, center.y + 1.0f);
    ImVec2 p2(center.x - 3.0f, center.y + 9.0f);
    ImVec2 p3(center.x + 13.0f, center.y - 9.0f);
    float draw = easeOutCubic((g_viewTime - 0.08f) / 0.34f);
    if (draw > 0.0f) {
        float first = clamp01(draw / 0.44f);
        dl->AddLine(p1, ImLerp(p1, p2, first), withAlpha(COL_SUCCESS, alpha), 2.8f);
        if (draw > 0.44f) {
            float second = clamp01((draw - 0.44f) / 0.56f);
            dl->AddLine(p2, ImLerp(p2, p3, second), withAlpha(COL_SUCCESS, alpha), 2.8f);
        }
    }

    DrawStatusText(dl, center, "Ready", "Launch complete", alpha * easeOutCubic((g_viewTime - 0.12f) / 0.30f));
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_NCHITTEST: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hWnd, &pt);
            RECT rect;
            GetClientRect(hWnd, &rect);
            if (pt.y < 48 && pt.x < rect.right - 86)
                return HTCAPTION;
            break;
        }
        case WM_SIZE:
            if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                if (g_mainRenderTargetView) {
                    g_mainRenderTargetView->Release();
                    g_mainRenderTargetView = nullptr;
                }
                g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                ID3D11Texture2D* backBuffer = nullptr;
                g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
                g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
                if (backBuffer)
                    backBuffer->Release();
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

ImFont* LoadFont(ImGuiIO& io, const char* primary, const char* fallback, float size) {
    ImFont* font = io.Fonts->AddFontFromFileTTF(primary, size);
    if (!font && fallback)
        font = io.Fonts->AddFontFromFileTTF(fallback, size);
    if (!font)
        font = io.Fonts->AddFontDefault();
    return font;
}
}

namespace LoaderUI {
bool Init() {
    if (g_initialized)
        return true;

    SetProcessDPIAware();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CobbleLoader";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    const int windowW = 820;
    const int windowH = 500;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(0, L"CobbleLoader", L"Cobble", WS_POPUP,
                             (screenW - windowW) / 2, (screenH - windowH) / 2,
                             windowW, windowH, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd)
        return false;

    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
    COLORREF borderColor = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = windowW;
    sd.BufferDesc.Height = windowH;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               featureLevels, 2, D3D11_SDK_VERSION, &sd,
                                               &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(hr))
        return false;

    ID3D11Texture2D* backBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    if (backBuffer)
        backBuffer->Release();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    g_fontBody = LoadFont(io, "C:\\Windows\\Fonts\\segoeui.ttf", nullptr, 16.0f);
    g_fontBodyStrong = LoadFont(io, "C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf", 17.0f);
    g_fontDisplay = LoadFont(io, "C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\seguisb.ttf", 35.0f);
    g_fontSmall = LoadFont(io, "C:\\Windows\\Fonts\\segoeui.ttf", nullptr, 13.0f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.WindowBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    g_time = 0.0f;
    g_viewTime = 0.0f;
    g_messageTime = 0.0f;
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
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }

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
    float dt = ImMin(io.DeltaTime, 0.05f);
    g_time += dt;
    g_viewTime += dt;
    g_messageTime += dt;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 size = io.DisplaySize;

    DrawBackdrop(dl, size);
    DrawBrandField(dl, 1.0f);
    DrawWindowControls(dl, size, dt);

    float alpha = easeOutCubic(g_viewTime / 0.20f);

    if (g_currentState == State::LOGIN_REQUIRED || g_currentState == State::ERROR_STATE) {
        DrawLoginView(dl, size, dt, alpha);
    } else if (g_currentState == State::WAITING_MINECRAFT || g_currentState == State::LICENSE_VALID) {
        DrawWaitingView(dl, size, alpha);
    } else if (g_currentState == State::INJECTING_SUCCESS) {
        DrawSuccessView(dl, size, alpha);
        if (g_viewTime > 0.82f)
            g_shouldClose = true;
    } else {
        DrawBusyView(dl, size, alpha);
    }

    ImGui::End();
    ImGui::Render();

    D3D11_VIEWPORT vp = {};
    vp.Width = io.DisplaySize.x;
    vp.Height = io.DisplaySize.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pd3dDeviceContext->RSSetViewports(1, &vp);

    const float clear[4] = {9.0f / 255.0f, 10.0f / 255.0f, 12.0f / 255.0f, 1.0f};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);

    return !g_shouldClose;
}

void SetState(State state) {
    if (g_currentState != state) {
        g_currentState = state;
        g_viewTime = 0.0f;
    }

    if (state != State::ERROR_STATE && state != State::LOGIN_REQUIRED)
        g_errorMessage.clear();
}

void SetMessage(const std::string& msg) {
    if (g_message != msg) {
        g_message = msg;
        g_messageTime = 0.0f;
    }
}

void SetError(const std::string& error) {
    g_errorMessage = error;
    g_currentState = State::ERROR_STATE;
    g_viewTime = 0.0f;
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
}
