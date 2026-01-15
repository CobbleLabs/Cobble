#include "security.h"
#include <DbgHelp.h>
#include <intrin.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include "../license/telemetry.hpp"
#include "xor.h"

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

// ============================================================================
// ANTI-DEBUG IMPLEMENTATIONS
// ============================================================================

namespace Security {

#pragma region AntiDebug

bool CheckRemoteDebugger() {
    BOOL isDebuggerPresent = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &isDebuggerPresent);
    return isDebuggerPresent != FALSE;
}

bool CheckPEB() {
#ifdef _WIN64
    PPEB pPeb = (PPEB)__readgsqword(0x60);
#else
    PPEB pPeb = (PPEB)__readfsdword(0x30);
#endif
    return pPeb->BeingDebugged == 1;
}

bool CheckNtDebugFlags() {
    const DWORD ProcessDebugFlags = 0x1f;

    auto NtQueryInformationProcess = reinterpret_cast<NtQueryInformationProcessTypedef>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));

    if (!NtQueryInformationProcess)
        return false;

    DWORD debugInherit = 0;
    NTSTATUS status = NtQueryInformationProcess(GetCurrentProcess(), (PROCESSINFOCLASS)ProcessDebugFlags, &debugInherit,
                                                sizeof(DWORD), nullptr);

    // If NoDebugInherit is 0, a debugger is present
    return (status == 0 && debugInherit == 0);
}

bool CheckNtDebugObject() {
    const DWORD ProcessDebugObjectHandle = 0x1e;

    auto NtQueryInformationProcess = reinterpret_cast<NtQueryInformationProcessTypedef>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));

    if (!NtQueryInformationProcess)
        return false;

    HANDLE debugObject = nullptr;
    NTSTATUS status = NtQueryInformationProcess(GetCurrentProcess(), (PROCESSINFOCLASS)ProcessDebugObjectHandle,
                                                &debugObject, sizeof(HANDLE), nullptr);

    return (status == 0 && debugObject != nullptr);
}

bool CheckHeapFlags() {
#ifdef _WIN64
    PPEB pPeb = (PPEB)__readgsqword(0x60);
    PVOID pHeapBase = (PVOID)(*(PDWORD_PTR)((PBYTE)pPeb + 0x30));
    DWORD dwHeapFlagsOffset = 0x70;
    DWORD dwHeapForceFlagsOffset = 0x74;
#else
    PPEB pPeb = (PPEB)__readfsdword(0x30);
    PVOID pHeapBase = (PVOID)(*(PDWORD_PTR)((PBYTE)pPeb + 0x18));
    DWORD dwHeapFlagsOffset = 0x40;
    DWORD dwHeapForceFlagsOffset = 0x44;
#endif

    PDWORD pdwHeapFlags = (PDWORD)((PBYTE)pHeapBase + dwHeapFlagsOffset);
    PDWORD pdwHeapForceFlags = (PDWORD)((PBYTE)pHeapBase + dwHeapForceFlagsOffset);

    return (*pdwHeapFlags & ~HEAP_GROWABLE) || (*pdwHeapForceFlags != 0);
}

bool CheckHeapProtection() {
    PROCESS_HEAP_ENTRY HeapEntry = {0};

    do {
        if (!HeapWalk(GetProcessHeap(), &HeapEntry))
            return false;
    } while (HeapEntry.wFlags != PROCESS_HEAP_ENTRY_BUSY);

    PVOID pOverlapped = (PBYTE)HeapEntry.lpData + HeapEntry.cbData;
    return ((DWORD)(*(PDWORD)pOverlapped) == 0xABABABAB);
}

bool CheckHardwareBreakpoints() {
    CONTEXT ctx = {0};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    RtlCaptureContext(&ctx);

    return (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3);
}

// Check if debugger windows are visible (fast check, no false positives)
bool CheckDebuggerWindow() {
    if (FindWindowA(nullptr, "x64dbg") != nullptr)
        return true;
    if (FindWindowA(nullptr, "x32dbg") != nullptr)
        return true;
    if (FindWindowA("OLLYDBG", nullptr) != nullptr)
        return true;
    if (FindWindowA("WinDbgFrameClass", nullptr) != nullptr)
        return true;
    if (FindWindowA(nullptr, "Cheat Engine") != nullptr)
        return true;

    return false;
}

