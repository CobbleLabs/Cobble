#pragma once

#include <Windows.h>
#include <wincrypt.h>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

namespace Integrity {

// Calculate SHA256 of a memory region
inline std::string HashMemory(const BYTE* data, DWORD size) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;
    
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }
    
    if (!CryptHashData(hHash, data, size, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    
    DWORD hashLen = 32;
    BYTE hashBytes[32];
    if (CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0)) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (DWORD i = 0; i < hashLen; i++) {
            oss << std::setw(2) << (int)hashBytes[i];
        }
        result = oss.str();
    }
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    return result;
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
inline std::string g_InitialTextHash;

// Initialize integrity check - call once during startup
inline bool Init(HMODULE hModule) {
    BYTE* textAddr = nullptr;
    DWORD textSize = 0;
    
    if (!GetTextSection(hModule, textAddr, textSize)) {
        return false;
    }
    
    g_InitialTextHash = HashMemory(textAddr, textSize);
    return !g_InitialTextHash.empty();
}

// Verify code hasn't been patched - call periodically or at critical points
// Returns true if OK, false if tampered
inline bool Verify(HMODULE hModule) {
    if (g_InitialTextHash.empty()) return true; // Not initialized
    
    BYTE* textAddr = nullptr;
    DWORD textSize = 0;
    
    if (!GetTextSection(hModule, textAddr, textSize)) {
        return false;
    }
    
    std::string currentHash = HashMemory(textAddr, textSize);
    return currentHash == g_InitialTextHash;
}

// Soft exit - clean exit without crash or suspicious behavior
inline void SoftExit(HMODULE hModule) {
    // Just signal shutdown, let normal cleanup handle it
    // Don't call ExitProcess or do anything noisy
}

} // namespace Integrity
