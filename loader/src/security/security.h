#pragma once
#include <Windows.h>
#include <winternl.h>
#include <vector>

// Forward declarations for NT types
typedef NTSTATUS(NTAPI* NtQueryInformationProcessTypedef)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

// Structures for anti-attach patching
#pragma pack(push, 1)
struct DbgUiRemoteBreakinPatch {
    WORD  push_0;
    BYTE  push;
    DWORD CurrentPorcessHandle;
    BYTE  mov_eax;
    DWORD TerminateProcess;
    WORD  call_eax;
};
#pragma pack(pop)

// Section info for integrity checking
struct SECTIONINFO {
    LPVOID lpVirtualAddress;
    DWORD  dwSizeOfRawData;
};

struct HASHSET {
    DWORD64     dwRealHash;
    SECTIONINFO SectionInfo;
};

namespace Security {

    // ==================== ANTI-DEBUG ====================
    
    // Basic API check
    bool CheckRemoteDebugger();
    
    // PEB BeingDebugged flag (manual access)
    bool CheckPEB();
    
    // NtQueryInformationProcess with DebugFlags
    bool CheckNtDebugFlags();
    
    // NtQueryInformationProcess with DebugObject
    bool CheckNtDebugObject();
    
    // Heap debug flags (HEAP_GROWABLE check)
    bool CheckHeapFlags();
    
    // Heap protection marker (0xABABABAB)
    bool CheckHeapProtection();
    
    // Hardware breakpoint registers (DR0-DR3)
    bool CheckHardwareBreakpoints();
    
    // Cross-process IsDebuggerPresent patch detection
    bool CheckIsDebuggerPresentPatched();
    
    // Combined anti-debug check
    bool IsBeingDebugged();
    
    // ==================== ANTI-ATTACH ====================
    
    // Patch DbgBreakPoint to RET
    void PatchDbgBreakPoint();
    
    // Patch DbgUiRemoteBreakin to call TerminateProcess
    void PatchDbgUiRemoteBreakin();
    
    // Hide thread from debugger
    void HideThread(HANDLE hThread);
    
    // ==================== ANTI-VM ====================
    
    // Check for low specs (CPU < 2, RAM < 2GB)
    bool HasSuspiciousSpecs();
    
    // Check for VMTools registry key
    bool HasVMRegistry();
    
    // Check for vm3dgl.dll file
    bool HasVMFiles();
    
    // Check ACPI tables for "VMWARE"
    bool HasVMWareACPI();
    
    // Combined VM check
    bool IsOnVM();
    
    // ==================== ANTI-HOOK ====================
    
    // Check if critical APIs have been hooked (JMP/RET at start)
    bool HasAPIHooks();
    
    // ==================== ANTI-TAMPER ====================
    
    // Get hash of .text section
    DWORD64 HashSection(LPVOID lpAddress, DWORD dwSize);
    
    // Get section info for all loaded modules
    std::vector<HASHSET> GetModuleSectionHashes();
    
    // Verify current hashes match original
    bool VerifyIntegrity(const std::vector<HASHSET>& originalHashes);
    
    // ==================== INITIALIZATION ====================
    
    // Run all startup security measures
    void Initialize();
    
    // Run continuous security checks (call in loop)
    bool RunChecks();
}
