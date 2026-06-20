#include <aclapi.h>   // SetEntriesInAclW
#include <objbase.h>  // CoCreateGuid
#include <Windows.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>

#pragma comment(lib, "ole32.lib")     // CoCreateGuid
#pragma comment(lib, "advapi32.lib")  // SetEntriesInAclW

#include "hwid.h"
#include "inject.h"
#include "license.h"
#include "process.h"
#include "resource.h"
#include "security.h"
#include "src/core/payload_crypto.hpp"
#include "src/core/secure_mapper.hpp"
#include "src/ui/loader_window.hpp"
#include "src/update/download.h"
#include "src/update/updater.h"
#include "src/util/dev_console.hpp"
#include "src/util/dll_hash.h"
#include "src/util/hash.h"
#include "xor.h"

// Generated config (per-build obfuscated, sourced from cobble.json)
#include "generated/server_config.h"
#include "generated/keys.h"
#include "generated/version.h"

// Protection system
#include "src/license/telemetry.hpp"
#include "src/security/integrity.hpp"
#include "src/security/integrity_gen.hpp"
#include "src/security/themida.h"
#include "src/util/aes_string.hpp"
#include "src/util/control_flow.hpp"

// Shared memory structure for DLL validation
// This is passed from loader to DLL via named shared memory
struct InjectionData {
    char signature[16];      // "COBBLE_INJ_V1" - magic signature
    char secret[65];         // 64 hex chars + null (injection_secret from server)
    char username[64];       // Username from license
    char hwid[65];           // HWID for server validation
    uint32_t timestamp;      // Unix timestamp when created
    uint32_t expiresIn;      // Seconds until expiry (5 minutes)
    char handshakeSig[128];  // Base64 Ed25519 signature from server
    char sessionToken[128];  // Session token for heartbeat
    char syncToken[65];      // Sync token for runtime validation
    // Note: DLL derives the shared memory name from GetCurrentProcessId()
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    DevConsole::Initialize();
    COBBLE_DEV_LOG("Loader startup, version=%s, server=%s", COBBLE_VERSION, cobble_config::GetServerHost().c_str());

    // === EARLY TELEMETRY: Send webhook BEFORE Themida can kill the process ===
    {
        Telemetry::Init(cobble_config::GetServerHost());
        auto toolDetections = Security::CheckRunningTools();
        for (auto& d : toolDetections) {
            Telemetry::Severity sev = (d.score >= 40) ? Telemetry::Severity::WARN : Telemetry::Severity::INFO;
            Telemetry::ReportImmediate(d.code, sev, d.score, d.details);
        }
    }

    // === THEMIDA PROTECTION CHECKS ===
    // These macros are inert in dev builds (CHECK_PROTECTION returns 0
    // because the binary hasn't been Themida-packed). In Pack builds the
    // checks become real and the inner block runs the integrity/debugger
    // verification.
    {
        int protectionCheck = 0;
        CHECK_PROTECTION(protectionCheck, 1);

        if (protectionCheck == 1) {
            // Anti-tamper check
            int integrityCheck = 0;
            CHECK_CODE_INTEGRITY(integrityCheck, 1);
            if (integrityCheck != 1) {
                Telemetry::ReportImmediate("THEMIDA_TAMPER", Telemetry::Severity::CRITICAL, 100,
                                           "CHECK_CODE_INTEGRITY");
                return 1;
            }

            // Debugger detection
            int debuggerCheck = 0;
            CHECK_DEBUGGER(debuggerCheck, 0);
            if (debuggerCheck != 0) {
                Telemetry::ReportImmediate("THEMIDA_DEBUGGER", Telemetry::Severity::CRITICAL, 100, "CHECK_DEBUGGER");
                return 1;
            }
        }
    }

    // Initialize security patches (anti-attach, etc.)
    Security::Initialize();

    // === SCORE-BASED DETECTION SYSTEM ===
    {
        std::vector<Security::Detection> detections;
        Security::SecurityDecision decision = Security::RunSecurityChecksWithDecision(detections);

        if (decision == Security::SecurityDecision::BLOCK) {
            return 1;
        }

        if (decision == Security::SecurityDecision::WARN) {
            int userChoice = MessageBoxA(
                nullptr,
                OBFUSCATE_STRING(
                    "Analysis tools detected.\n\nPlease close any debugging or analysis software and try again.")
                    .c_str(),
                OBFUSCATE_STRING("Cobble Loader").c_str(), MB_RETRYCANCEL | MB_ICONWARNING);

            if (userChoice == IDRETRY) {
                detections.clear();
                decision = Security::RunSecurityChecksWithDecision(detections);
                if (decision != Security::SecurityDecision::ALLOW) {
                    return 1;
                }
            } else {
                return 0;
            }
        }
    }

