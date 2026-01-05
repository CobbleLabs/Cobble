#pragma once

#include <string>
#include <vector>
#include <Windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <winhttp.h>
#include "../security/xor.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
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

// Progress callback signature: void callback(size_t downloaded, size_t total)
using ProgressCallback = void(*)(size_t downloaded, size_t total);

// Download file to temp with verification and progress reporting
// Returns true if download successful and hash matches
inline bool downloadAndVerify(const std::string& url, const std::string& destPath,
                              const std::string& expectedHash, size_t expectedSize,
                              const std::string& hwid, ProgressCallback progress = nullptr) {
    // Download to .tmp first
    std::string tmpPath = destPath + ".tmp";
    
    // Parse URL
    bool isHttps = (url.find("https://") == 0);
    std::string host, path;
    size_t start = isHttps ? 8 : 7;
    size_t slashPos = url.find('/', start);
    if (slashPos == std::string::npos) {
        host = url.substr(start);
        path = "/";
    } else {
        host = url.substr(start, slashPos - start);
        path = url.substr(slashPos);
    }
    
    // Add HWID to path
    path += "?hwid=" + hwid;
    
    // Convert to wide strings for WinHTTP
    std::wstring wHost(host.begin(), host.end());
    std::wstring wPath(path.begin(), path.end());
    
    // Open session
    HINTERNET session = WinHttpOpen(_xorw_(L"Mozilla/5.0"), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) return false;
    
    // Set timeouts (10 seconds for download)
    DWORD timeout = 10000;
    WinHttpSetOption(session, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    
    // Connect
    INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET connect = WinHttpConnect(session, wHost.c_str(), port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }
    
    // Create request
    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", wPath.c_str(), nullptr, 
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // Send request
    if (!WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        nullptr, &statusCode, &statusSize, nullptr);
    if (statusCode != 200) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // Open output file
    HANDLE file = CreateFileA(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // Download in chunks with progress
    size_t totalDownloaded = 0;
    BYTE buffer[8192];
    DWORD bytesAvailable = 0;
    DWORD bytesRead = 0;
    
    while (true) {
        // Check how much data is available
        if (!WinHttpQueryDataAvailable(request, &bytesAvailable)) {
            break;
        }
        
        if (bytesAvailable == 0) {
            break; // No more data
        }
        
        // Read available data (up to buffer size)
        DWORD toRead = (bytesAvailable > sizeof(buffer)) ? sizeof(buffer) : bytesAvailable;
        if (!WinHttpReadData(request, buffer, toRead, &bytesRead) || bytesRead == 0) {
            break;
        }
        
        // Write to file
        DWORD bytesWritten;
        WriteFile(file, buffer, bytesRead, &bytesWritten, nullptr);
        totalDownloaded += bytesRead;
        
        // Call progress callback
        if (progress) {
            progress(totalDownloaded, expectedSize);
        }
    }
    
    FlushFileBuffers(file);
    CloseHandle(file);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    // Verify size
    if (totalDownloaded != expectedSize) {
        DeleteFileA(tmpPath.c_str());
        return false;
    }
    
    // Verify hash
    std::string actualHash = sha256File(tmpPath);
    if (actualHash != expectedHash) {
        DeleteFileA(tmpPath.c_str());
        return false;
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
