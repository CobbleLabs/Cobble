#pragma once
#include <windows.h>
#include <string>
#include <map>

// ─── HTTP Client ──────────────────────────────────────────────────────────────
// Native WinHTTP client for REST API communication.
// No external dependencies — uses Windows WinHTTP API.

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    bool success = false;
    std::string error;
};

class HttpClient {
public:
    // Set the base URL for all requests (e.g., "http://localhost:3000")
    static void SetBaseUrl(const std::wstring& baseUrl);

    // POST request with JSON body, returns response
    static HttpResponse Post(const std::wstring& path, const std::string& jsonBody);

    // GET request with optional auth header
    static HttpResponse Get(const std::wstring& path, const std::string& authToken = "");

    // ─── Simple JSON helpers (no external library) ───────────────────────────
    // Extract a string value from a JSON object: {"key": "value"}
    static std::string JsonGetString(const std::string& json, const std::string& key);

    // Extract a bool value from a JSON object: {"key": true}
    static bool JsonGetBool(const std::string& json, const std::string& key);

    // Extract an int value from a JSON object: {"key": 123}
    static int JsonGetInt(const std::string& json, const std::string& key);

    // Build a simple JSON object from key-value pairs
    static std::string JsonBuild(const std::map<std::string, std::string>& fields);

private:
    static std::wstring s_baseUrl;
    static HttpResponse DoRequest(const std::wstring& method, const std::wstring& path,
                                   const std::string& body, const std::string& authToken);
};
