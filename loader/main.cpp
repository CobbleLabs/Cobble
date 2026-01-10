#include <Windows.h>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <ctime>

#include "process.h"
#include "inject.h"
#include "src/core/secure_mapper.hpp"
#include "src/core/payload_crypto.hpp"
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

// Shared memory structure for DLL validation
// This is passed from loader to DLL via named shared memory
struct InjectionData {
    char signature[16];       // "COBBLE_INJ_V1" - magic signature
    char secret[65];          // 64 hex chars + null (injection_secret from server)
    char username[64];        // Username from license
    char hwid[65];            // HWID for server validation
    uint32_t timestamp;       // Unix timestamp when created
    uint32_t expiresIn;       // Seconds until expiry (5 minutes)
    char handshakeSig[128];   // Base64 Ed25519 signature from server
};

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
    
    // Load encrypted payload from resources
    HMODULE hModule = GetModuleHandle(nullptr);
    HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDR_COBBLE_DLL), RT_RCDATA);
    if (!hRes) {
        LoaderUI::SetError("Launch failed");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
    
    HGLOBAL hData = LoadResource(hModule, hRes);
    BYTE* encryptedData = (BYTE*)LockResource(hData);
    DWORD encryptedSize = SizeofResource(hModule, hRes);
    
    // License check FIRST (before decryption to get server key)
    std::vector<uint8_t> decryptedDll;
    BYTE* dllData = nullptr;
    size_t dllSize = 0;
    
    // Pre-declare lunar so we can detect MC during login
    ProcessInfo lunar = {};
    
#if LICENSE_CHECK_ENABLED
    bool licenseValid = false;
    
    if (License::Verify(getLicenseServerPrimary().c_str())) {
        licenseValid = true;
        LoaderUI::SetState(LoaderUI::State::LICENSE_VALID);
    } else if (License::RequiresLogin()) {
        // First time login flow...
        LoaderUI::SetState(LoaderUI::State::LOGIN_REQUIRED);
        
        // Poll for Minecraft in background while user types credentials
        auto nextMcScan = std::chrono::steady_clock::now();
        
        while (renderFrame()) {
            // MC detection in background
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
                    LoaderUI::SetError("Enter username and password");
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
    
    // Try to fetch payload key from server
    LoaderUI::SetMessage("Initializing...");
    renderFrame();
    
    std::string payloadKey;
    if (License::FetchPayloadKey(getLicenseServerPrimary().c_str())) {
        payloadKey = License::GetPayloadKey();
    }
    
    // Decrypt payload
    if (!payloadKey.empty()) {
        // Use server-provided key (secure path)
        decryptedDll = PayloadCrypto::DecryptPayloadWithKey(encryptedData, encryptedSize, payloadKey);
    } else {
        // Fallback to compiled key (legacy/offline compatibility)
        decryptedDll = PayloadCrypto::DecryptPayload(encryptedData, encryptedSize);
    }
    
    // Zero key from memory
    SecureZeroMemory(&payloadKey[0], payloadKey.size());
    
#else
    // No license check, use compiled key
    decryptedDll = PayloadCrypto::DecryptPayload(encryptedData, encryptedSize);
#endif
    
    if (decryptedDll.empty() || !PayloadCrypto::ValidatePE(decryptedDll)) {
        LoaderUI::SetError("Launch failed");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
    
    dllData = decryptedDll.data();
    dllSize = decryptedDll.size();
    
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

    // Final security check before injection
    if (Security::HasAPIHooks() || AntiDebug::isDebugged()) {
        LoaderUI::SetError("Launch failed");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
    
    LoaderUI::SetMessage("Injecting...");
    renderFrame();
    
    // Create shared memory with injection data BEFORE injection
    // The DLL will read this to validate it was loaded by the legitimate loader
    HANDLE hMapFile = nullptr;
    {
        InjectionData injData = {};
        memcpy(injData.signature, "COBBLE_INJ_V1\0\0\0", 16);
        
        std::string secret = License::GetInjectionSecret();
        std::string username = License::GetUsername();
        std::string hwid = HWID::Generate();
        
        strncpy_s(injData.secret, secret.c_str(), 64);
        strncpy_s(injData.username, username.c_str(), 63);
        strncpy_s(injData.hwid, hwid.c_str(), 64);
        injData.timestamp = (uint32_t)time(nullptr);
        injData.expiresIn = 300; // 5 minutes to init
        
        // Copy handshake signature from server (for DLL to verify)
        std::string handshakeSig = License::GetHandshakeSig();
        strncpy_s(injData.handshakeSig, handshakeSig.c_str(), 127);
        
        // Create security attributes that allow cross-process access
        // Without this, Minecraft can't read the shared memory
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = FALSE;
        
        SECURITY_DESCRIPTOR sd = {};
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE); // NULL DACL = all access
        sa.lpSecurityDescriptor = &sd;
        
        // Create named shared memory (visible to target process)
        hMapFile = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            &sa,  // Use our permissive security attributes
            PAGE_READWRITE,
            0,
            sizeof(InjectionData),
            L"Local\\CobbleInjection"
        );
        
        if (hMapFile) {
            void* pBuf = MapViewOfFile(hMapFile, FILE_MAP_WRITE, 0, 0, sizeof(InjectionData));
            if (pBuf) {
                memcpy(pBuf, &injData, sizeof(InjectionData));
                UnmapViewOfFile(pBuf);
            }
        }
        
        // Zero secret from local memory
        SecureZeroMemory(&injData, sizeof(injData));
    }
    
    // Use Secure Manual Mapping with shared memory sections
    // No WriteProcessMemory, no disk write, uses NT syscalls
    // NO FALLBACK - insecure methods are worse than failing
    if (!SecureMapper::SecureManualMap(lunar.pid, dllData, dllSize)) {
        if (hMapFile) CloseHandle(hMapFile);
        LoaderUI::SetError("Launch failed");
        while (renderFrame());
        LoaderUI::Shutdown();
        return 1;
    }
    
    // Clear decrypted DLL from memory immediately after injection
    SecureZeroMemory(decryptedDll.data(), decryptedDll.size());

    LoaderUI::SetState(LoaderUI::State::INJECTING_SUCCESS);

    // Let the UI complete the full inject + success animation; the UI will close itself.
    while (renderFrame());

    // Clean up shared memory handle (data persists until DLL reads it)
    if (hMapFile) CloseHandle(hMapFile);

    LoaderUI::Shutdown();
    return 0;
}
