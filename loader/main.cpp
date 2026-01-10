#include <Windows.h>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>

#include "process.h"
#include "inject.h"
#include "resource.h"
#include "security.h"
#include "xor.h"
#include "license.h"
#include "hwid.h"
#include "version.h"
#include "src/util/hash.h"
#include "src/util/dll_hash.h"
#include "src/update/updater.h"
#include "src/update/download.h"
#include "src/ui/loader_window.hpp"

// Protection system
#include "src/util/obfuscate.hpp"
#include "src/security/antidbg.hpp"
#include "src/security/integrity.hpp"
#include "src/security/integrity_gen.hpp"
#include "src/security/advanced_security.hpp"

// Server configuration (obfuscated)
inline std::string getServerPrimary() { return OBFS("api.cobble.vip"); }
inline std::string getLicenseServerPrimary() { return OBFS("https://api.cobble.vip"); }

#define SERVER_PORT_HTTPS 443
#define LICENSE_CHECK_ENABLED true

std::atomic<bool> g_SecurityViolation(false);

DWORD WINAPI SecurityThread1(LPVOID lpParam) {
    while (!g_SecurityViolation) {
        if (Security::IsBeingDebugged()) { g_SecurityViolation = true; break; }
        Sleep(500);
    }
    return 0;
}

DWORD WINAPI SecurityThread2(LPVOID lpParam) {
    while (!g_SecurityViolation) {
        if (Security::HasAPIHooks()) { g_SecurityViolation = true; break; }
        Sleep(500);
    }
    return 0;
}

