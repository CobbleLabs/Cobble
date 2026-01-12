#pragma once
#include <Psapi.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>

// DLL Self-Integrity Check
// Detects if critical code has been patched in memory

namespace DllIntegrity {

// Stored at startup, compared periodically
inline uint32_t g_ExpectedHash = 0;
inline uintptr_t g_TextBase = 0;
inline size_t g_TextSize = 0;
inline std::atomic<bool> g_Initialized{false};

// Simple fast hash (FNV-1a)
inline uint32_t hashMemory(const uint8_t* data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

// Find .text section of our DLL
inline bool findTextSection(HMODULE hModule) {
    if (!hModule)
        return false;

    // Get DOS header
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    // Get NT header
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)hModule + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return false;

    // Find .text section
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (memcmp(section->Name, ".text", 5) == 0) {
            g_TextBase = (uintptr_t)hModule + section->VirtualAddress;
            g_TextSize = section->Misc.VirtualSize;
            return true;
        }
    }

    return false;
}

// Initialize integrity check - call once at DLL startup
inline bool Initialize(HMODULE hModule) {
    if (g_Initialized)
        return true;

    if (!findTextSection(hModule)) {
        return false;
    }

    // Compute and store initial hash
    g_ExpectedHash = hashMemory((const uint8_t*)g_TextBase, g_TextSize);
    g_Initialized = true;

    return true;
}

// Verify integrity - call periodically
// Returns true if code is unchanged
inline bool Verify() {
    if (!g_Initialized || g_TextBase == 0 || g_TextSize == 0) {
        return false;  // Not initialized = fail closed
    }

    uint32_t currentHash = hashMemory((const uint8_t*)g_TextBase, g_TextSize);
    return (currentHash == g_ExpectedHash);
}

// Silent crash on tamper detection
inline void OnTamperDetected() {
    // Various "subtle" ways to crash that don't look like security
    volatile int* p = nullptr;
    *p = 0xDEAD;  // Null pointer dereference = looks like bug
}

// Check and crash if tampered
inline void Check() {
    if (!Verify()) {
        OnTamperDetected();
    }
}

}  // namespace DllIntegrity
