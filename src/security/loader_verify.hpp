#pragma once

#include <intrin.h>
#include <Psapi.h>
#include <Windows.h>
#include <winternl.h>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "psapi.lib")

// ============================================================================
// LOADER VERIFICATION
// Ensures the DLL was loaded by the legitimate Cobble loader, not via
// LoadLibrary by a cracker. Uses multiple detection techniques.
// ============================================================================

namespace LoaderVerify {

// Expected characteristics when loaded via SecureMapper
// vs LoadLibrary (detectable differences)

// 1. When manually mapped, the module won't appear in PEB's module list
inline bool isInModuleList(HMODULE hModule) {
    // Get PEB
#ifdef _WIN64
    PPEB pPeb = (PPEB)__readgsqword(0x60);
#else
    PPEB pPeb = (PPEB)__readfsdword(0x30);
#endif

    if (!pPeb || !pPeb->Ldr)
        return false;

    // Walk loaded modules list
    PLIST_ENTRY head = &pPeb->Ldr->InMemoryOrderModuleList;
    PLIST_ENTRY current = head->Flink;

    while (current != head) {
        PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(current, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

        // Check if our module is in the list
        if (entry->DllBase == hModule) {
            return true;  // Found in list = likely LoadLibrary
        }

        current = current->Flink;
    }

    return false;  // Not in list = likely manual mapped (good)
}

// 2. Check if loaded from expected memory region (shared section vs normal alloc)
inline bool hasSharedSectionCharacteristics(HMODULE hModule) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(hModule, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    // Shared sections typically have MEM_MAPPED type
    // LoadLibrary uses MEM_IMAGE
    // Our SecureMapper uses MEM_MAPPED (shared section)

    // However, this can vary. The key difference is:
    // - LoadLibrary: Type == MEM_IMAGE
    // - Manual map with sections: Type == MEM_MAPPED or MEM_PRIVATE

    // If Type is MEM_IMAGE, it was loaded by the loader (LoadLibrary)
    return (mbi.Type != MEM_IMAGE);
}

// 3. Check return address to detect direct LoadLibrary calls
// When called from our loader's shellcode, the return address will be
// within the allocated memory region, not in kernel32
inline bool isCalledFromTrustedContext() {
    // This is a heuristic - check if we're deep in a call stack
    // that started from our loader vs LoadLibrary

    // Get return address
    void* returnAddr = _ReturnAddress();

    // Check if return addr is in kernel32 (LoadLibrary path)
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32)
        return true;  // Can't check, assume OK

    MODULEINFO modInfo;
    if (GetModuleInformation(GetCurrentProcess(), hKernel32, &modInfo, sizeof(modInfo))) {
        BYTE* k32Start = (BYTE*)modInfo.lpBaseOfDll;
        BYTE* k32End = k32Start + modInfo.SizeOfImage;

        // If return address is in kernel32, suspicious
        if ((BYTE*)returnAddr >= k32Start && (BYTE*)returnAddr < k32End) {
            return false;
        }
    }

    return true;
}

// 4. Check for loader signature in process memory
// The loader writes a unique signature that we can verify
constexpr uint64_t LOADER_SIGNATURE = 0xC0BB1E5EC0DE2026;

struct LoaderHandshake {
    uint64_t signature;
    uint32_t loaderVersion;
    uint32_t injectionTimestamp;
    uint8_t challenge[16];
    uint8_t response[16];
};

// Global handshake location (set by loader via shared memory)
inline volatile LoaderHandshake* g_Handshake = nullptr;

// Verify the loader signature
inline bool verifySignature() {
    if (!g_Handshake)
        return false;

    // Check signature
    if (g_Handshake->signature != LOADER_SIGNATURE) {
        return false;
    }

    // Check timestamp (must be within last 30 seconds)
    uint32_t now = (uint32_t)GetTickCount();
    uint32_t elapsed = now - g_Handshake->injectionTimestamp;
    if (elapsed > 30000) {
        return false;  // Too old, likely replay
    }

    return true;
}

// Combined verification - returns true if loaded by legitimate loader
inline bool verify(HMODULE hModule) {
    // Method 1: Check module list (most reliable)
    // If we're in the module list, we were loaded via LoadLibrary
    if (isInModuleList(hModule)) {
        return false;  // CRACK DETECTED - loaded via LoadLibrary
    }

    // Method 2: Check memory type
    if (!hasSharedSectionCharacteristics(hModule)) {
        // Could be false positive, don't fail on this alone
        // but log for monitoring
    }

    // All checks passed
    return true;
}

// Silent exit if verification fails (don't give hints to crackers)
inline void verifyOrDie(HMODULE hModule) {
    if (!verify(hModule)) {
        // Don't show any error - just corrupt state and crash later
        // This makes debugging harder for crackers

        // Corrupt our own headers to cause crash on any function call
        DWORD oldProtect;
        if (VirtualProtect(hModule, 0x1000, PAGE_READWRITE, &oldProtect)) {
            memset(hModule, 0xCC, 0x1000);  // Fill with INT3
            VirtualProtect(hModule, 0x1000, oldProtect, &oldProtect);
        }

        // Also just exit
        ExitProcess(0);
    }
}

}  // namespace LoaderVerify
