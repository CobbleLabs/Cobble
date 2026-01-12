#pragma once
#include <Windows.h>
#include <winternl.h>
#include <cstdint>

#pragma comment(lib, "ntdll.lib")

class AdvancedSecurity {
private:
    // NT API function types
    typedef NTSTATUS(NTAPI* NtQuerySystemInformation_t)(ULONG, PVOID, ULONG, PULONG);
    typedef NTSTATUS(NTAPI* NtQueryInformationProcess_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    typedef NTSTATUS(NTAPI* NtQueryInformationThread_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    typedef NTSTATUS(NTAPI* NtResumeThread_t)(HANDLE, PULONG);

    static inline NtQuerySystemInformation_t pNtQuerySystemInformation = nullptr;
    static inline NtQueryInformationProcess_t pNtQueryInformationProcess = nullptr;
    static inline NtQueryInformationThread_t pNtQueryInformationThread = nullptr;
    static inline NtResumeThread_t pNtResumeThread = nullptr;

    static bool initNtFunctions() {
        if (pNtQuerySystemInformation)
            return true;

        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll)
            return false;

        pNtQuerySystemInformation = (NtQuerySystemInformation_t)GetProcAddress(ntdll, "NtQuerySystemInformation");
        pNtQueryInformationProcess = (NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
        pNtQueryInformationThread = (NtQueryInformationThread_t)GetProcAddress(ntdll, "NtQueryInformationThread");
        pNtResumeThread = (NtResumeThread_t)GetProcAddress(ntdll, "NtResumeThread");

        return pNtQuerySystemInformation && pNtQueryInformationProcess;
    }

public:
    // 1. VM Detection - Detects virtual machines
    static bool isVirtualMachine() {
        if (!initNtFunctions())
            return false;

        // SystemHypervisorDetailInformation = 0x9F
        struct SYSTEM_HYPERVISOR_DETAIL_INFORMATION {
            ULONG HvVendorAndMaxFunction[4];
            ULONG HvInterface[4];
        } shdi = {};

        NTSTATUS status = pNtQuerySystemInformation(0x9F, &shdi, sizeof(shdi), nullptr);

        // If hypervisor info available, check vendor
        if (status == 0) {
            // Check if zeroed (no hypervisor)
            bool isZeroed = true;
            for (int i = 0; i < 4 && isZeroed; i++) {
                if (shdi.HvVendorAndMaxFunction[i] != 0)
                    isZeroed = false;
            }

            if (!isZeroed) {
                // Extract vendor string from CPUID-style data
                char vendor[13] = {};
                memcpy(vendor, &shdi.HvVendorAndMaxFunction[1], 12);

                // "Microsoft Hv" is Windows Hyper-V (legitimate)
                // Others like "VMwareVMware", "VBoxVBoxVBox" are VMs
                if (strcmp(vendor, "Microsoft Hv") != 0 && vendor[0] != '\0') {
                    return true;  // Running in non-Microsoft VM
                }
            }
        }

        return false;
    }

    // 2. Kernel Debugger Detection
    static bool hasKernelDebugger() {
        if (!initNtFunctions())
            return false;

        // SystemKernelDebuggerInformation = 0x23
        struct SYSTEM_KERNEL_DEBUGGER_INFORMATION {
            BOOLEAN DebuggerEnabled;
            BOOLEAN DebuggerNotPresent;
        } skdi = {};

        NTSTATUS status = pNtQuerySystemInformation(0x23, &skdi, sizeof(skdi), nullptr);

        if (status == 0) {
            // Debugger is present if enabled OR if "not present" flag is false
            if (skdi.DebuggerEnabled || !skdi.DebuggerNotPresent) {
                return true;
            }
        }

        // Also check process debug port
        HANDLE debugPort = nullptr;
        // ProcessDebugPort = 0x7
        if (pNtQueryInformationProcess) {
            status = pNtQueryInformationProcess(GetCurrentProcess(), 0x7, &debugPort, sizeof(debugPort), nullptr);
            if (status == 0 && debugPort != nullptr) {
                return true;
            }
        }

        // ProcessDebugObjectHandle = 0x1E
        HANDLE debugObject = nullptr;
        if (pNtQueryInformationProcess) {
            status = pNtQueryInformationProcess(GetCurrentProcess(), 0x1E, &debugObject, sizeof(debugObject), nullptr);
            // If we CAN query it (status == 0), debugger is attached
            if (status == 0) {
                return true;
            }
        }

        return false;
    }

    // 3. Thread Monitoring - Checks if threads were suspended
    static bool isThreadSuspended(HANDLE hThread) {
        if (!hThread || !pNtResumeThread)
            return false;

        ULONG suspendCount = 0;
        NTSTATUS status = pNtResumeThread(hThread, &suspendCount);

        if (status == 0 && suspendCount > 0) {
            // Thread was suspended! Re-suspend it to maintain state
            SuspendThread(hThread);
            return true;
        }

        return false;
    }

    // 4. Syscall Tampering Detection - Verifies ntdll wasn't hooked
    static bool hasSyscallHooks() {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll)
            return true;  // If we can't get ntdll, assume tampered

        // List of critical NT functions to check
        const char* criticalFunctions[] = {
            "NtQuerySystemInformation", "NtQueryInformationProcess", "NtOpenProcess",           "NtReadVirtualMemory",
            "NtWriteVirtualMemory",     "NtProtectVirtualMemory",    "NtAllocateVirtualMemory", "NtCreateThreadEx"};

        for (const char* funcName : criticalFunctions) {
            FARPROC func = GetProcAddress(ntdll, funcName);
            if (!func)
                continue;

            uint8_t* bytes = (uint8_t*)func;

            // Expected NT syscall stub pattern (x64):
            // 4C 8B D1       mov r10, rcx
            // B8 XX XX XX XX mov eax, <syscall_number>
            //
            // If first bytes are different, it's likely hooked

            bool isStandardStub = (bytes[0] == 0x4C && bytes[1] == 0x8B && bytes[2] == 0xD1 && bytes[3] == 0xB8);

            // Also check for common hook patterns
            bool isJmpHook = (bytes[0] == 0xE9);                          // jmp
            bool isCallHook = (bytes[0] == 0xE8);                         // call
            bool isPushRetHook = (bytes[0] == 0x68 && bytes[5] == 0xC3);  // push addr; ret
            bool isMovRaxHook = (bytes[0] == 0x48 && bytes[1] == 0xB8);   // mov rax, addr

            if (!isStandardStub || isJmpHook || isCallHook || isPushRetHook || isMovRaxHook) {
                return true;  // Hooked!
            }
        }

        return false;
    }

    // Combined check - runs all security checks
    static bool isCompromised() { return isVirtualMachine() || hasKernelDebugger() || hasSyscallHooks(); }
};
