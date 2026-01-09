#include "loader_window.hpp"
#include "../../../infra/ext/imgui/imgui.h"
#include "../../../infra/ext/imgui/imgui_internal.h"
#include "../../../infra/ext/imgui/imgui_impl_win32.h"
#include "../../../infra/ext/imgui/imgui_impl_opengl2.h"
#include <Windows.h>
#include <gl/GL.h>
#include <string>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    HWND g_hwnd = nullptr;
    HDC g_hdc = nullptr;
    HGLRC g_hglrc = nullptr;
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

    // Easing
    float easeOutCubic(float x) { return 1.0f - powf(1.0f - x, 3.0f); }
    float easeInOutCubic(float x) { return x < 0.5f ? 4.0f * x * x * x : 1.0f - powf(-2.0f * x + 2.0f, 3.0f) / 2.0f; }
    float easeOutBack(float x) { const float c1 = 1.70158f; const float c3 = c1 + 1.0f; return 1.0f + c3 * powf(x - 1.0f, 3.0f) + c1 * powf(x - 1.0f, 2.0f); }

    ImFont* g_fontRegular = nullptr;
    ImFont* g_fontMedium = nullptr;
    ImFont* g_fontLogo = nullptr;

    LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
        switch (msg) {
            case WM_CLOSE: g_shouldClose = true; return 0;
            case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    void DrawSpinner(ImDrawList* dl, ImVec2 center, float radius, float thickness, float t) {
        const int segments = 30;
        float start = t * 6.28318f;
        for (int i = 0; i < segments; i++) {
            float a1 = start + (float)i / segments * 3.14159f;
            float a2 = start + (float)(i + 1) / segments * 3.14159f;
            float alpha = (float)i / segments;
            ImU32 col = IM_COL32(255, 122, 24, (int)(alpha * 255));
            dl->AddLine(
                ImVec2(center.x + cosf(a1) * radius, center.y + sinf(a1) * radius),
                ImVec2(center.x + cosf(a2) * radius, center.y + sinf(a2) * radius),
                col, thickness
            );
        }
    }

    void DrawGradientButton(ImDrawList* dl, ImVec2 min, ImVec2 max, const char* text, bool hovered, bool* clicked) {
        float lift = hovered ? 2.0f : 0.0f;
        min.y -= lift; max.y -= lift;
        
        // Shadow
        dl->AddRectFilled(ImVec2(min.x + 4, min.y + 6), ImVec2(max.x - 4, max.y + 6), IM_COL32(255, 122, 24, 40), 10.0f);
        
        // Gradient fill
        dl->AddRectFilledMultiColor(min, max, COL_ORANGE, COL_ORANGE_LIGHT, COL_ORANGE_LIGHT, COL_ORANGE);
        dl->AddRect(min, max, IM_COL32(255, 255, 255, 30), 10.0f);
        
        // Text
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos = ImVec2(min.x + (max.x - min.x - textSize.x) * 0.5f, min.y + (max.y - min.y - textSize.y) * 0.5f);
        dl->AddText(textPos, COL_TEXT, text);
        
        if (hovered && ImGui::IsMouseClicked(0)) *clicked = true;
    }
}

