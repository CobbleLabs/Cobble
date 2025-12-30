#include "license.h"
#include "hwid.h"
#include "version.h"
#include <Windows.h>
#include <wininet.h>
#include <string>
#include <sstream>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

// Use centralized version
#define CLIENT_VERSION COBBLE_VERSION

namespace License {

static std::string g_LastError;
static bool g_NeedsUpdate = false;
static std::string g_MinVersion;
static std::string g_Username;
static int g_DaysRemaining = -1;

std::string GetLastError() {
    return g_LastError;
}

bool NeedsUpdate() {
    return g_NeedsUpdate;
}

std::string GetMinVersion() {
    return g_MinVersion;
}

std::string GetUsername() {
    return g_Username;
}

int GetDaysRemaining() {
    return g_DaysRemaining;
}

bool Verify(const std::string& serverUrl) {
    // Generate HWID
    std::string hwid = HWID::Generate();
    if (hwid.empty()) {
        g_LastError = "Failed to generate HWID";
        return false;
    }
    
    // Build URL
    std::string url = serverUrl + "/api/verify";
    
    // Build POST data with VERSION for anti-crack!
    std::string postData = "hwid=" + hwid + "&version=" + CLIENT_VERSION;
    
    // Initialize WinInet
    HINTERNET hInternet = InternetOpenA(
        "CobbleLoader/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL, NULL, 0
    );
    
    if (!hInternet) {
        g_LastError = "Failed to initialize internet";
        return false;
    }
    
    // Parse URL
    URL_COMPONENTSA urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    
    char hostName[256] = { 0 };
    char urlPath[1024] = { 0 };
    
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath);
    
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &urlComp)) {
        InternetCloseHandle(hInternet);
        g_LastError = "Invalid server URL";
        return false;
    }
    
    // Connect to server
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? 
                  INTERNET_FLAG_SECURE : 0;
    
    HINTERNET hConnect = InternetConnectA(
        hInternet,
        hostName,
        urlComp.nPort,
        NULL, NULL,
        INTERNET_SERVICE_HTTP,
        0, 0
    );
    
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        g_LastError = "Failed to connect to server";
        return false;
    }
    
    // Create request
    HINTERNET hRequest = HttpOpenRequestA(
        hConnect,
        "POST",
        urlPath,
        NULL, NULL, NULL,
        flags | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD,
        0
    );
    
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Failed to create request";
        return false;
    }
    
    // Set headers
    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    
    // Send request
    BOOL result = HttpSendRequestA(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        (LPVOID)postData.c_str(),
        (DWORD)postData.length()
    );
    
    if (!result) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Failed to send request";
        return false;
    }
    
    // Read response
    char buffer[1024] = { 0 };
    DWORD bytesRead = 0;
    std::string response;
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    
    // Cleanup
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    // Check for update response
    if (response.find("\"update\":true") != std::string::npos) {
        g_NeedsUpdate = true;
        
        // Extract min_version
        size_t verStart = response.find("\"min_version\":\"");
        if (verStart != std::string::npos) {
            verStart += 15;
            size_t verEnd = response.find("\"", verStart);
            if (verEnd != std::string::npos) {
                g_MinVersion = response.substr(verStart, verEnd - verStart);
            }
        }
        
        g_LastError = "Update required! You have v" + std::string(CLIENT_VERSION) + ", need v" + g_MinVersion;
        return false;
    }
    
    // Check for valid license
    if (response.find("\"valid\":true") != std::string::npos ||
        response.find("\"valid\": true") != std::string::npos) {
        
        // Extract username (handle both "username":"X" and "username": "X")
        size_t userStart = response.find("\"username\":");
        if (userStart != std::string::npos) {
            userStart += 11; // skip "username":
            // Skip whitespace and opening quote
            while (userStart < response.length() && (response[userStart] == ' ' || response[userStart] == '"')) {
                userStart++;
            }
            size_t userEnd = response.find("\"", userStart);
            if (userEnd != std::string::npos) {
                g_Username = response.substr(userStart, userEnd - userStart);
            }
        }
        
        // Extract days_remaining
        size_t daysStart = response.find("\"days_remaining\":");
        if (daysStart != std::string::npos) {
            daysStart += 17;
            size_t daysEnd = response.find_first_not_of("0123456789", daysStart);
            if (daysEnd != std::string::npos) {
                std::string daysStr = response.substr(daysStart, daysEnd - daysStart);
                g_DaysRemaining = std::stoi(daysStr);
            }
        }
        
        return true;
    }
    
    // Extract error message
    size_t msgStart = response.find("\"message\":\"");
    if (msgStart != std::string::npos) {
        msgStart += 11;
        size_t msgEnd = response.find("\"", msgStart);
        if (msgEnd != std::string::npos) {
            g_LastError = response.substr(msgStart, msgEnd - msgStart);
        }
    } else {
        g_LastError = "License not valid";
    }
    
    return false;
}

// Register new user (returns true if successful)
bool Register(const std::string& serverUrl, const std::string& username) {
    std::string hwid = HWID::Generate();
    if (hwid.empty()) {
        g_LastError = "Failed to generate HWID";
        return false;
    }
    
    std::string url = serverUrl + "/api/register";
    std::string postData = "hwid=" + hwid + "&username=" + username;
    
    HINTERNET hInternet = InternetOpenA("CobbleLoader/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;
    
    URL_COMPONENTSA urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    char hostName[256] = { 0 };
    char urlPath[1024] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath);
    
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &urlComp)) {
        InternetCloseHandle(hInternet);
        return false;
    }
    
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;
    
    HINTERNET hConnect = InternetConnectA(hInternet, hostName, urlComp.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }
    
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", urlPath, NULL, NULL, NULL, flags | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }
    
    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
    
    char buffer[1024] = { 0 };
    DWORD bytesRead = 0;
    std::string response;
    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    if (response.find("\"success\":true") != std::string::npos) {
        return true;
    }
    
    // Extract error
    size_t errStart = response.find("\"error\":\"");
    if (errStart != std::string::npos) {
        errStart += 9;
        size_t errEnd = response.find("\"", errStart);
        if (errEnd != std::string::npos) {
            g_LastError = response.substr(errStart, errEnd - errStart);
        }
    }
    
    return false;
}

} // namespace License
