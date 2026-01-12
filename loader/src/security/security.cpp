#include "security.h"
#include <DbgHelp.h>
#include <intrin.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
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

bool CheckIsDebuggerPresentPatched() {
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32)
        return false;

    FARPROC pIsDebuggerPresent = GetProcAddress(hKernel32, "IsDebuggerPresent");
    if (!pIsDebuggerPresent)
        return false;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W ProcessEntry = {sizeof(PROCESSENTRY32W)};

    if (!Process32FirstW(hSnapshot, &ProcessEntry)) {
        CloseHandle(hSnapshot);
        return false;
    }

    bool bDebuggerPresent = false;
    DWORD dwCurrentPID = GetCurrentProcessId();
    DWORD dwLocalFuncBytes = *(PDWORD)pIsDebuggerPresent;

    do {
        if (dwCurrentPID == ProcessEntry.th32ProcessID)
            continue;

        HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, ProcessEntry.th32ProcessID);
        if (!hProcess)
            continue;

        DWORD dwRemoteFuncBytes = 0;
        if (ReadProcessMemory(hProcess, pIsDebuggerPresent, &dwRemoteFuncBytes, sizeof(DWORD), NULL)) {
            if (dwRemoteFuncBytes != dwLocalFuncBytes) {
                bDebuggerPresent = true;
            }
        }

        CloseHandle(hProcess);

        if (bDebuggerPresent)
            break;

    } while (Process32NextW(hSnapshot, &ProcessEntry));

    CloseHandle(hSnapshot);
    return bDebuggerPresent;
}

