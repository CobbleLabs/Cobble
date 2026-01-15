#pragma once
#include <Windows.h>
#include <winternl.h>
#include <string>
#include <vector>

// Forward declarations for NT types
typedef NTSTATUS(NTAPI* NtQueryInformationProcessTypedef)(HANDLE ProcessHandle,
                                                          PROCESSINFOCLASS ProcessInformationClass,
                                                          PVOID ProcessInformation, ULONG ProcessInformationLength,
                                                          PULONG ReturnLength);

// Structures for anti-attach patching
#pragma pack(push, 1)
struct DbgUiRemoteBreakinPatch {
    WORD push_0;
    BYTE push;
    DWORD CurrentPorcessHandle;
    BYTE mov_eax;
    DWORD TerminateProcess;
    WORD call_eax;
};
#pragma pack(pop)

// Section info for integrity checking
struct SECTIONINFO {
    LPVOID lpVirtualAddress;
    DWORD dwSizeOfRawData;
};

struct HASHSET {
    DWORD64 dwRealHash;
    SECTIONINFO SectionInfo;
};

namespace Security {

// ==================== DETECTION SYSTEM ====================

// Detection result from a security check
// NOTE: severity is derived from score, not stored (score >= 100 = critical, >= 40 = warn, else info)
struct Detection {
    std::string code;     // "DBG_PEB", "TOOL_X64DBG", etc.
    int score;            // Risk score (100 = BLOCK, 40 = WARN, <40 = REPORT)
    std::string details;  // Optional context
};

// Helper: derive severity string from score (for telemetry)
inline const char* ScoreToSeverity(int score) {
    if (score >= 100)
        return "critical";
    if (score >= 40)
        return "warn";
    return "info";
}

// Final decision after evaluating all detections
enum class SecurityDecision {
    ALLOW,  // score < 40: continue normally
    WARN,   // score >= 40: show warning, allow retry
    BLOCK   // score >= 100: silent exit
};

// NEW API: Run all security checks and return detections
std::vector<Detection> RunSecurityChecks();

// NEW API: Calculate final score from detections
int CalculateRiskScore(const std::vector<Detection>& detections);

// NEW API: Decide action based on score
SecurityDecision Decide(int score);

// ==================== INDIVIDUAL CHECKS (internal, return Detection) ====================

// Anti-debug checks (TIER 1: score = 100)
Detection CheckPEB_v2();
Detection CheckNtDebugObject_v2();
Detection CheckNtDebugFlags_v2();
Detection CheckRemoteDebugger_v2();
Detection CheckHardwareBreakpoints_v2();
Detection CheckHeapFlags_v2();

// Tool detection (TIER 2/3: variable scores)
std::vector<Detection> CheckRunningTools();

// VM detection (TIER 3: score = 30)
std::vector<Detection> CheckVMIndicators();

// ==================== LEGACY API (for compatibility) ====================

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

// Combined anti-debug check (legacy)
bool IsBeingDebugged();

// ==================== ANTI-ATTACH ====================

// Patch DbgBreakPoint to RET
void PatchDbgBreakPoint();

// Patch DbgUiRemoteBreakin to call TerminateProcess
void PatchDbgUiRemoteBreakin();

// Hide thread from debugger
void HideThread(HANDLE hThread);

// ==================== ANTI-VM ====================

// Check for VMTools registry key
bool HasVMRegistry();

// Check for vm3dgl.dll file
bool HasVMFiles();

// Check ACPI tables for "VMWARE"
bool HasVMWareACPI();

// Combined VM check (legacy)
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

// Run all startup security measures (patches, thread hiding)
void Initialize();

// NEW: Run security checks with score-based decision
// Returns: ALLOW, WARN, or BLOCK
SecurityDecision RunSecurityChecksWithDecision(std::vector<Detection>& outDetections);

}  // namespace Security
