#pragma once

#include <Psapi.h>
#include <Windows.h>
#include <winternl.h>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "psapi.lib")

// ============================================================================
// LOADER VERIFICATION
//
// Confirms that the DLL was loaded by the legitimate Cobble loader (manual
// map via SecureMapper) and not via LoadLibrary by a cracker. The single
// signal we use is "is this module in the PEB Ldr list?" — a manually
// mapped image is invisible to the loader's bookkeeping, while a
// LoadLibrary'd image always shows up.
// ============================================================================

namespace LoaderVerify {

inline bool isInModuleList(HMODULE hModule) {
#ifdef _WIN64
    PPEB pPeb = (PPEB)__readgsqword(0x60);
#else
    PPEB pPeb = (PPEB)__readfsdword(0x30);
#endif
    if (!pPeb || !pPeb->Ldr)
        return false;

    PLIST_ENTRY head = &pPeb->Ldr->InMemoryOrderModuleList;
    PLIST_ENTRY current = head->Flink;

    while (current != head) {
        PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(current, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
        if (entry->DllBase == hModule) {
            return true;  // Found in list = LoadLibrary'd
        }
        current = current->Flink;
    }
    return false;  // Not in list = manual map (good)
}

inline bool verify(HMODULE hModule) {
    // If we're in the loader's module list, we were LoadLibrary'd → fail.
    return !isInModuleList(hModule);
}

// Lightweight re-verifier for periodic checks. Returns false so the caller
// (heartbeat thread, hot module path) decides how to react.
inline bool reverify(HMODULE hModule) {
    return verify(hModule);
}

// Silent exit if verification fails. We deliberately avoid any error
// strings: corrupt the DLL header to provoke a future crash, then exit.
inline void verifyOrDie(HMODULE hModule) {
    if (!verify(hModule)) {
        DWORD oldProtect;
        if (VirtualProtect(hModule, 0x1000, PAGE_READWRITE, &oldProtect)) {
            memset(hModule, 0xCC, 0x1000);  // Fill with INT3
            VirtualProtect(hModule, 0x1000, oldProtect, &oldProtect);
        }
        ExitProcess(0);
    }
}

}  // namespace LoaderVerify