bool IsBeingDebugged() {
    // Core NT-level checks (most reliable)
    if (CheckRemoteDebugger() || CheckPEB() || CheckNtDebugFlags() || CheckNtDebugObject())
        return true;

    // Memory/register-level checks
    if (CheckHeapFlags() || CheckHeapProtection() || CheckHardwareBreakpoints())
        return true;

    return false;
}

#pragma endregion

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

#pragma endregion

#pragma region AntiVM

bool HasVMRegistry() {
    std::vector<std::wstring> vmKeys;
    vmKeys.reserve(6);
    vmKeys.push_back(_xorw_(L"System\\ControlSet001\\Services\\VMTools"));
    vmKeys.push_back(_xorw_(L"System\\ControlSet001\\Services\\VBoxGuest"));
    vmKeys.push_back(_xorw_(L"System\\ControlSet001\\Services\\VBoxMouse"));
    vmKeys.push_back(_xorw_(L"System\\ControlSet001\\Services\\VBoxService"));
    vmKeys.push_back(_xorw_(L"SOFTWARE\\Oracle\\VirtualBox Guest Additions"));
    vmKeys.push_back(_xorw_(L"SOFTWARE\\VMware, Inc.\\VMware Tools"));

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
    vmFiles.push_back(_xorw_(L"C:\\Windows\\System32\\vm3dgl.dll"));
    vmFiles.push_back(_xorw_(L"C:\\Windows\\System32\\vboxdisp.dll"));
    vmFiles.push_back(_xorw_(L"C:\\Windows\\System32\\vboxhook.dll"));
    vmFiles.push_back(_xorw_(L"C:\\Windows\\System32\\vboxogl.dll"));
    vmFiles.push_back(_xorw_(L"C:\\Windows\\System32\\drivers\\vboxmouse.sys"));
    vmFiles.push_back(_xorw_(L"C:\\Windows\\System32\\drivers\\vmhgfs.sys"));

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
    // List of critical APIs to check
    struct APICheck {
        const wchar_t* module;
        const char* function;
    };

    APICheck apis[] = {
        {L"kernel32.dll", "GetModuleHandleA"},   {L"kernel32.dll", "GetProcAddress"},
        {L"kernel32.dll", "VirtualAlloc"},       {L"kernel32.dll", "VirtualProtect"},
        {L"kernel32.dll", "CreateRemoteThread"}, {L"kernel32.dll", "WriteProcessMemory"},
        {L"kernel32.dll", "TerminateProcess"},   {L"ntdll.dll", "NtSetInformationThread"},
        {L"ntdll.dll", "NtQueryVirtualMemory"},  {L"ntdll.dll", "NtQueryInformationProcess"},
        {L"user32.dll", "FindWindowA"},
    };

    for (const auto& api : apis) {
        HMODULE hMod = GetModuleHandleW(api.module);
        if (!hMod)
            continue;

        FARPROC pFunc = GetProcAddress(hMod, api.function);
        if (!pFunc)
            continue;

        BYTE* pBytes = (BYTE*)pFunc;

        // Skip NOP instructions
        while (*pBytes == 0x90)
            pBytes++;

        // Check for JMP (0xE9) or RET (0xC3) at function start
        if (*pBytes == 0xE9 || *pBytes == 0xC3) {
            return true;
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
    // 1. Apply anti-attach patches
    PatchDbgBreakPoint();
    PatchDbgUiRemoteBreakin();

    // 2. Hide main thread
    HideThread(GetCurrentThread());
}

bool RunChecks() {
    // Run all security checks
    if (IsBeingDebugged())
        return false;
    if (HasAPIHooks())
        return false;
    return true;
}

#pragma endregion

#pragma region DetectionSystem

// Detection wrapper for each check
Detection CheckPEB_v2() {
    Detection d = {"DBG_PEB", 0, ""};
    if (CheckPEB()) {
        d.score = 100;
        d.details = "PEB.BeingDebugged";
    }
    return d;
}

Detection CheckNtDebugObject_v2() {
    Detection d = {"DBG_DEBUGOBJECT", 0, ""};
    if (CheckNtDebugObject()) {
        d.score = 100;
        d.details = "DebugObjectHandle";
    }
    return d;
}

Detection CheckNtDebugFlags_v2() {
    Detection d = {"DBG_DEBUGFLAGS", 0, ""};
    if (CheckNtDebugFlags()) {
        d.score = 100;
        d.details = "NoDebugInherit";
    }
    return d;
}

Detection CheckRemoteDebugger_v2() {
    Detection d = {"DBG_REMOTE", 0, ""};
    if (CheckRemoteDebugger()) {
        d.score = 100;
        d.details = "RemoteDebugger";
    }
    return d;
}

Detection CheckHardwareBreakpoints_v2() {
    Detection d = {"DBG_HWBP", 0, ""};
    if (CheckHardwareBreakpoints()) {
        d.score = 100;
        d.details = "HardwareBP";
    }
    return d;
}

Detection CheckHeapFlags_v2() {
    Detection d = {"DBG_HEAP", 0, ""};
    if (CheckHeapFlags()) {
        d.score = 80;
        d.details = "HeapFlags";
    }
    return d;
}

// Check running tools and return detections with appropriate scores
std::vector<Detection> CheckRunningTools() {
    std::vector<Detection> detections;

    // Tool definitions: {process_name, code, score}
    struct ToolDef {
        const wchar_t* name;
        const char* code;
        int score;
    };

    // TIER 2: Debuggers/CheatEngine (score = 40, WARN)
    // TIER 3: Network tools (score = 10, REPORT ONLY)
    ToolDef tools[] = {
        // TIER 2 - WARN
        {L"x64dbg.exe", "TOOL_X64DBG", 40},
        {L"x32dbg.exe", "TOOL_X32DBG", 40},
        {L"ollydbg.exe", "TOOL_OLLYDBG", 40},
        {L"windbg.exe", "TOOL_WINDBG", 40},
        {L"ida.exe", "TOOL_IDA", 40},
        {L"ida64.exe", "TOOL_IDA64", 40},
        {L"idaq.exe", "TOOL_IDAQ", 40},
        {L"idaq64.exe", "TOOL_IDAQ64", 40},
        {L"dnspy.exe", "TOOL_DNSPY", 40},
        {L"cheatengine-x86_64.exe", "TOOL_CE", 40},
        {L"cheatengine.exe", "TOOL_CE", 40},
        {L"ce.exe", "TOOL_CE", 40},
        {L"processhacker.exe", "TOOL_PROCHACKER", 40},
        {L"extremeinjector.exe", "TOOL_INJECTOR", 40},
        {L"xenos.exe", "TOOL_XENOS", 40},
        // TIER 3 - REPORT ONLY
        {L"wireshark.exe", "TOOL_WIRESHARK", 10},
        {L"fiddler.exe", "TOOL_FIDDLER", 10},
        {L"charles.exe", "TOOL_CHARLES", 10},
        {L"httpdebugger.exe", "TOOL_HTTPDEBUG", 10},
        {L"mitmproxy.exe", "TOOL_MITMPROXY", 10},
    };

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
                    d.details = (tool.score >= 40) ? "debugger" : "network";
                    detections.push_back(d);
                    break;
                }
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return detections;
}

// Check VM indicators (score 30 = ALLOW, info severity)
std::vector<Detection> CheckVMIndicators() {
    std::vector<Detection> detections;

    if (HasVMRegistry()) {
        detections.push_back({"VM_REGISTRY", 30, "registry"});
    }
    if (HasVMFiles()) {
        detections.push_back({"VM_FILES", 30, "files"});
    }
    if (HasVMWareACPI()) {
        detections.push_back({"VM_ACPI", 30, "acpi"});
    }

    return detections;
}

// Run ALL security checks and return all detections
std::vector<Detection> RunSecurityChecks() {
    std::vector<Detection> all;

    // TIER 1: Anti-debug (score 80-100)
    auto d1 = CheckPEB_v2();
    if (d1.score > 0)
        all.push_back(d1);

    auto d2 = CheckNtDebugObject_v2();
    if (d2.score > 0)
        all.push_back(d2);

    auto d3 = CheckNtDebugFlags_v2();
    if (d3.score > 0)
        all.push_back(d3);

    auto d4 = CheckRemoteDebugger_v2();
    if (d4.score > 0)
        all.push_back(d4);

    auto d5 = CheckHardwareBreakpoints_v2();
    if (d5.score > 0)
        all.push_back(d5);

    auto d6 = CheckHeapFlags_v2();
    if (d6.score > 0)
        all.push_back(d6);

    // TIER 2/3: Running tools
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
SecurityDecision Decide(int score) {
    if (score >= 100)
        return SecurityDecision::BLOCK;
    if (score >= 40)
        return SecurityDecision::WARN;
    return SecurityDecision::ALLOW;
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
