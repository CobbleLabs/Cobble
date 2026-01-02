#pragma once

#include <string>
#include <vector>
#include <Windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <urlmon.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "ole32.lib")

namespace Updater {

// SHA256 implementation for file verification
inline std::string sha256File(const std::string& path) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, 
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "";
    
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(file);
        return "";
    }
    
    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        CloseHandle(file);
        return "";
    }
    
    BYTE buffer[65536];
    DWORD bytesRead;
    while (ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        CryptHashData(hash, buffer, bytesRead, 0);
    }
    
    BYTE hashValue[32];
    DWORD hashLen = 32;
    CryptGetHashParam(hash, HP_HASHVAL, hashValue, &hashLen, 0);
    
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    CloseHandle(file);
    
    // Convert to hex
    static const char* hex = "0123456789abcdef";
    std::string result;
    for (DWORD i = 0; i < hashLen; i++) {
        result += hex[hashValue[i] >> 4];
        result += hex[hashValue[i] & 0xf];
    }
    return result;
}

// Get temp directory for downloads
inline std::string getTempDownloadDir() {
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    
    // Generate random folder name
    GUID guid;
    CoCreateGuid(&guid);
    char guidStr[64];
    sprintf_s(guidStr, "%08x%04x%04x", guid.Data1, guid.Data2, guid.Data3);
    
    std::string dir = std::string(temp) + "cb_" + guidStr;
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

// Download file to temp with verification
// Returns true if download successful and hash matches
inline bool downloadAndVerify(const std::string& url, const std::string& destPath,
                              const std::string& expectedHash, size_t expectedSize,
                              const std::string& hwid) {
    // Download to .tmp first
    std::string tmpPath = destPath + ".tmp";
    
    // Build full URL with HWID
    std::string fullUrl = url + "?hwid=" + hwid;
    
    // Use URLDownloadToFile for simplicity
    // In production, use WinHTTP for progress callbacks
    HRESULT hr = URLDownloadToFileA(nullptr, fullUrl.c_str(), tmpPath.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        DeleteFileA(tmpPath.c_str());
        return false;
    }
    
    // Verify size
    HANDLE file = CreateFileA(tmpPath.c_str(), GENERIC_READ, 0, nullptr, 
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);
    CloseHandle(file);
    
    if ((size_t)size.QuadPart != expectedSize) {
        DeleteFileA(tmpPath.c_str());
        return false;
    }
    
    // Verify hash
    std::string actualHash = sha256File(tmpPath);
    if (actualHash != expectedHash) {
        DeleteFileA(tmpPath.c_str());
        return false;
    }
    
    // Flush and rename atomically
    file = CreateFileA(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, 
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(file);
        CloseHandle(file);
    }
    
    // Rename tmp -> final
    if (!MoveFileExA(tmpPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(tmpPath.c_str());
        return false;
    }
    
    return true;
}

// Get current executable path
inline std::string getSelfPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return path;
}

// Spawn self-update: launches the NEW loader with update mode args
// The new loader will wait for us to exit, then replace us
inline bool spawnStub(const std::string& currentPath, const std::string& newPath) {
    // Command line: newLoader.exe --update-pid 1234 --update-old "oldpath" --update-new "newpath"
    char cmdLine[2048];
    sprintf_s(cmdLine, "\"%s\" --update-pid %lu --update-old \"%s\" --update-new \"%s\"",
              newPath.c_str(), GetCurrentProcessId(), 
              currentPath.c_str(), newPath.c_str());
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE, 
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Cleanup temp directory
inline void cleanupTempDir(const std::string& dir) {
    // Remove all files in dir
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                DeleteFileA((dir + "\\" + fd.cFileName).c_str());
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryA(dir.c_str());
}

} // namespace Updater
