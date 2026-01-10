#pragma once

#include <Windows.h>
#include <wincrypt.h>
#include <vector>
#include <cstdint>
#include <string>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================================
// PAYLOAD ENCRYPTION/DECRYPTION
// Uses AES-256-CBC for DLL payload protection
// Server-provided key ONLY - no legacy fallback
// ============================================================================

namespace PayloadCrypto {

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

// Convert hex string to bytes (64 hex chars -> 32 bytes)
inline bool HexToBytes(const std::string& hex, uint8_t* out, size_t outLen) {
    if (hex.length() != outLen * 2) return false;
    for (size_t i = 0; i < outLen; i++) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hv = hexVal(high), lv = hexVal(low);
        if (hv < 0 || lv < 0) return false;
        out[i] = (uint8_t)((hv << 4) | lv);
    }
    return true;
}

// Decrypt payload using server-provided key (64 hex chars)
// This is the ONLY decryption path - server key REQUIRED
// Returns empty vector on failure
inline std::vector<uint8_t> DecryptPayloadWithKey(const uint8_t* encryptedData, size_t encryptedSize, const std::string& hexKey) {
    if (!encryptedData || encryptedSize < sizeof(PayloadHeader) + 16) {
        return {};
    }
    
    // Parse hex key (64 chars -> 32 bytes) - REQUIRED
    uint8_t key[32];
    if (hexKey.length() != 64 || !HexToBytes(hexKey, key, 32)) {
        return {};
    }
    
    // Parse header
    const PayloadHeader* header = reinterpret_cast<const PayloadHeader*>(encryptedData);
    
    // Validate magic
    if (memcmp(header->magic, PAYLOAD_MAGIC, 4) != 0) {
        return {};
    }
    
    // Validate version
    if (header->version != PAYLOAD_VERSION) {
        return {};
    }
    
    // Validate sizes
    if (header->encryptedSize > encryptedSize - sizeof(PayloadHeader)) {
        return {};
    }
    
    // Initialize crypto provider
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, MS_ENH_RSA_AES_PROV_W, 
                               PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        SecureZeroMemory(key, sizeof(key));
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
        CryptReleaseContext(hProv, 0);
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(&keyBlob, sizeof(keyBlob));
        return {};
    }
    
    // Set IV
    if (!CryptSetKeyParam(hKey, KP_IV, (BYTE*)header->iv, 0)) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(&keyBlob, sizeof(keyBlob));
        return {};
    }
    
    // Set CBC mode
    DWORD mode = CRYPT_MODE_CBC;
    if (!CryptSetKeyParam(hKey, KP_MODE, (BYTE*)&mode, 0)) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(&keyBlob, sizeof(keyBlob));
        return {};
    }
    
    // Decrypt
    const uint8_t* ciphertext = encryptedData + sizeof(PayloadHeader);
    std::vector<uint8_t> plaintext(header->encryptedSize);
    memcpy(plaintext.data(), ciphertext, header->encryptedSize);
    
    DWORD decryptedLen = header->encryptedSize;
    if (!CryptDecrypt(hKey, 0, TRUE, 0, plaintext.data(), &decryptedLen)) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(&keyBlob, sizeof(keyBlob));
        return {};
    }
    
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);
    
    // Securely clear key material
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(&keyBlob, sizeof(keyBlob));
    
    plaintext.resize(decryptedLen);
    
    // Verify size matches expected
    if (decryptedLen != header->originalSize) {
        return {};
    }
    
    return plaintext;
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
