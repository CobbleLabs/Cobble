#pragma once

#include <Windows.h>
#include <wincrypt.h>
#include <vector>
#include <cstdint>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================================
// PAYLOAD ENCRYPTION/DECRYPTION
// Uses AES-256-CBC for DLL payload protection
// ============================================================================

namespace PayloadCrypto {

// 32-byte key for AES-256 (should match build tool)
// This key is obfuscated at compile time - the real key is derived
constexpr uint8_t PAYLOAD_KEY_SEED[32] = {
    0x4C, 0x6F, 0x61, 0x64, 0x65, 0x72, 0x4B, 0x65,
    0x79, 0x32, 0x30, 0x32, 0x36, 0x43, 0x6F, 0x62,
    0x62, 0x6C, 0x65, 0x53, 0x65, 0x63, 0x75, 0x72,
    0x65, 0x50, 0x61, 0x79, 0x6C, 0x6F, 0x61, 0x64
};

// Derive actual key from seed (simple obfuscation)
inline void DeriveKey(uint8_t* outKey) {
    for (int i = 0; i < 32; i++) {
        outKey[i] = PAYLOAD_KEY_SEED[i] ^ ((i * 0x37) & 0xFF) ^ 0xAB;
    }
}

// Header prepended to encrypted payload
struct PayloadHeader {
    uint8_t magic[4];       // "CBLP" (Cobble Payload)
    uint32_t version;       // Format version
    uint32_t originalSize;  // Original DLL size
    uint32_t encryptedSize; // Encrypted data size (after header)
    uint8_t iv[16];         // AES-CBC IV
    uint8_t reserved[8];    // Future use
};

constexpr uint8_t PAYLOAD_MAGIC[4] = { 'C', 'B', 'L', 'P' };
constexpr uint32_t PAYLOAD_VERSION = 1;

// Decrypt payload using Windows CryptoAPI
// Returns empty vector on failure
inline std::vector<uint8_t> DecryptPayload(const uint8_t* encryptedData, size_t encryptedSize) {
    if (!encryptedData || encryptedSize < sizeof(PayloadHeader) + 16) {
        MessageBoxA(nullptr, "Decrypt: Invalid input size", "Debug", MB_OK);
        return {};
    }
    
    // Parse header
    const PayloadHeader* header = reinterpret_cast<const PayloadHeader*>(encryptedData);
    
    // Validate magic
    if (memcmp(header->magic, PAYLOAD_MAGIC, 4) != 0) {
        char msg[256];
        sprintf_s(msg, "Decrypt: Bad magic. Got: %02X %02X %02X %02X", 
            header->magic[0], header->magic[1], header->magic[2], header->magic[3]);
        MessageBoxA(nullptr, msg, "Debug", MB_OK);
        return {};
    }
    
    // Validate version
    if (header->version != PAYLOAD_VERSION) {
        char msg[128];
        sprintf_s(msg, "Decrypt: Bad version. Got: %u, Expected: %u", header->version, PAYLOAD_VERSION);
        MessageBoxA(nullptr, msg, "Debug", MB_OK);
        return {};
    }
    
    // Validate sizes
    if (header->encryptedSize > encryptedSize - sizeof(PayloadHeader)) {
        char msg[256];
        sprintf_s(msg, "Decrypt: Size mismatch. encSize=%u, totalSize=%zu, headerSize=%zu", 
            header->encryptedSize, encryptedSize, sizeof(PayloadHeader));
        MessageBoxA(nullptr, msg, "Debug", MB_OK);
        return {};
    }
    
    // Get encryption key
    uint8_t key[32];
    DeriveKey(key);
    
    // Initialize crypto provider
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, MS_ENH_RSA_AES_PROV_W, 
                               PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        MessageBoxA(nullptr, "Decrypt: CryptAcquireContext failed", "Debug", MB_OK);
        return {};
    }
    
    // Create key blob for AES-256
    struct {
        BLOBHEADER hdr;
        DWORD keySize;
        BYTE keyData[32];
    } keyBlob;
    
    keyBlob.hdr.bType = PLAINTEXTKEYBLOB;
    keyBlob.hdr.bVersion = CUR_BLOB_VERSION;
    keyBlob.hdr.reserved = 0;
    keyBlob.hdr.aiKeyAlg = CALG_AES_256;
    keyBlob.keySize = 32;
    memcpy(keyBlob.keyData, key, 32);
    
    // Import key
    HCRYPTKEY hKey = 0;
    if (!CryptImportKey(hProv, (BYTE*)&keyBlob, sizeof(keyBlob), 0, 0, &hKey)) {
        char msg[128];
        sprintf_s(msg, "Decrypt: CryptImportKey failed. Error: %lu", GetLastError());
        MessageBoxA(nullptr, msg, "Debug", MB_OK);
        CryptReleaseContext(hProv, 0);
        return {};
    }
    
    // Set IV
    if (!CryptSetKeyParam(hKey, KP_IV, header->iv, 0)) {
        char msg[128];
        sprintf_s(msg, "Decrypt: CryptSetKeyParam IV failed. Error: %lu", GetLastError());
        MessageBoxA(nullptr, msg, "Debug", MB_OK);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return {};
    }
    
    // Set CBC mode
    DWORD mode = CRYPT_MODE_CBC;
    CryptSetKeyParam(hKey, KP_MODE, (BYTE*)&mode, 0);
    
    // Prepare output buffer
    const uint8_t* ciphertext = encryptedData + sizeof(PayloadHeader);
    DWORD dataLen = header->encryptedSize;
    
    std::vector<uint8_t> decrypted(dataLen);
    memcpy(decrypted.data(), ciphertext, dataLen);
    
    // Decrypt
    BOOL final = TRUE;
    if (!CryptDecrypt(hKey, 0, final, 0, decrypted.data(), &dataLen)) {
        char msg[128];
        sprintf_s(msg, "Decrypt: CryptDecrypt failed. Error: %lu", GetLastError());
        MessageBoxA(nullptr, msg, "Debug", MB_OK);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return {};
    }
    
    // Resize to actual decrypted size (removes PKCS7 padding)
    decrypted.resize(header->originalSize);
    
    // Cleanup
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);
    
    // Clear key from memory
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(&keyBlob, sizeof(keyBlob));
    
    return decrypted;
}

// Validate that decrypted data is a valid PE
inline bool ValidatePE(const std::vector<uint8_t>& data) {
    if (data.size() < 0x1000) return false;
    
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
    if (dos->e_magic != 0x5A4D) return false;
    
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > data.size()) return false;
    
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(data.data() + dos->e_lfanew);
    if (nt->Signature != 0x4550) return false;
    
    return true;
}

} // namespace PayloadCrypto
