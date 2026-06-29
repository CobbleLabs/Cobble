#include "http_client.h"
#include "xorstr.h"
#include <winhttp.h>
#include <sstream>
#include <algorithm>
#include <vector>

#pragma comment(lib, "winhttp.lib")

std::wstring HttpClient::s_baseUrl = XORW("https://jet-stopping-allan-absent.trycloudflare.com");

// ─── Set Base URL ────────────────────────────────────────────────────────────
void HttpClient::SetBaseUrl(const std::wstring& baseUrl) {
    OPAQUE_JUNK();
    s_baseUrl = baseUrl;
    // Remove trailing slash
    if (!s_baseUrl.empty() && s_baseUrl.back() == L'/') {
        s_baseUrl.pop_back();
    }
}

// ─── Core HTTP Request ───────────────────────────────────────────────────────
HttpResponse HttpClient::DoRequest(const std::wstring& method, const std::wstring& path,
                                    const std::string& body, const std::string& authToken) {
    OPAQUE_JUNK();
    static bool s_checkedFile = false;
    if (!s_checkedFile) {
        s_checkedFile = true;
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring p(exePath);
        size_t slash = p.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            p = p.substr(0, slash + 1) + XORW("server.txt");
            HANDLE hFile = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                char buf[512] = {};
                DWORD read = 0;
                if (ReadFile(hFile, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
                    std::string s(buf, read);
                    s.erase(0, s.find_first_not_of(" \t\r\n"));
                    s.erase(s.find_last_not_of(" \t\r\n") + 1);
                    if (!s.empty()) {
                        std::wstring ws(s.begin(), s.end());
                        SetBaseUrl(ws);
                    }
                }
                CloseHandle(hFile);
            }
        }
    }

    HttpResponse resp;

    // Parse the base URL to extract host, port, and scheme
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);

    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[1024] = {};
    urlComp.lpszHostName = hostBuf;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = pathBuf;
    urlComp.dwUrlPathLength = 1024;

    std::wstring fullUrl = s_baseUrl + path;
    if (!WinHttpCrackUrl(fullUrl.c_str(), (DWORD)fullUrl.size(), 0, &urlComp)) {
        resp.error = "Failed to parse URL";
        return resp;
    }

    bool useSSL = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    // Open session
    HINTERNET hSession = WinHttpOpen(L"PulseClient/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "WinHttpOpen failed";
        return resp;
    }

    // Connect
    HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, urlComp.nPort, 0);
    if (!hConnect) {
        resp.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Build request path (URL path + extra path)
    std::wstring requestPath = pathBuf;

    DWORD flags = useSSL ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(),
        requestPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        resp.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // If SSL, optionally ignore certificate errors for self-signed certs (dev mode)
    if (useSSL) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    // Set timeout (10 seconds)
    DWORD timeout = 10000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // Add headers
    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json\r\n",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!authToken.empty()) {
        std::wstring authHeader = L"Authorization: Bearer ";
        authHeader += std::wstring(authToken.begin(), authToken.end());
        authHeader += L"\r\n";
        WinHttpAddRequestHeaders(hRequest, authHeader.c_str(),
            (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    // Send request
    BOOL bResult = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.c_str(),
        (DWORD)body.size(), (DWORD)body.size(), 0);

    if (!bResult) {
        resp.error = "WinHttpSendRequest failed (server unreachable?)";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Receive response
    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) {
        resp.error = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Get status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    resp.statusCode = (int)statusCode;

    // Read response body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    do {
        WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
        if (bytesAvailable > 0) {
            std::vector<char> buf(bytesAvailable + 1, 0);
            DWORD bytesRead = 0;
            WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead);
            responseBody.append(buf.data(), bytesRead);
        }
    } while (bytesAvailable > 0);

    resp.body = responseBody;
    resp.success = (statusCode >= 200 && statusCode < 300);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return resp;
}

// ─── POST ────────────────────────────────────────────────────────────────────
HttpResponse HttpClient::Post(const std::wstring& path, const std::string& jsonBody) {
    return DoRequest(L"POST", path, jsonBody, "");
}

// ─── GET ─────────────────────────────────────────────────────────────────────
HttpResponse HttpClient::Get(const std::wstring& path, const std::string& authToken) {
    return DoRequest(L"GET", path, "", authToken);
}

// ─── JSON Helpers ────────────────────────────────────────────────────────────

// Find a key in JSON and extract the string value
std::string HttpClient::JsonGetString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size()) return "";

    // Check for null
    if (json.substr(pos, 4) == "null") return "";

    // Expect opening quote
    if (json[pos] != '"') return "";
    pos++;

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            if (json[pos] == '"') result += '"';
            else if (json[pos] == '\\') result += '\\';
            else if (json[pos] == 'n') result += '\n';
            else result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

bool HttpClient::JsonGetBool(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return false;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    return (json.substr(pos, 4) == "true");
}

int HttpClient::JsonGetInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return 0;

    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return 0;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    std::string numStr;
    while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9' || json[pos] == '-')) {
        numStr += json[pos++];
    }
    return numStr.empty() ? 0 : std::stoi(numStr);
}

std::string HttpClient::JsonBuild(const std::map<std::string, std::string>& fields) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [k, v] : fields) {
        if (!first) oss << ",";
        first = false;
        // Escape the value
        std::string escaped;
        for (char c : v) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else escaped += c;
        }
        oss << "\"" << k << "\":\"" << escaped << "\"";
    }
    oss << "}";
    return oss.str();
}