namespace LoaderUI {
    bool Init() {
        if (g_initialized) return true;

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
        int windowW = 420, windowH = 520;

        g_hwnd = CreateWindowExW(0, L"CobbleLoader", L"Cobble", WS_POPUP,
            (screenW - windowW) / 2, (screenH - windowH) / 2, windowW, windowH,
            nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_hwnd) return false;

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;

        g_hdc = GetDC(g_hwnd);
        SetPixelFormat(g_hdc, ChoosePixelFormat(g_hdc, &pfd), &pfd);
        g_hglrc = wglCreateContext(g_hdc);
        wglMakeCurrent(g_hdc, g_hglrc);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        g_fontRegular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f);
        g_fontMedium = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 20.0f);
        g_fontLogo = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 36.0f);

        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImColor(11, 11, 13, 255);
        ImGui::GetStyle().WindowPadding = ImVec2(0, 0);
        ImGui::GetStyle().WindowBorderSize = 0;

        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplOpenGL2_Init();

        ShowWindow(g_hwnd, SW_SHOW);
        g_initialized = true;
        return true;
    }

    void Shutdown() {
        if (!g_initialized) return;
        ImGui_ImplOpenGL2_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        wglMakeCurrent(nullptr, nullptr);
        if (g_hglrc) wglDeleteContext(g_hglrc);
        if (g_hdc) ReleaseDC(g_hwnd, g_hdc);
        if (g_hwnd) DestroyWindow(g_hwnd);
        g_initialized = false;
    }

    bool Render() {
        if (!g_initialized || g_shouldClose) return false;

        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) return false;
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime;
        g_stageTime += dt;
        
        // Update stage based on state
        if (g_currentState == State::LOGIN_REQUIRED || g_currentState == State::ERROR_STATE) {
            if (g_animStage < STAGE_LOGIN) g_animStage = STAGE_LOGIN;
        } else if (g_currentState == State::LICENSE_VALID || g_currentState == State::WAITING_MINECRAFT) {
            if (g_animStage < STAGE_WAITING) { g_animStage = STAGE_WAITING; g_stageTime = 0; }
        }
        
        // Fade animation
        if (g_animStage == STAGE_INTRO) {
            g_fadeAlpha = ImClamp(g_stageTime / 0.5f, 0.0f, 1.0f);
            if (g_stageTime > 1.5f) { g_animStage = STAGE_LOGIN; g_stageTime = 0; }
        } else if (g_animStage == STAGE_EXIT) {
            g_fadeAlpha = 1.0f - ImClamp(g_stageTime / 0.3f, 0.0f, 1.0f);
            if (g_stageTime > 0.5f) { g_shouldClose = true; return false; }
        } else {
            g_fadeAlpha = 1.0f;
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_fadeAlpha);
        ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winSize = io.DisplaySize;

        // Background
        dl->AddRectFilled(ImVec2(0, 0), winSize, COL_BG);

        // Card
        float cardW = 360, cardH = 420;
        float cardY = g_animStage == STAGE_INTRO ? 
            (winSize.y - cardH) * 0.5f + 30.0f * (1.0f - easeOutCubic(ImClamp(g_stageTime / 0.5f, 0.0f, 1.0f))) :
            (winSize.y - cardH) * 0.5f;
        ImVec2 cardMin((winSize.x - cardW) * 0.5f, cardY);
        ImVec2 cardMax(cardMin.x + cardW, cardMin.y + cardH);

        // Card shadow
        dl->AddRectFilled(ImVec2(cardMin.x + 10, cardMin.y + 20), ImVec2(cardMax.x - 10, cardMax.y + 20), IM_COL32(0, 0, 0, 50), 20.0f);
        dl->AddRectFilled(cardMin, cardMax, COL_CARD, 12.0f);
        dl->AddRect(cardMin, cardMax, COL_BORDER, 12.0f);

        // Logo
        ImGui::PushFont(g_fontLogo);
        const char* logo = "COBBLE";
        ImVec2 logoSize = ImGui::CalcTextSize(logo);
        dl->AddText(ImVec2(cardMin.x + (cardW - logoSize.x) * 0.5f, cardMin.y + 40), COL_TEXT, logo);
        ImGui::PopFont();

        // Subtitle
        ImGui::PushFont(g_fontRegular);
        const char* subtitle = "Internal Minecraft Client";
        ImVec2 subSize = ImGui::CalcTextSize(subtitle);
        dl->AddText(ImVec2(cardMin.x + (cardW - subSize.x) * 0.5f, cardMin.y + 85), COL_TEXT_DIM, subtitle);
        ImGui::PopFont();

        float contentY = cardMin.y + 130;

        // Stage-specific content
        if (g_animStage == STAGE_INTRO) {
            // Spinner during intro
            DrawSpinner(dl, ImVec2(cardMin.x + cardW * 0.5f, contentY + 80), 20.0f, 3.0f, (float)ImGui::GetTime() * 2.0f);
            
            ImGui::PushFont(g_fontRegular);
            const char* loadingText = "Initializing...";
            ImVec2 loadSize = ImGui::CalcTextSize(loadingText);
            dl->AddText(ImVec2(cardMin.x + (cardW - loadSize.x) * 0.5f, contentY + 120), COL_TEXT_DIM, loadingText);
            ImGui::PopFont();
        }
        else if (g_animStage == STAGE_LOGIN) {
            float slideIn = easeOutCubic(ImClamp(g_stageTime / 0.4f, 0.0f, 1.0f));
            float inputX = cardMin.x + 30 + (1.0f - slideIn) * 50.0f;
            float inputW = cardW - 60;
            
            ImGui::PushFont(g_fontRegular);
            
            // Username
            dl->AddText(ImVec2(inputX, contentY), COL_TEXT_DIM, "Username");
            ImVec2 usernameMin(inputX, contentY + 25);
            ImVec2 usernameMax(inputX + inputW, contentY + 73);
            dl->AddRectFilled(usernameMin, usernameMax, COL_INPUT, 8.0f);
            dl->AddRect(usernameMin, usernameMax, g_username[0] ? COL_ORANGE : COL_BORDER, 8.0f);
            
            ImGui::SetCursorPos(ImVec2(inputX - cardMin.x + 15, contentY + 25 - cardMin.y + 12));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
            ImGui::PushItemWidth(inputW - 30);
            ImGui::InputText("##user", g_username, 256);
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            // Password  
            dl->AddText(ImVec2(inputX, contentY + 90), COL_TEXT_DIM, "Password");
            ImVec2 passMin(inputX, contentY + 115);
            ImVec2 passMax(inputX + inputW, contentY + 163);
            dl->AddRectFilled(passMin, passMax, COL_INPUT, 8.0f);
            dl->AddRect(passMin, passMax, g_password[0] ? COL_ORANGE : COL_BORDER, 8.0f);
            
            ImGui::SetCursorPos(ImVec2(inputX - cardMin.x + 15, contentY + 115 - cardMin.y + 12));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
            ImGui::PushItemWidth(inputW - 30);
            ImGui::InputText("##pass", g_password, 256, ImGuiInputTextFlags_Password);
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            // Error message
            if (!g_errorMessage.empty()) {
                ImVec2 errSize = ImGui::CalcTextSize(g_errorMessage.c_str());
                dl->AddText(ImVec2(cardMin.x + (cardW - errSize.x) * 0.5f, contentY + 180), COL_ERROR, g_errorMessage.c_str());
            }

            // Login button
            ImVec2 btnMin(inputX, contentY + 210);
            ImVec2 btnMax(inputX + inputW, contentY + 258);
            bool btnHovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);
            DrawGradientButton(dl, btnMin, btnMax, "Login", btnHovered, &g_loginButtonPressed);
            
            ImGui::PopFont();
        }
        else if (g_animStage == STAGE_WAITING || g_animStage == STAGE_INJECTING) {
            DrawSpinner(dl, ImVec2(cardMin.x + cardW * 0.5f, contentY + 60), 25.0f, 3.0f, (float)ImGui::GetTime() * 2.0f);
            
            ImGui::PushFont(g_fontMedium);
            const char* statusText = g_message.empty() ? "Waiting for Minecraft..." : g_message.c_str();
            ImVec2 statusSize = ImGui::CalcTextSize(statusText);
            dl->AddText(ImVec2(cardMin.x + (cardW - statusSize.x) * 0.5f, contentY + 110), COL_TEXT, statusText);
            ImGui::PopFont();
            
            if (!g_licensePlan.empty()) {
                ImGui::PushFont(g_fontRegular);
                std::string licenseText = g_licensePlan + " - " + g_licenseExpiry;
                ImVec2 licSize = ImGui::CalcTextSize(licenseText.c_str());
                dl->AddText(ImVec2(cardMin.x + (cardW - licSize.x) * 0.5f, contentY + 150), COL_TEXT_DIM, licenseText.c_str());
                ImGui::PopFont();
            }
        }
        else if (g_animStage == STAGE_SUCCESS) {
            float scale = easeOutBack(ImClamp(g_stageTime / 0.3f, 0.0f, 1.0f));
            
            ImGui::PushFont(g_fontLogo);
            const char* checkmark = "Active";
            ImVec2 checkSize = ImGui::CalcTextSize(checkmark);
            dl->AddText(ImVec2(cardMin.x + (cardW - checkSize.x * scale) * 0.5f, contentY + 60), COL_SUCCESS, checkmark);
            ImGui::PopFont();
            
            if (g_stageTime > 1.5f) { g_animStage = STAGE_EXIT; g_stageTime = 0; }
        }

        // Close button
        ImVec2 closePos(winSize.x - 35, 10);
        bool closeHovered = ImGui::IsMouseHoveringRect(closePos, ImVec2(closePos.x + 25, closePos.y + 25));
        dl->AddText(closePos, closeHovered ? COL_TEXT : COL_TEXT_DIM, "X");
        if (closeHovered && ImGui::IsMouseClicked(0)) g_shouldClose = true;

        // Version
        ImGui::PushFont(g_fontRegular);
        dl->AddText(ImVec2(15, winSize.y - 25), COL_TEXT_DIM, "v0.20.1");
        ImGui::PopFont();

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        glViewport(0, 0, 420, 520);
        glClearColor(0.043f, 0.043f, 0.051f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_hdc);
        return true;
    }

    void SetState(State state) {
        g_currentState = state;
        if (state == State::LICENSE_VALID) {
            g_animStage = STAGE_SUCCESS;
            g_stageTime = 0;
        }
    }
    
    void SetMessage(const std::string& msg) { g_message = msg; }
    void SetError(const std::string& error) { g_errorMessage = error; g_currentState = State::ERROR_STATE; g_animStage = STAGE_LOGIN; }
    void SetLicenseInfo(const std::string& plan, const std::string& expiry) { g_licensePlan = plan; g_licenseExpiry = expiry; }
    std::string GetUsername() { return std::string(g_username); }
    std::string GetPassword() { return std::string(g_password); }
    bool IsLoginButtonPressed() { return g_loginButtonPressed; }
    void ClearLoginButton() { g_loginButtonPressed = false; }
}