    // UI Init
    if (!LoaderUI::Init()) {
        MessageBoxA(nullptr, OBFUSCATE_STRING("Failed to initialize UI").c_str(),
                    OBFUSCATE_STRING("Cobble Loader").c_str(), MB_ICONERROR);
        return 1;
    }

    LoaderUI::SetState(LoaderUI::State::INITIALIZING);
    LoaderUI::SetMessage(OBFUSCATE_STRING("Initializing..."));

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

    // ==========================================================================
    // AUTO-UPDATER: Check for updates before proceeding
    // ==========================================================================
    LoaderUI::SetMessage(OBFUSCATE_STRING("Checking for updates..."));
    renderFrame();

    COBBLE_DEV_LOG("Checking for loader updates");
    auto updateResult = Updater::Check(COBBLE_VERSION, cobble_config::GetServerHost(), cobble_config::SERVER_PORT);
    COBBLE_DEV_LOG("Update check complete: success=%s, available=%s, mandatory=%s",
                   updateResult.success ? "true" : "false",
                   updateResult.updateAvailable ? "true" : "false",
                   updateResult.mandatory ? "true" : "false");

    if (updateResult.success && updateResult.mandatory) {
        // Mandatory update required - must download and restart
        LoaderUI::SetMessage(OBFUSCATE_STRING("Downloading update..."));
        renderFrame();

        // Get HWID for authenticated download
        std::string hwid = HWID::Generate();

        // Create temp directory for download
        std::string tempDir = Updater::getTempDownloadDir();
        std::string newLoaderPath = tempDir + "\\CobbleLoader.exe";

        // Download new loader
        std::string downloadUrl = "https://" + cobble_config::GetServerHost() + OBFUSCATE_STRING("/api/update/loader");

        bool downloadSuccess =
            Updater::downloadAndVerify(downloadUrl, newLoaderPath, updateResult.manifest.loader.sha256,
                                       updateResult.manifest.loader.size, hwid, nullptr);

        if (downloadSuccess) {
            // Spawn the new loader to replace us
            std::string selfPath = Updater::getSelfPath();
            if (Updater::spawnStub(selfPath, newLoaderPath)) {
                // Exit so the new loader can replace us
                LoaderUI::Shutdown();
                return 0;
            }
        }

        // Download failed - cleanup and show error
        Updater::cleanupTempDir(tempDir);
        LoaderUI::SetError(OBFUSCATE_STRING("Update failed - please try again"));
        while (renderFrame())
            ;
        LoaderUI::Shutdown();
        return 1;
    }

    // Optional update available - show notification but don't block
    if (updateResult.success && updateResult.updateAvailable && !updateResult.mandatory) {
        std::string msg = "Update v" + updateResult.manifest.version + " available";
        LoaderUI::SetMessage(msg);
        renderFrame();
        Sleep(1500);  // Brief pause to show the message
    }

