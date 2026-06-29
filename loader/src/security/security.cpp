#include "security.h"
#include <DbgHelp.h>
#include <intrin.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include "../license/telemetry.hpp"
#include "../util/aes_string.hpp"
#include "vmprotect.h"

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "dbghelp.lib")

// Disable optimizations to prevent compiler from removing security checks
#pragma optimize("", off)

// ============================================================================
// NT API Declarations
// ============================================================================

extern "C" NTSTATUS NTAPI NtSetInformationThread(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass,
                                                 PVOID ThreadInformation, ULONG ThreadInformationLength);

const THREADINFOCLASS ThreadHideFromDebugger = (THREADINFOCLASS)0x11;

namespace Security {

#pragma region AntiAttach

void PatchDbgBreakPoint() {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll)
        return;

    FARPROC pDbgBreakPoint = GetProcAddress(hNtdll, "DbgBreakPoint");
    if (!pDbgBreakPoint)
        return;

    DWORD dwOldProtect;
    if (!VirtualProtect(pDbgBreakPoint, 1, PAGE_EXECUTE_READWRITE, &dwOldProtect))
        return;

    *(PBYTE)pDbgBreakPoint = 0xC3;  // RET

    VirtualProtect(pDbgBreakPoint, 1, dwOldProtect, &dwOldProtect);
}

void PatchDbgUiRemoteBreakin() {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll)
        return;

    FARPROC pDbgUiRemoteBreakin = GetProcAddress(hNtdll, "DbgUiRemoteBreakin");
    if (!pDbgUiRemoteBreakin)
        return;

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32)
        return;

    FARPROC pTerminateProcess = GetProcAddress(hKernel32, "TerminateProcess");
    if (!pTerminateProcess)
        return;

    DbgUiRemoteBreakinPatch patch = {0};
    patch.push_0 = 0x006A;                    // push 0
    patch.push = 0x68;                        // push
    patch.CurrentPorcessHandle = 0xFFFFFFFF;  // -1 (current process)
    patch.mov_eax = 0xB8;                     // mov eax
    patch.TerminateProcess = (DWORD)(DWORD_PTR)pTerminateProcess;
    patch.call_eax = 0xD0FF;  // call eax

    DWORD dwOldProtect;
    if (!VirtualProtect(pDbgUiRemoteBreakin, sizeof(patch), PAGE_EXECUTE_READWRITE, &dwOldProtect))
        return;

    memcpy(pDbgUiRemoteBreakin, &patch, sizeof(patch));

    VirtualProtect(pDbgUiRemoteBreakin, sizeof(patch), dwOldProtect, &dwOldProtect);
}

void HideThread(HANDLE hThread) {
    NtSetInformationThread(hThread, ThreadHideFromDebugger, NULL, 0);
}

bool ShouldApplyAntiAttach() {
    // VMProtect CHECK_PROTECTION returns 1 only when the binary has been
    // packed; in dev builds (build.ps1 without -Pack) it returns 0 and we
    // skip anti-attach entirely.
    int protectedResult = 0;
    CHECK_PROTECTION(protectedResult, 1);
    if (protectedResult != 1) {
        return false;
    }

    int debugResult = 0;
    CHECK_DEBUGGER(debugResult, 0);
    return debugResult != 0;
}

#pragma endregion

#pragma region AntiVM

bool HasVMRegistry() {
    std::vector<std::wstring> vmKeys;
    vmKeys.reserve(6);

    // VMPROTECT: Mutate VM registry key checks
    COBBLE_STR_ENCRYPTW_BEGIN
    vmKeys.push_back(OBFUSCATE_WSTRING(L"System\\ControlSet001\\Services\\VMTools"));
    vmKeys.push_back(OBFUSCATE_WSTRING(L"System\\ControlSet001\\Services\\VBoxGuest"));
    vmKeys.push_back(OBFUSCATE_WSTRING(L"System\\ControlSet001\\Services\\VBoxMouse"));
    vmKeys.push_back(OBFUSCATE_WSTRING(L"System\\ControlSet001\\Services\\VBoxService"));
    vmKeys.push_back(OBFUSCATE_WSTRING(L"SOFTWARE\\Oracle\\VirtualBox Guest Additions"));
    vmKeys.push_back(OBFUSCATE_WSTRING(L"SOFTWARE\\VMware, Inc.\\VMware Tools"));
    COBBLE_STR_ENCRYPTW_END

    for (const auto& keyPath : vmKeys) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
    }

    return false;
}

