#pragma once

#include <string>
#include <Windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// Simple HTTP client for config sync
// Uses WinHTTP (built into Windows)

namespace http {

struct Response {
    bool success = false;
    int statusCode = 0;
    std::string body;
    std::string error;
};

inline Response get(const std::wstring& host, int port, const std::wstring& path, bool https = false) {
    Response res;
    
    HINTERNET hSession = WinHttpOpen(L"Cobble/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (!hSession) {
        res.error = "WinHttpOpen failed";
        return res;
    }
    
    // Set timeout (5 seconds)
    DWORD timeout = 5000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) {
        res.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    
    if (!hRequest) {
        res.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        res.error = "WinHttpSendRequest failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        res.error = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &statusCodeSize, nullptr);
    res.statusCode = static_cast<int>(statusCode);
    
    // Read body
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            res.body.append(buffer.data(), bytesRead);
        }
    }
    
    res.success = (statusCode >= 200 && statusCode < 300);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return res;
}

inline Response post(const std::wstring& host, int port, const std::wstring& path, 
                     const std::string& body, bool https = false) {
    Response res;
    
    HINTERNET hSession = WinHttpOpen(L"Cobble/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (!hSession) {
        res.error = "WinHttpOpen failed";
        return res;
    }
    
    DWORD timeout = 5000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) {
        res.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    
    if (!hRequest) {
        res.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    const wchar_t* headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    
    if (!WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1L),
        (LPVOID)body.c_str(), static_cast<DWORD>(body.length()),
        static_cast<DWORD>(body.length()), 0)) {
        res.error = "WinHttpSendRequest failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        res.error = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &statusCodeSize, nullptr);
    res.statusCode = static_cast<int>(statusCode);
    
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            res.body.append(buffer.data(), bytesRead);
        }
    }
    
    res.success = (statusCode >= 200 && statusCode < 300);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return res;
}

// URL encode a string
inline std::string urlencode(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            result += buf;
        }
    }
    return result;
}

// ============================================================
// Mojang API - Username to UUID lookup
// ============================================================

// Response from Mojang API: {"id":"uuid_without_dashes","name":"Username"}
struct MojangProfile {
    std::string uuid;       // UUID with dashes
    std::string name;       // Exact capitalized name from Mojang
    bool found = false;
};

// Convert UUID without dashes to standard format with dashes
inline std::string formatUUID(const std::string& raw) {
    if (raw.length() != 32) return raw;
    // xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    return raw.substr(0, 8) + "-" + raw.substr(8, 4) + "-" + 
           raw.substr(12, 4) + "-" + raw.substr(16, 4) + "-" + raw.substr(20);
}

// Lookup Minecraft username -> UUID via Mojang API
// API: GET https://api.mojang.com/users/profiles/minecraft/{username}
// Returns: {"id":"uuid_no_dashes","name":"ExactName"} or 204 No Content if not found
inline MojangProfile lookupMojangUUID(const std::string& username) {
    MojangProfile profile;
    
    // Build path: /users/profiles/minecraft/{username}
    std::wstring path = L"/users/profiles/minecraft/";
    for (char c : username) {
        path += static_cast<wchar_t>(c);
    }
    
    // Call Mojang API (HTTPS on port 443)
    auto response = get(L"api.mojang.com", 443, path, true);
    
    if (!response.success) {
        return profile;  // API error or user not found
    }
    
    // Parse simple JSON: {"id":"uuid","name":"Name"}
    // Simple parsing since we control the format
    std::string& body = response.body;
    
    // Find "id":"value"
    size_t idPos = body.find("\"id\":");
    if (idPos != std::string::npos) {
        size_t start = body.find('\"', idPos + 5);
        if (start != std::string::npos) {
            size_t end = body.find('\"', start + 1);
            if (end != std::string::npos) {
                std::string rawUUID = body.substr(start + 1, end - start - 1);
                profile.uuid = formatUUID(rawUUID);
            }
        }
    }
    
    // Find "name":"value"
    size_t namePos = body.find("\"name\":");
    if (namePos != std::string::npos) {
        size_t start = body.find('\"', namePos + 7);
        if (start != std::string::npos) {
            size_t end = body.find('\"', start + 1);
            if (end != std::string::npos) {
                profile.name = body.substr(start + 1, end - start - 1);
            }
        }
    }
    
    profile.found = !profile.uuid.empty();
    return profile;
}

} // namespace http