bool IsBeingDebugged() {
    return CheckRemoteDebugger() || CheckPEB() || CheckNtDebugFlags() || CheckNtDebugObject() || CheckHeapFlags() ||
           CheckHeapProtection() || CheckHardwareBreakpoints();
    // Note: CheckIsDebuggerPresentPatched() is expensive, use sparingly
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

bool HasSuspiciousSpecs() {
    // Check CPU count
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    if (sysInfo.dwNumberOfProcessors < 2)
        return true;

    // Check RAM (< 2GB is suspicious)
    MEMORYSTATUSEX memStatus = {sizeof(MEMORYSTATUSEX)};
    GlobalMemoryStatusEx(&memStatus);
    DWORD ramMB = (DWORD)(memStatus.ullTotalPhys / 1024 / 1024);
    if (ramMB < 2048)
        return true;

    return false;
}

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

// Check for analysis tools running
bool HasAnalysisTools() {
    // Build list dynamically to prevent static string extraction
    std::vector<std::wstring> badProcesses;
    badProcesses.reserve(28);

    // Debuggers
    badProcesses.push_back(_xorw_(L"x64dbg.exe"));
    badProcesses.push_back(_xorw_(L"x32dbg.exe"));
    badProcesses.push_back(_xorw_(L"ollydbg.exe"));
    badProcesses.push_back(_xorw_(L"windbg.exe"));

    // Disassemblers
    badProcesses.push_back(_xorw_(L"ida.exe"));
    badProcesses.push_back(_xorw_(L"ida64.exe"));
    badProcesses.push_back(_xorw_(L"idaq.exe"));
    badProcesses.push_back(_xorw_(L"idaq64.exe"));
    badProcesses.push_back(_xorw_(L"dnspy.exe"));

    // System monitors
    badProcesses.push_back(_xorw_(L"procmon.exe"));
    badProcesses.push_back(_xorw_(L"procmon64.exe"));
    badProcesses.push_back(_xorw_(L"procexp.exe"));
    badProcesses.push_back(_xorw_(L"procexp64.exe"));
    badProcesses.push_back(_xorw_(L"processhacker.exe"));

    // PE analyzers
    badProcesses.push_back(_xorw_(L"pestudio.exe"));
    badProcesses.push_back(_xorw_(L"hiew32.exe"));
    badProcesses.push_back(_xorw_(L"lordpe.exe"));
    badProcesses.push_back(_xorw_(L"die.exe"));

    // Network sniffers
    badProcesses.push_back(_xorw_(L"wireshark.exe"));
    badProcesses.push_back(_xorw_(L"fiddler.exe"));
    badProcesses.push_back(_xorw_(L"charles.exe"));
    badProcesses.push_back(_xorw_(L"dumpcap.exe"));
    badProcesses.push_back(_xorw_(L"httpdebugger.exe"));

    // Memory editors
    badProcesses.push_back(_xorw_(L"cheatengine-x86_64.exe"));
    badProcesses.push_back(_xorw_(L"cheatengine.exe"));

    // System tools
    badProcesses.push_back(_xorw_(L"autoruns.exe"));
    badProcesses.push_back(_xorw_(L"autorunsc.exe"));
    badProcesses.push_back(_xorw_(L"regshot.exe"));

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe = {sizeof(PROCESSENTRY32W)};

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            for (const auto& badProc : badProcesses) {
                if (_wcsicmp(pe.szExeFile, badProc.c_str()) == 0) {
                    CloseHandle(hSnapshot);
                    return true;
                }
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return false;
}

// Check for low system uptime (fresh sandbox)
bool HasLowUptime() {
    ULONGLONG uptimeMs = GetTickCount64();
    ULONGLONG uptimeMinutes = uptimeMs / 1000 / 60;

    // Less than 10 minutes uptime is suspicious (fresh sandbox)
    return uptimeMinutes < 10;
}

// Check for small disk (sandboxes often have tiny disks)
bool HasSmallDisk() {
    ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;

    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        ULONGLONG totalGB = totalBytes.QuadPart / 1024 / 1024 / 1024;
        // Less than 50GB disk is suspicious
        return totalGB < 50;
    }

    return false;
}

// Check for suspicious usernames
bool HasSuspiciousUsername() {
    wchar_t username[256] = {0};
    DWORD size = 256;
    GetUserNameW(username, &size);

    std::vector<std::wstring> badUsernames;
    badUsernames.reserve(21);
    badUsernames.push_back(_xorw_(L"user"));
    badUsernames.push_back(_xorw_(L"test"));
    badUsernames.push_back(_xorw_(L"sandbox"));
    badUsernames.push_back(_xorw_(L"malware"));
    badUsernames.push_back(_xorw_(L"virus"));
    badUsernames.push_back(_xorw_(L"sample"));
    badUsernames.push_back(_xorw_(L"admin"));
    badUsernames.push_back(_xorw_(L"administrator"));
    badUsernames.push_back(_xorw_(L"guest"));
    badUsernames.push_back(_xorw_(L"vmware"));
    badUsernames.push_back(_xorw_(L"vbox"));
    badUsernames.push_back(_xorw_(L"qemu"));
    badUsernames.push_back(_xorw_(L"analysis"));
    badUsernames.push_back(_xorw_(L"analyst"));
    badUsernames.push_back(_xorw_(L"john"));
    badUsernames.push_back(_xorw_(L"peter"));
    badUsernames.push_back(_xorw_(L"currentuser"));
    badUsernames.push_back(_xorw_(L"abby"));
    badUsernames.push_back(_xorw_(L"george"));
    badUsernames.push_back(_xorw_(L"hong"));
    badUsernames.push_back(_xorw_(L"user1"));

    for (const auto& badName : badUsernames) {
        if (_wcsicmp(username, badName.c_str()) == 0) {
            return true;
        }
    }

    return false;
}

// Check for suspicious computer names
bool HasSuspiciousComputerName() {
    wchar_t computerName[256] = {0};
    DWORD size = 256;
    GetComputerNameW(computerName, &size);

    // Only check for obvious sandbox names (NOT DESKTOP- or WIN- which are normal Windows defaults)
    std::vector<std::wstring> badNames;
    badNames.reserve(9);
    badNames.push_back(_xorw_(L"SANDBOX"));
    badNames.push_back(_xorw_(L"VIRUS"));
    badNames.push_back(_xorw_(L"MALWARE"));
    badNames.push_back(_xorw_(L"SAMPLE"));
    badNames.push_back(_xorw_(L"TEST-PC"));
    badNames.push_back(_xorw_(L"JOHN-PC"));
    badNames.push_back(_xorw_(L"ANALYSIS"));
    badNames.push_back(_xorw_(L"TEQUILABOOMBOOM"));
    badNames.push_back(_xorw_(L"SANDBOX-PC"));

    for (const auto& badName : badNames) {
        if (_wcsicmp(computerName, badName.c_str()) == 0) {
            return true;
        }
    }

    // Very short computer name is suspicious
    if (wcslen(computerName) < 4) {
        return true;
    }

    return false;
}

bool IsOnVM() {
    // Core VM detection (reliable)
    if (HasVMRegistry() || HasVMFiles() || HasVMWareACPI()) {
        return true;
    }

    // Analysis tools (highly suspicious)
    if (HasAnalysisTools()) {
        return true;
    }

    // Suspicious identifiers (sandbox names)
    if (HasSuspiciousComputerName()) {
        return true;
    }

    // NOTE: The following checks are DISABLED to reduce false positives:
    // - HasSuspiciousSpecs() -> Low CPU/RAM can be normal laptops
    // - HasLowUptime() -> User might have just rebooted
    // - HasSmallDisk() -> Normal for SSDs
    // - HasSuspiciousUsername() -> "admin" is common on real PCs

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
        if (*pData) {
            hash = *pData + (hash << 6) + (hash << 16) - hash;
        }
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

}  // namespace Security

#pragma optimize("", on)
