#include "license.h"
#include "hwid.h"
#include <Windows.h>
#include <wininet.h>
#include <string>
#include <sstream>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

namespace License {

static std::string g_LastError;

std::string GetLastError() {
    return g_LastError;
}

bool Verify(const std::string& serverUrl, const std::string& apiKey) {
    // Generate HWID
    std::string hwid = HWID::Generate();
    if (hwid.empty()) {
        g_LastError = "Failed to generate HWID";
        return false;
    }
    
    // Build URL
    std::string url = serverUrl + "/api/verify";
    
    // Build POST data
    std::string postData = "hwid=" + hwid;
    
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
    headers += "X-API-Key: " + apiKey + "\r\n";
    
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
    
    // Check status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &statusCode, &statusCodeSize, NULL);
    
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
    
    // Check response
    if (statusCode != 200) {
        g_LastError = "Server returned error: " + std::to_string(statusCode);
        return false;
    }
    
    // Simple JSON parsing (look for "valid":true)
    if (response.find("\"valid\":true") != std::string::npos ||
        response.find("\"valid\": true") != std::string::npos) {
        return true;
    }
    
    // Try to extract error message
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

} // namespace License