DWORD WINAPI SecurityThread3(LPVOID lpParam) {
    while (!g_SecurityViolation) {
        if (AdvancedSecurity::hasKernelDebugger()) { g_SecurityViolation = true; break; }
        if (AdvancedSecurity::hasSyscallHooks()) { g_SecurityViolation = true; break; }
        Sleep(1000);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Security checks
    if (Security::IsBeingDebugged()) return 1;
    Security::Initialize();
    
    AntiDebug::calibrate();
    if (AntiDebug::isDebugged()) return 1;
    
    // Advanced security checks (VM, kernel debugger, syscall hooks)
    if (AdvancedSecurity::isCompromised()) return 1;
    
    HANDLE hThread1 = CreateThread(NULL, 0, SecurityThread1, NULL, 0, NULL);
    HANDLE hThread2 = CreateThread(NULL, 0, SecurityThread2, NULL, 0, NULL);
    HANDLE hThread3 = CreateThread(NULL, 0, SecurityThread3, NULL, 0, NULL);
    if (hThread1) CloseHandle(hThread1);
    if (hThread2) CloseHandle(hThread2);
    if (hThread3) CloseHandle(hThread3);
    
    // UI Init
    if (!LoaderUI::Init()) {
        MessageBoxA(nullptr, "Failed to initialize UI", "Cobble Loader", MB_ICONERROR);
        return 1;
    }

    LoaderUI::SetState(LoaderUI::State::INITIALIZING);
    LoaderUI::SetMessage("Initializing...");

    auto renderFrame = []() -> bool {
        bool keepRunning = LoaderUI::Render();
        std::this_thread::yield();
        return keepRunning;
    };

    // Pre-render frames offscreen to compile fonts/shaders before showing window
    for (int i = 0; i < 10; i++) {
        if (!renderFrame()) {
            LoaderUI::Shutdown();
            return 0;
        }
    }
    
    // Now show the window with everything ready
    LoaderUI::Show();
    
    // Integrity Check
    HMODULE hModule = GetModuleHandle(nullptr);
    HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDR_COBBLE_DLL), RT_RCDATA);
    if (!hRes) {
        LoaderUI::SetError("Corruption detected");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
    
    HGLOBAL hData = LoadResource(hModule, hRes);
    BYTE* dllData = (BYTE*)LockResource(hData);
    DWORD dllSize = SizeofResource(hModule, hRes);
    
#if DLL_HASH_ENABLED
    if (dllSize != DLL_EXPECTED_SIZE ||
        !Hash::VerifyChunked(dllData, dllSize, DLL_HASH_CHUNK1, DLL_HASH_CHUNK2, DLL_HASH_CHUNK3, DLL_HASH_CHUNK4)) {
        LoaderUI::SetError("Integrity check failed");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
#endif
    
    // License
#if LICENSE_CHECK_ENABLED
    bool licenseValid = false;
    
    if (License::Verify(getLicenseServerPrimary().c_str())) {
        licenseValid = true;
        LoaderUI::SetState(LoaderUI::State::LICENSE_VALID);
    }
    
    // Pre-declare lunar so we can detect MC during login
    ProcessInfo lunar = {};

    if (!licenseValid && License::RequiresLogin()) {
        LoaderUI::SetState(LoaderUI::State::LOGIN_REQUIRED);

        // Poll for Minecraft in background while user types credentials
        auto nextMcScan = std::chrono::steady_clock::now();

        while (renderFrame()) {
            // Check for Minecraft every 200ms while on login screen
            auto now = std::chrono::steady_clock::now();
            if (now >= nextMcScan) {
                lunar = findLunarClient();
                nextMcScan = now + std::chrono::milliseconds(200);
            }

            if (LoaderUI::IsLoginButtonPressed()) {
                std::string username = LoaderUI::GetUsername();
                std::string password = LoaderUI::GetPassword();
                LoaderUI::ClearLoginButton();

                if (username.empty() || password.empty()) {
                    LoaderUI::SetError("Username and password required");
                    continue;
                }

                LoaderUI::SetState(LoaderUI::State::AUTHENTICATING);
                renderFrame();

                if (License::LoginWithCredentials(getLicenseServerPrimary().c_str(), username, password)) {
                    licenseValid = true;
                    // If MC already detected, skip waiting stage entirely
                    if (lunar.pid == 0) {
                        LoaderUI::SetState(LoaderUI::State::LICENSE_VALID);
                    }
                    break;
                } else {
                    LoaderUI::SetError(License::GetLastError());
                }
            }
        }

        if (!licenseValid) {
            LoaderUI::Shutdown();
            return 0;
        }
    }
    
    if (!licenseValid) {
        LoaderUI::SetError(License::GetLastError());
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
#endif
    
    if (g_SecurityViolation) {
        LoaderUI::Shutdown();
        return 1;
    }
    
    // Only wait for Minecraft if not already detected during login
    if (lunar.pid == 0) {
        LoaderUI::SetMessage("Waiting for Minecraft...");

        auto nextScan = std::chrono::steady_clock::now();
        while (true) {
            if (!renderFrame()) {
                LoaderUI::Shutdown();
                return 0;
            }

            auto now = std::chrono::steady_clock::now();
            if (now >= nextScan) {
                lunar = findLunarClient();
                if (lunar.pid != 0) break;
                nextScan = now + std::chrono::milliseconds(100);
            }
        }
    }
    
    if (isDllInjected(lunar.pid, L"Cobble.dll")) {
        LoaderUI::SetError("Already injected");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 0;
    }

    if (g_SecurityViolation) {
        LoaderUI::Shutdown();
        return 1;
    }

    LoaderUI::SetMessage("Injecting...");
    renderFrame();
    
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    DWORD tick = GetTickCount();
    wchar_t uniqueName[64];
    swprintf_s(uniqueName, L"u%08X.tmp", tick);
    std::wstring dllTempPath = std::wstring(tempPath) + uniqueName;
    
    DeleteFileW(dllTempPath.c_str());
    HANDLE hFile = CreateFileW(dllTempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LoaderUI::SetError("Write error");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
    
    DWORD written;
    WriteFile(hFile, dllData, dllSize, &written, nullptr);
    CloseHandle(hFile);
    
    if (!injectDllLoadLibrary(lunar.pid, dllTempPath)) {
        LoaderUI::SetError("Injection failed\nTry running as Administrator");
        DeleteFileW(dllTempPath.c_str());
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
    
    Sleep(500);
    DeleteFileW(dllTempPath.c_str());

    LoaderUI::SetState(LoaderUI::State::INJECTING_SUCCESS);

    // Let the UI complete the full inject + success animation; the UI will close itself.
    while (renderFrame());

    LoaderUI::Shutdown();
    return 0;
}