    // Load encrypted payload from resources
    HMODULE hModule = GetModuleHandle(nullptr);
    HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDR_COBBLE_DLL), RT_RCDATA);
    if (!hRes) {
        LoaderUI::SetError(OBFUSCATE_STRING("Launch failed"));
        while (renderFrame())
            ;
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

    bool licenseValid = false;

    COBBLE_DEV_LOG("Starting startup license verification");
    const bool startupLicenseValid = License::Verify(cobble_config::GetServerUrl().c_str());
    COBBLE_DEV_LOG("Startup verification complete: valid=%s, requires_login=%s, error=\"%s\"",
                   startupLicenseValid ? "true" : "false",
                   License::RequiresLogin() ? "true" : "false",
                   License::GetLastError().c_str());

    if (startupLicenseValid) {
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
                    LoaderUI::SetError(OBFUSCATE_STRING("Enter username and password"));
                    continue;
                }

                LoaderUI::SetState(LoaderUI::State::AUTHENTICATING);
                renderFrame();

                COBBLE_DEV_LOG("Submitting login request (username_length=%zu)", username.size());
                const bool loginValid =
                    License::LoginWithCredentials(cobble_config::GetServerUrl().c_str(), username, password);
                COBBLE_DEV_LOG("Login request complete: valid=%s, error=\"%s\"",
                               loginValid ? "true" : "false", License::GetLastError().c_str());

                if (loginValid) {
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
        COBBLE_DEV_LOG("Displaying terminal license error: \"%s\"", License::GetLastError().c_str());
        LoaderUI::SetError(License::GetLastError());
        while (renderFrame())
            ;
        LoaderUI::Shutdown();
        return 1;
    }

    // Wait for Minecraft if not already detected during login
    if (lunar.pid == 0) {
        LoaderUI::SetMessage(OBFUSCATE_STRING("Waiting for Minecraft..."));

        auto nextScan = std::chrono::steady_clock::now();
        while (true) {
            if (!renderFrame()) {
                LoaderUI::Shutdown();
                return 0;
            }

            // Check for security violations while waiting
            auto now = std::chrono::steady_clock::now();
            if (now >= nextScan) {
                lunar = findLunarClient();
                if (lunar.pid != 0)
                    break;
                nextScan = now + std::chrono::milliseconds(100);
            }
        }
    }

    // Check if already injected before fetching key (saves server resources)
    if (isDllInjected(lunar.pid, L"Cobble.dll")) {
        LoaderUI::SetError(OBFUSCATE_STRING("Already injected"));
        while (renderFrame())
            ;
        LoaderUI::Shutdown();
        return 0;
    }

    // =========================================================================
    // CRITICAL SECTION: Fetch key, decrypt, and inject as fast as possible
    // Minimize time that decrypted DLL exists in memory
    // =========================================================================
    LoaderUI::SetMessage(OBFUSCATE_STRING("Initializing..."));
    renderFrame();

    // Fetch payload key from server - REQUIRED
    std::string payloadKey;
    if (!License::FetchPayloadKey(cobble_config::GetServerUrl().c_str())) {
        LoaderUI::SetError(OBFUSCATE_STRING("Server connection failed"));
        while (renderFrame())
            ;
        LoaderUI::Shutdown();
        return 1;
    }
    payloadKey = License::GetPayloadKey();

    if (payloadKey.empty() || payloadKey.length() != 64) {
        LoaderUI::SetError(OBFUSCATE_STRING("Key fetch failed"));
        while (renderFrame())
            ;
        LoaderUI::Shutdown();
        return 1;
    }

    // One final security check right before decryption
    CF_CRITICAL_BEGIN();
    if (CF_CHECK(false)) {
        CF_JUNK();
        Telemetry::Report("DBG_FINAL_CHECK", Telemetry::Severity::CRITICAL, 100,
                          OBFUSCATE_STRING("Pre-decrypt security check failed"));
        Sleep(500);
        SecureZeroMemory(&payloadKey[0], payloadKey.size());
        LoaderUI::Shutdown();
        return 1;
    }
    CF_CRITICAL_END();

    // Decrypt payload with server key - no fallback
    decryptedDll = PayloadCrypto::DecryptPayloadWithKey(encryptedData, encryptedSize, payloadKey);

    // Zero key from memory immediately
    SecureZeroMemory(&payloadKey[0], payloadKey.size());

    if (decryptedDll.empty() || !PayloadCrypto::ValidatePE(decryptedDll)) {
        LoaderUI::SetError(OBFUSCATE_STRING("Launch failed"));
        while (renderFrame())
            ;
        LoaderUI::Shutdown();
        return 1;
    }

    dllData = decryptedDll.data();
    dllSize = decryptedDll.size();

    // Immediately proceed to injection - DO NOT add any delays here
    LoaderUI::SetMessage(OBFUSCATE_STRING("Injecting..."));
    renderFrame();

    // Create shared memory with injection data BEFORE injection
    // The DLL will read this to validate it was loaded by the legitimate loader
    HANDLE hMapFile = nullptr;
    std::wstring sharedMemNameW;
    {
        // THEMIDA: Protect InjectionData preparation - contains secrets
        // Note: VM protection covers all strings within this block
        COBBLE_VM_HIGH_START

        InjectionData injData = {};
        memcpy(injData.signature, "COBBLE_INJ_V1\0\0\0", 16);

        std::string secret = License::GetInjectionSecret();
        std::string username = License::GetUsername();
        std::string hwid = HWID::Generate();

        strncpy_s(injData.secret, secret.c_str(), 64);
        strncpy_s(injData.username, username.c_str(), 63);
        strncpy_s(injData.hwid, hwid.c_str(), 64);
        // Use server's timestamp (issued_at) - MUST match what was signed
        injData.timestamp = (uint32_t)License::GetHandshakeTimestamp();
        injData.expiresIn = 300;  // 5 minutes to init

        // Copy handshake signature from server (for DLL to verify)
        std::string handshakeSig = License::GetHandshakeSig();
        strncpy_s(injData.handshakeSig, handshakeSig.c_str(), 127);

        // Copy session token for DLL heartbeat (Phase 4)
        std::string sessionToken = License::GetSessionToken();
        strncpy_s(injData.sessionToken, sessionToken.c_str(), 127);

        // Copy sync token for runtime validation
        std::string syncToken = License::GetSyncToken();
        strncpy_s(injData.syncToken, syncToken.c_str(), 64);

        // Generate PID-based shared memory name
        wchar_t pidStr[64];
        swprintf_s(pidStr, L"Local\\Cobble_%lu", lunar.pid);
        sharedMemNameW = pidStr;

        COBBLE_VM_HIGH_END

        // Create security attributes with restricted DACL
        // Only current user can access (not NULL DACL which allows everyone)
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = FALSE;

        // Create a DACL that grants full access only to the current user
        PSECURITY_DESCRIPTOR pSD = nullptr;
        PSID pCurrentUserSid = nullptr;

        // Get current user's SID
        HANDLE hToken = nullptr;
        PTOKEN_USER pUserToken = nullptr;
        PACL pAcl = nullptr;

        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            DWORD dwSize = 0;
            GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize);
            if (dwSize > 0) {
                pUserToken = (PTOKEN_USER)LocalAlloc(LPTR, dwSize);
                if (pUserToken && GetTokenInformation(hToken, TokenUser, pUserToken, dwSize, &dwSize)) {
                    pCurrentUserSid = pUserToken->User.Sid;

                    // Create security descriptor with explicit ACE for current user only
                    EXPLICIT_ACCESS ea = {};
                    ea.grfAccessPermissions = FILE_MAP_ALL_ACCESS;
                    ea.grfAccessMode = SET_ACCESS;
                    ea.grfInheritance = NO_INHERITANCE;
                    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
                    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
                    ea.Trustee.ptstrName = (LPWSTR)pCurrentUserSid;

                    if (SetEntriesInAclW(1, &ea, nullptr, &pAcl) == ERROR_SUCCESS) {
                        pSD = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
                        if (pSD) {
                            InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
                            SetSecurityDescriptorDacl(pSD, TRUE, pAcl, FALSE);
                            sa.lpSecurityDescriptor = pSD;
                        }
                    }
                }
            }
            CloseHandle(hToken);
        }

        // Fallback: if we couldn't create restricted DACL, use default (not NULL)
        if (!sa.lpSecurityDescriptor) {
            sa.lpSecurityDescriptor = nullptr;
        }

        // Create named shared memory with PID-based name
        hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(InjectionData),
                                      sharedMemNameW.c_str());

        if (hMapFile) {
            void* pBuf = MapViewOfFile(hMapFile, FILE_MAP_WRITE, 0, 0, sizeof(InjectionData));
            if (pBuf) {
                memcpy(pBuf, &injData, sizeof(InjectionData));
                UnmapViewOfFile(pBuf);
            }
        }

        // Clean up all allocated resources
        if (pAcl)
            LocalFree(pAcl);
        if (pUserToken)
            LocalFree(pUserToken);
        if (pSD)
            LocalFree(pSD);

        // Zero secret from local memory
        SecureZeroMemory(&injData, sizeof(injData));
    }

    // Use Secure Manual Mapping with shared memory sections
    // No WriteProcessMemory, no disk write, uses NT syscalls
    // NO FALLBACK - insecure methods are worse than failing
    if (!SecureMapper::SecureManualMap(lunar.pid, dllData, dllSize)) {
        if (hMapFile)
            CloseHandle(hMapFile);
        LoaderUI::SetError(OBFUSCATE_STRING("Launch failed"));
        while (renderFrame())
            ;
        LoaderUI::Shutdown();
        return 1;
    }

    // Clear decrypted DLL from memory immediately after injection
    SecureZeroMemory(decryptedDll.data(), decryptedDll.size());

    LoaderUI::SetState(LoaderUI::State::INJECTING_SUCCESS);

    // Let the UI complete the full inject + success animation; the UI will close itself.
    while (renderFrame())
        ;

    // Clean up shared memory handle (data persists until DLL reads it)
    if (hMapFile)
        CloseHandle(hMapFile);

    LoaderUI::Shutdown();
    return 0;
}
