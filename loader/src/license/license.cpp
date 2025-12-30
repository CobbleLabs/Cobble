#include "license.h"
#include "hwid.h"
#include "version.h"
#include "../util/json.h"
#include <Windows.h>
#include <wininet.h>
#include <string>
#include <sstream>
#include <iomanip>


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

std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}


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
    
    // Build POST data with VERSION
    std::string postData = "hwid=" + UrlEncode(hwid) + "&version=" + UrlEncode(CLIENT_VERSION);
    
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

    DWORD timeout = 4000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    
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

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeSize, NULL);

    if (statusCode != 200) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        g_LastError = "Server returned error (Status: " + std::to_string(statusCode) + ")";
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
    
    // Parse JSON response
    json::Object json;
    if (!json.parse(response)) {
        g_LastError = "Invalid server response";
        return false;
    }
    
    // Check for update requirement
    if (json.get_bool("update", false)) {
        g_NeedsUpdate = true;
        g_MinVersion = json.get_string("min_version", "");
        g_LastError = "Update required! You have v" + std::string(CLIENT_VERSION) + ", need v" + g_MinVersion;
        return false;
    }
    
    // Check for valid license
    if (json.get_bool("valid", false)) {
        g_Username = json.get_string("username", "");
        g_DaysRemaining = json.get_int("days_remaining", -1);
        return true;
    }
    
    // Extract error message
    g_LastError = json.get_string("message", "License not valid");
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

}
