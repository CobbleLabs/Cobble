#pragma once

#include <Windows.h>
#include <cstdint>

// ============================================================================
// SECURE MAPPER - Shared Memory Section Injection
// Uses NT syscalls and shared memory to avoid detection
// ============================================================================

namespace SecureMapper {

// Shellcode structure passed to remote process
struct MAPPER_CONTEXT {
    uint64_t moduleBase;    // Base address of mapped module
    uint64_t ntdllBase;     // ntdll.dll base in target
    uint64_t kernel32Base;  // kernel32.dll base in target

    // Function pointers resolved in target
    void* pLdrLoadDll;
    void* pLdrGetProcedureAddress;
    void* pRtlInitAnsiString;
    void* pRtlInitUnicodeString;

    // Completion flag
    volatile uint64_t completionFlag;
    volatile uint32_t lastError;
};

// Main secure mapping function - injects from memory buffer using shared sections
bool SecureManualMap(DWORD pid, BYTE* pData, size_t dataSize);

// Check if already injected (via mutex)
bool IsAlreadyInjected(DWORD pid);

}  // namespace SecureMapper