bool HasVMFiles() {
    std::vector<std::wstring> vmFiles;
    vmFiles.reserve(6);

    // VMPROTECT: Mutate VM file path checks
    COBBLE_STR_ENCRYPTW_BEGIN
    vmFiles.push_back(OBFUSCATE_WSTRING(L"C:\\Windows\\System32\\vm3dgl.dll"));
    vmFiles.push_back(OBFUSCATE_WSTRING(L"C:\\Windows\\System32\\vboxdisp.dll"));
    vmFiles.push_back(OBFUSCATE_WSTRING(L"C:\\Windows\\System32\\vboxhook.dll"));
    vmFiles.push_back(OBFUSCATE_WSTRING(L"C:\\Windows\\System32\\vboxogl.dll"));
    vmFiles.push_back(OBFUSCATE_WSTRING(L"C:\\Windows\\System32\\drivers\\vboxmouse.sys"));
    vmFiles.push_back(OBFUSCATE_WSTRING(L"C:\\Windows\\System32\\drivers\\vmhgfs.sys"));
    COBBLE_STR_ENCRYPTW_END

    for (const auto& filePath : vmFiles) {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(filePath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            FindClose(hFind);
            return true;
        }
    }

    return false;
}

bool HasVMWareACPI() {
    DWORD acpiDataSize = GetSystemFirmwareTable('ACPI', 'TEPH', NULL, 0);
    if (acpiDataSize == 0)
        return false;

    BYTE* acpiData = (BYTE*)HeapAlloc(GetProcessHeap(), 0, acpiDataSize);
    if (!acpiData)
        return false;

    DWORD bytesWritten = GetSystemFirmwareTable('ACPI', 'TEPH', acpiData, acpiDataSize);

    if (bytesWritten != acpiDataSize) {
        HeapFree(GetProcessHeap(), 0, acpiData);
        return false;
    }

    // Search for VM indicators in ACPI data
    std::string haystack((char*)acpiData, bytesWritten);
    bool found = (haystack.find("VMWARE") != std::string::npos) || (haystack.find("VBOX") != std::string::npos) ||
                 (haystack.find("QEMU") != std::string::npos);

    HeapFree(GetProcessHeap(), 0, acpiData);
    return found;
}

bool IsOnVM() {
    // Core VM detection (reliable)
    if (HasVMRegistry() || HasVMFiles() || HasVMWareACPI()) {
        return true;
    }

    return false;
}

#pragma endregion

#pragma region AntiHook

bool HasAPIHooks() {
    // ONLY check anti-debug specific APIs in ntdll
    struct APICheck {
        const wchar_t* module;
        const char* function;
    };

    APICheck apis[] = {
        // Only ntdll anti-debug functions
        {L"ntdll.dll", "NtQueryInformationProcess"}, {L"ntdll.dll", "NtSetInformationThread"},
        {L"ntdll.dll", "DbgUiRemoteBreakin"},        {L"ntdll.dll", "DbgBreakPoint"},
        {L"ntdll.dll", "NtReadVirtualMemory"},
    };

    for (const auto& api : apis) {
        HMODULE hMod = GetModuleHandleW(api.module);
        if (!hMod)
            continue;

        FARPROC pFunc = GetProcAddress(hMod, api.function);
        if (!pFunc)
            continue;

        BYTE* pBytes = (BYTE*)pFunc;

        // Skip NOP padding
        while (*pBytes == 0x90)
            pBytes++;

        // Look for JMP/CALL patterns
        bool hasJmp = false;
        void* jumpTarget = nullptr;

        if (*pBytes == 0xE9) {
            // JMP rel32 - calculate target
            int32_t offset = *(int32_t*)(pBytes + 1);
            jumpTarget = pBytes + 5 + offset;
            hasJmp = true;
        } else if (*pBytes == 0xFF && pBytes[1] == 0x25) {
            // JMP [rip+disp32] - indirect jump (Detours/MinHook style)
            int32_t offset = *(int32_t*)(pBytes + 2);
            void** pTarget = (void**)(pBytes + 6 + offset);
            jumpTarget = *pTarget;
            hasJmp = true;
        } else if (*pBytes == 0x48 && pBytes[1] == 0xB8 && pBytes[10] == 0xFF && pBytes[11] == 0xE0) {
            // MOV RAX, imm64 ; JMP RAX - full pattern confirmation
            jumpTarget = *(void**)(pBytes + 2);
            hasJmp = true;
        }

        if (!hasJmp)
            continue;

        // Now verify: is this hook going somewhere SUSPICIOUS?
        // Legitimate hooks (AV, drivers) go to other loaded DLLs
        // Malicious hooks go to heap/private RWX memory
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(jumpTarget, &mbi, sizeof(mbi)) == 0)
            continue;

        // Hook to loaded module = probably legitimate (AV, driver, etc)
        if (mbi.Type == MEM_IMAGE)
            continue;

        // Hook to RWX heap/private memory = suspicious
        bool isRWX = (mbi.Protect & PAGE_EXECUTE_READWRITE) || (mbi.Protect & PAGE_EXECUTE_WRITECOPY);
        bool isPrivate = (mbi.Type == MEM_PRIVATE);

        if (isRWX || isPrivate) {
            return true;  // Suspicious hook detected
        }
    }

    return false;
}

