#pragma once
#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstdlib>

namespace integrity_data {
    extern const uint32_t EXPECTED_HASHES[];
    extern const size_t NUM_CHUNKS;
}

class IntegrityGuard {
private:
    // SHA256-based hash (truncated to 32-bit for simplicity)
    static uint32_t hashRegion(const uint8_t* data, size_t size) {
        // Use Windows CryptoAPI for SHA256
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        BYTE hash[32];
        DWORD hashLen = 32;
        uint32_t result = 0;
        
        if (CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                if (CryptHashData(hHash, data, static_cast<DWORD>(size), 0)) {
                    if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
                        // Take first 4 bytes as uint32
                        result = *reinterpret_cast<uint32_t*>(hash);
                    }
                }
                CryptDestroyHash(hHash);
            }
            CryptReleaseContext(hProv, 0);
        }
        
        return result;
    }
    
    // Get module base and size
    static bool getModuleInfo(HMODULE* outModule, uint8_t** outBase, size_t* outSize) {
        *outModule = GetModuleHandle(nullptr);
        if (!*outModule) return false;
        
        MODULEINFO mi;
        if (!GetModuleInformation(GetCurrentProcess(), *outModule, &mi, sizeof(mi))) {
            return false;
        }
        
        *outBase = static_cast<uint8_t*>(mi.lpBaseOfDll);
        *outSize = mi.SizeOfImage;
        return true;
    }
    
public:
    // Verify all chunks
    static bool verify() {
        HMODULE hModule;
        uint8_t* base;
        size_t totalSize;
        
        if (!getModuleInfo(&hModule, &base, &totalSize)) {
            return false;
        }
        
        // Calculate chunk size
        size_t chunkSize = totalSize / integrity_data::NUM_CHUNKS;
        
        // Verify each chunk
        for (size_t i = 0; i < integrity_data::NUM_CHUNKS; i++) {
            uint8_t* chunkStart = base + (i * chunkSize);
            size_t currentChunkSize = (i == integrity_data::NUM_CHUNKS - 1) 
                ? (totalSize - (i * chunkSize))  // Last chunk may be smaller
                : chunkSize;
            
            uint32_t hash = hashRegion(chunkStart, currentChunkSize);
            
            if (hash != integrity_data::EXPECTED_HASHES[i]) {
                // Code tampered - silent crash (looks like normal bug)
                *(volatile int*)nullptr = 0;
                return false;
            }
        }
        
        return true;
    }
    
    // Checkpoint - call periodically, not every time
    static inline void checkpoint() {
        static int counter = 0;
        
        // Only verify every ~17 calls to avoid overhead
        if (++counter % 17 == 0) {
            if (!verify()) {
                std::abort();
            }
        }
    }
    
    // Force immediate verification (critical points)
    static inline void forceCheck() {
        if (!verify()) {
            *(volatile int*)nullptr = 0;
        }
    }
};

// Macro for inserting checkpoints
#define INTEGRITY_CHECK() IntegrityGuard::checkpoint()
#define INTEGRITY_FORCE_CHECK() IntegrityGuard::forceCheck()
