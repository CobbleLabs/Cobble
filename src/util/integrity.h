#pragma once

#include <Windows.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

namespace Integrity {

// FNV-1a 64-bit Hash (Custom implementation, no external dependencies like advapi32)
// This is crucial for manual mapping where imports might not be fully verifiable
inline uint64_t HashMemoryFNV64(const BYTE* data, DWORD size) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (DWORD i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Get .text section info for the current module
inline bool GetTextSection(HMODULE hModule, BYTE*& outAddr, DWORD& outSize) {
    if (!hModule) return false;
    
    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(hModule);
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<BYTE*>(hModule) + pDos->e_lfanew);
    if (pNt->Signature != IMAGE_NT_SIGNATURE) return false;
    
    auto* pSection = IMAGE_FIRST_SECTION(pNt);
    for (WORD i = 0; i < pNt->FileHeader.NumberOfSections; i++) {
        if (memcmp(pSection[i].Name, ".text", 5) == 0) {
            outAddr = reinterpret_cast<BYTE*>(hModule) + pSection[i].VirtualAddress;
            outSize = pSection[i].Misc.VirtualSize;
            return true;
        }
    }
    
    return false;
}

// Global storage for initial hash (set during init)
inline uint64_t g_InitialTextHash = 0;

// Initialize integrity check - call once during startup
inline bool Init(HMODULE hModule) {
    BYTE* textAddr = nullptr;
    DWORD textSize = 0;
    
    if (!GetTextSection(hModule, textAddr, textSize)) {
        return false;
    }
    
    g_InitialTextHash = HashMemoryFNV64(textAddr, textSize);
    return g_InitialTextHash != 0;
}

// Verify code hasn't been patched - call periodically or at critical points
// Returns true if OK, false if tampered
inline bool Verify(HMODULE hModule) {
    if (g_InitialTextHash == 0) return true; // Not initialized or disabled
    
    BYTE* textAddr = nullptr;
    DWORD textSize = 0;
    
    if (!GetTextSection(hModule, textAddr, textSize)) {
        return false;
    }
    
    uint64_t currentHash = HashMemoryFNV64(textAddr, textSize);
    return currentHash == g_InitialTextHash;
}

// Soft exit - clean exit without crash or suspicious behavior
inline void SoftExit(HMODULE hModule) {
    // Just signal shutdown, let normal cleanup handle it
    // Don't call ExitProcess or do anything noisy
}

} // namespace Integrity