#pragma endregion

#pragma region AntiTamper

static int GetTextSectionInfo(LPVOID lpModBase, SECTIONINFO* info) {
    PIMAGE_NT_HEADERS pNtHdr = ImageNtHeader(lpModBase);
    if (!pNtHdr)
        return -1;

    PIMAGE_SECTION_HEADER pSection = (PIMAGE_SECTION_HEADER)(pNtHdr + 1);

    for (int i = 0; i < pNtHdr->FileHeader.NumberOfSections; i++) {
        if (strcmp((char*)pSection->Name, ".text") == 0) {
            info->lpVirtualAddress = (LPVOID)((DWORD_PTR)lpModBase + pSection->VirtualAddress);
            info->dwSizeOfRawData = pSection->SizeOfRawData;
            return 0;
        }
        pSection++;
    }

    return -1;
}

DWORD64 HashSection(LPVOID lpAddress, DWORD dwSize) {
    DWORD64 hash = 0;
    BYTE* pData = (BYTE*)lpAddress;

    for (DWORD i = 0; i < dwSize; i++, pData++) {
        hash = *pData + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

std::vector<HASHSET> GetModuleSectionHashes() {
    std::vector<HASHSET> hashes;

    MODULEENTRY32W mEntry = {sizeof(MODULEENTRY32W)};
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE)
        return hashes;

    if (Module32FirstW(hSnapshot, &mEntry)) {
        do {
            SECTIONINFO info = {0};
            if (GetTextSectionInfo(mEntry.modBaseAddr, &info) == 0) {
                DWORD64 hash = HashSection(info.lpVirtualAddress, info.dwSizeOfRawData);
                hashes.push_back({hash, info});
            }
        } while (Module32NextW(hSnapshot, &mEntry));
    }

    CloseHandle(hSnapshot);
    return hashes;
}

bool VerifyIntegrity(const std::vector<HASHSET>& originalHashes) {
    for (const auto& entry : originalHashes) {
        DWORD64 currentHash = HashSection(entry.SectionInfo.lpVirtualAddress, entry.SectionInfo.dwSizeOfRawData);
        if (currentHash != entry.dwRealHash) {
            return false;  // Memory was tampered
        }
    }
    return true;
}

#pragma endregion

#pragma region Initialization

void Initialize() {
    // Apply anti-attach patches only when VMProtect detects a debugger
    if (ShouldApplyAntiAttach()) {
        PatchDbgBreakPoint();
        PatchDbgUiRemoteBreakin();
        HideThread(GetCurrentThread());
    }
}

#pragma endregion

#pragma region DetectionSystem

// Check running tools and return detections with appropriate scores
std::vector<Detection> CheckRunningTools() {
    std::vector<Detection> detections;

    // Tool definitions: {process_name, code, score}
    struct ToolDef {
        const wchar_t* name;
        const char* code;
        int score;
    };

    // VMPROTECT: Mutate tool-name checks
    COBBLE_STR_ENCRYPTW_BEGIN

    // TIER 3: Report-only. Detection is noisy; rely on VMProtect checks for decisions.
    ToolDef tools[] = {
        {L"x64dbg.exe", "TOOL_X64DBG", 10},
        {L"x32dbg.exe", "TOOL_X32DBG", 10},
        {L"ollydbg.exe", "TOOL_OLLYDBG", 10},
        {L"windbg.exe", "TOOL_WINDBG", 10},
        {L"ida.exe", "TOOL_IDA", 10},
        {L"ida64.exe", "TOOL_IDA64", 10},
        {L"idaq.exe", "TOOL_IDAQ", 10},
        {L"idaq64.exe", "TOOL_IDAQ64", 10},
        {L"dnspy.exe", "TOOL_DNSPY", 10},
        {L"cheatengine-x86_64.exe", "TOOL_CE", 10},
        {L"cheatengine.exe", "TOOL_CE", 10},
        {L"ce.exe", "TOOL_CE", 10},
        {L"processhacker.exe", "TOOL_PROCHACKER", 10},
        {L"extremeinjector.exe", "TOOL_INJECTOR", 10},
        {L"xenos.exe", "TOOL_XENOS", 10},
        {L"wireshark.exe", "TOOL_WIRESHARK", 10},
        {L"fiddler.exe", "TOOL_FIDDLER", 10},
        {L"charles.exe", "TOOL_CHARLES", 10},
        {L"httpdebugger.exe", "TOOL_HTTPDEBUG", 10},
        {L"mitmproxy.exe", "TOOL_MITMPROXY", 10},
    };

    COBBLE_STR_ENCRYPTW_END

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return detections;

    PROCESSENTRY32W pe = {sizeof(PROCESSENTRY32W)};

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            for (const auto& tool : tools) {
                if (_wcsicmp(pe.szExeFile, tool.name) == 0) {
                    Detection d;
                    d.code = tool.code;
                    d.score = tool.score;
                    // Use tool class as details, not exact name (to not leak intel)
                    d.details = OBFUSCATE_STRING("report");
                    detections.push_back(d);
                    break;
                }
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return detections;
}

// VMProtect-backed checks (preferred for decisions)
std::vector<Detection> CheckVMProtect() {
    std::vector<Detection> detections;

    // CHECK_PROTECTION is 1 only when the binary has been VMProtect-packed.
    // In dev builds it returns 0 and we leave detections empty.
    int protectedResult = 0;
    CHECK_PROTECTION(protectedResult, 1);
    if (protectedResult != 1) {
        return detections;
    }

    int result = 0;
    CHECK_DEBUGGER(result, 0);
    if (result != 0) {
        detections.push_back({OBFUSCATE_STRING("VMPROTECT_DEBUGGER"), 40, OBFUSCATE_STRING("debugger")});
    }

    result = 0;
    CHECK_VIRTUAL_PC(result, 0);
    if (result != 0) {
        detections.push_back({OBFUSCATE_STRING("VMPROTECT_VM"), 30, OBFUSCATE_STRING("vm")});
    }

    return detections;
}

// Check VM indicators (score 30 = ALLOW, info severity)
std::vector<Detection> CheckVMIndicators() {
    std::vector<Detection> detections;

    if (HasVMRegistry()) {
        detections.push_back({OBFUSCATE_STRING("VM_REGISTRY"), 30, OBFUSCATE_STRING("registry")});
    }
    if (HasVMFiles()) {
        detections.push_back({OBFUSCATE_STRING("VM_FILES"), 30, OBFUSCATE_STRING("files")});
    }
    if (HasVMWareACPI()) {
        detections.push_back({OBFUSCATE_STRING("VM_ACPI"), 30, OBFUSCATE_STRING("acpi")});
    }

    return detections;
}

// Run ALL security checks and return all detections
std::vector<Detection> RunSecurityChecks() {
    std::vector<Detection> all;

    // VMPROTECT: Primary anti-debug/anti-vm signals
    auto vmProtectDetections = CheckVMProtect();
    for (auto& d : vmProtectDetections) {
        all.push_back(d);
    }

    // TIER 3: Running tools (report only, no decision impact)
    auto toolDetections = CheckRunningTools();
    for (auto& d : toolDetections) {
        all.push_back(d);
    }

    // TIER 3: VM indicators (report only, no block)
    auto vmDetections = CheckVMIndicators();
    for (auto& d : vmDetections) {
        all.push_back(d);
    }

    return all;
}

// Calculate maximum score from all detections
int CalculateRiskScore(const std::vector<Detection>& detections) {
    int maxScore = 0;
    for (const auto& d : detections) {
        if (d.score > maxScore) {
            maxScore = d.score;
        }
    }
    return maxScore;
}

// Decide action based on score
// VMPROTECT ULTRA: Critical decision point
SecurityDecision Decide(int score) {
    COBBLE_VM_CRITICAL_START

    SecurityDecision result;
    if (score >= 100)
        result = SecurityDecision::BLOCK;
    else if (score >= 40)
        result = SecurityDecision::WARN;
    else
        result = SecurityDecision::ALLOW;

    COBBLE_VM_CRITICAL_END
    return result;
}

// Main entry point: run checks, report telemetry, return decision
// NOTE: Telemetry::Init() should be called once at loader startup, not here
SecurityDecision RunSecurityChecksWithDecision(std::vector<Detection>& outDetections) {
    // Run all checks
    outDetections = RunSecurityChecks();

    // Report each detection to telemetry (throttled)
    // Severity is derived from score using ScoreToSeverity()
    for (const auto& d : outDetections) {
        Telemetry::Severity sev = Telemetry::Severity::INFO;
        if (d.score >= 100)
            sev = Telemetry::Severity::CRITICAL;
        else if (d.score >= 40)
            sev = Telemetry::Severity::WARN;

        Telemetry::Report(d.code, sev, d.score, d.details);
    }

    // Calculate score and decide
    int score = CalculateRiskScore(outDetections);
    return Decide(score);
}

#pragma endregion

}  // namespace Security

#pragma optimize("", on)
