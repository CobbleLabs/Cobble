#include "auth_system.h"
#include "http_client.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace
{
    bool g_authenticated = false;
    bool g_remember_me = true;
    std::string g_currentKey = "";
    std::string g_licenseType = "";
    std::string g_expiresAt = "";
    std::string g_statusMessage = "";

    const std::string SECRET_XOR_KEY = "PULSE_ANTI_LEAK_SECRET_KEY_2026";

    std::string xor_cipher(const std::string& input)
    {
        std::string output = input;
        for (size_t i = 0; i < output.size(); ++i)
        {
            output[i] ^= SECRET_XOR_KEY[i % SECRET_XOR_KEY.size()];
        }
        return output;
    }

    std::string get_session_path()
    {
        char appdata[MAX_PATH] = {};
        SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
        return std::string(appdata) + "\\.pulse_session.dat";
    }

    void save_session(const std::string& key)
    {
        std::string enc = xor_cipher(key);
        std::ofstream f(get_session_path(), std::ios::binary);
        if (f) f.write(enc.c_str(), enc.size());
    }

    std::string load_session()
    {
        std::ifstream f(get_session_path(), std::ios::binary);
        if (!f) return "";
        std::string enc((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return xor_cipher(enc);
    }

    void delete_session()
    {
        DeleteFileA(get_session_path().c_str());
    }
}

namespace auth
{
    std::string get_system_hwid()
    {
        DWORD serialNum = 0;
        GetVolumeInformationW(L"C:\\", nullptr, 0, &serialNum, nullptr, nullptr, nullptr, 0);

        HKEY hKey;
        wchar_t machineGuid[256] = {};
        DWORD bufSize = sizeof(machineGuid);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS)
        {
            RegQueryValueExW(hKey, L"MachineGuid", nullptr, nullptr, reinterpret_cast<LPBYTE>(machineGuid), &bufSize);
            RegCloseKey(hKey);
        }

        std::wstring raw = std::wstring(machineGuid) + L"-" + std::to_wstring(serialNum);
        
        // Hash custom FNV-1a 64-bit para generar un string limpio en Hex
        uint64_t hash = 14695981039346656037ULL;
        for (wchar_t c : raw)
        {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }

        std::stringstream hexStream;
        hexStream << "HWID-" << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << hash;
        return hexStream.str();
    }

    bool copy_to_clipboard(const std::string& text)
    {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        HGLOBAL hG = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (!hG) { CloseClipboard(); return false; }
        memcpy(GlobalLock(hG), text.c_str(), text.size() + 1);
        GlobalUnlock(hG);
        SetClipboardData(CF_TEXT, hG);
        CloseClipboard();
        return true;
    }

    void initialize()
    {
        // Try to auto-login from saved session (remember me)
        std::string savedKey = load_session();
        if (!savedKey.empty())
        {
            g_remember_me = true;
            AuthStatus res = login(savedKey);
            if (res != AuthStatus::Success)
            {
                // Saved session invalid — clear it
                delete_session();
                g_statusMessage = ""; // Don't show error on auto-login failure
            }
        }
    }

    bool is_authenticated()
    {
        return g_authenticated;
    }

    AuthStatus login(const std::string& inputKey)
    {
        std::string key = inputKey;
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);

        if (key.empty()) return AuthStatus::InvalidFormat;

        std::string hwid = get_system_hwid();

        // Build JSON body: { "key": "...", "hwid": "..." }
        std::map<std::string, std::string> fields;
        fields["key"] = key;
        fields["hwid"] = hwid;
        std::string jsonBody = HttpClient::JsonBuild(fields);

        // POST to backend
        HttpResponse resp = HttpClient::Post(L"/api/auth/login", jsonBody);

        if (!resp.success && resp.error.size() > 0)
        {
            // Network error — couldn't reach server
            g_statusMessage = "Cannot connect to license server.";
            return AuthStatus::ServerError;
        }

        // Parse response
        bool serverSuccess = HttpClient::JsonGetBool(resp.body, "success");
        std::string message = HttpClient::JsonGetString(resp.body, "message");

        if (serverSuccess)
        {
            g_authenticated = true;
            g_currentKey = key;
            g_licenseType = HttpClient::JsonGetString(resp.body, "license_type");
            g_expiresAt = HttpClient::JsonGetString(resp.body, "expires_at");
            g_statusMessage = message;

            // Save session for remember me
            if (g_remember_me)
            {
                save_session(key);
            }

            return AuthStatus::Success;
        }
        else
        {
            g_statusMessage = message;

            // Map HTTP status codes to AuthStatus
            if (resp.statusCode == 401)
            {
                if (message.find("already been used") != std::string::npos)
                    return AuthStatus::HwidMismatch;
                return AuthStatus::KeyNotFound;
            }
            else if (resp.statusCode == 403)
            {
                if (message.find("banned") != std::string::npos || message.find("Banned") != std::string::npos)
                    return AuthStatus::Banned;
                if (message.find("expired") != std::string::npos || message.find("Expired") != std::string::npos)
                    return AuthStatus::Expired;
                if (message.find("HWID") != std::string::npos || message.find("mismatch") != std::string::npos)
                    return AuthStatus::HwidMismatch;
                return AuthStatus::Banned;
            }

            return AuthStatus::KeyNotFound;
        }
    }

    void logout()
    {
        g_authenticated = false;
        g_currentKey = "";
        g_licenseType = "";
        g_expiresAt = "";
        g_statusMessage = "";
        delete_session();
    }

    bool get_remember_me()
    {
        return g_remember_me;
    }

    void set_remember_me(bool value)
    {
        g_remember_me = value;
        if (!value)
        {
            delete_session();
        }
    }

    std::string get_current_key()
    {
        return g_currentKey;
    }

    std::string get_license_type()
    {
        return g_licenseType;
    }

    std::string get_expires_at()
    {
        return g_expiresAt;
    }

    std::string get_status_message()
    {
        return g_statusMessage;
    }

    std::string status_to_string(AuthStatus status)
    {
        switch (status)
        {
        case AuthStatus::Success: return "License activated!";
        case AuthStatus::KeyNotFound: return "Invalid license key.";
        case AuthStatus::HwidMismatch: return "ACCESS DENIED! This key is bound to another PC.";
        case AuthStatus::Expired: return "License expired. Contact admin.";
        case AuthStatus::Banned: return "This account has been banned.";
        case AuthStatus::InvalidFormat: return "Enter a valid license key.";
        case AuthStatus::ServerError: return "Cannot connect to license server.";
        }
        return "Unknown error.";
    }
}
