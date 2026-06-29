#include "RegistryManager.h"
#include <iostream>

HKEY RegistryManager::GetRootKey(const std::wstring& path, std::wstring& subKeyPath) {
    if (path.substr(0, 5) == L"HKLM\\") {
        subKeyPath = path.substr(5);
        return HKEY_LOCAL_MACHINE;
    }
    if (path.substr(0, 19) == L"HKEY_LOCAL_MACHINE\\") {
        subKeyPath = path.substr(19);
        return HKEY_LOCAL_MACHINE;
    }
    if (path.substr(0, 5) == L"HKCU\\") {
        subKeyPath = path.substr(5);
        return HKEY_CURRENT_USER;
    }
    if (path.substr(0, 18) == L"HKEY_CURRENT_USER\\") {
        subKeyPath = path.substr(18);
        return HKEY_CURRENT_USER;
    }
    subKeyPath = path;
    return HKEY_LOCAL_MACHINE;
}

RegistryValueBackup RegistryManager::CreateBackup(const std::wstring& fullKeyPath, const std::wstring& valueName) {
    RegistryValueBackup backup;
    backup.subKeyPath = fullKeyPath;
    backup.valueName = valueName;
    backup.existed = false;

    std::wstring subKeyPath;
    HKEY root = GetRootKey(fullKeyPath, subKeyPath);

    HKEY hKey = nullptr;
    LONG res = RegOpenKeyExW(root, subKeyPath.c_str(), 0, KEY_READ, &hKey);
    if (res == ERROR_SUCCESS && hKey) {
        DWORD type = 0;
        DWORD size = sizeof(DWORD);
        DWORD dwVal = 0;

        // Try DWORD first
        if (RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, (LPBYTE)&dwVal, &size) == ERROR_SUCCESS) {
            if (type == REG_DWORD) {
                backup.existed = true;
                backup.dwValue = dwVal;
                backup.valueKind = REG_DWORD;
            } else if (type == REG_SZ) {
                // Re-query as string
                size = 0;
                RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, nullptr, &size);
                std::wstring strVal(size / sizeof(wchar_t), L'\0');
                if (RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, (LPBYTE)strVal.data(), &size) == ERROR_SUCCESS) {
                    backup.existed = true;
                    backup.strValue = strVal;
                    backup.valueKind = REG_SZ;
                }
            }
        }
        RegCloseKey(hKey);
    }
    return backup;
}

bool RegistryManager::WriteValueDword(const std::wstring& fullKeyPath, const std::wstring& valueName, DWORD value) {
    std::wstring subKeyPath;
    HKEY root = GetRootKey(fullKeyPath, subKeyPath);

    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(root, subKeyPath.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (res == ERROR_SUCCESS && hKey) {
        LONG r = RegSetValueExW(hKey, valueName.c_str(), 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}

bool RegistryManager::WriteValueString(const std::wstring& fullKeyPath, const std::wstring& valueName, const std::wstring& value) {
    std::wstring subKeyPath;
    HKEY root = GetRootKey(fullKeyPath, subKeyPath);

    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(root, subKeyPath.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (res == ERROR_SUCCESS && hKey) {
        DWORD size = (DWORD)((value.size() + 1) * sizeof(wchar_t));
        LONG r = RegSetValueExW(hKey, valueName.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(), size);
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}

bool RegistryManager::DeleteValue(const std::wstring& fullKeyPath, const std::wstring& valueName) {
    std::wstring subKeyPath;
    HKEY root = GetRootKey(fullKeyPath, subKeyPath);

    HKEY hKey = nullptr;
    LONG res = RegOpenKeyExW(root, subKeyPath.c_str(), 0, KEY_WRITE, &hKey);
    if (res == ERROR_SUCCESS && hKey) {
        LONG r = RegDeleteValueW(hKey, valueName.c_str());
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}

bool RegistryManager::RestoreBackup(const RegistryValueBackup& backup) {
    if (backup.existed) {
        if (backup.valueKind == REG_DWORD)
            return WriteValueDword(backup.subKeyPath, backup.valueName, backup.dwValue);
        else if (backup.valueKind == REG_SZ)
            return WriteValueString(backup.subKeyPath, backup.valueName, backup.strValue);
    } else {
        return DeleteValue(backup.subKeyPath, backup.valueName);
    }
    return false;
}

bool RegistryManager::DeleteSubKeyTree(const std::wstring& parentPath, const std::wstring& subKey) {
    std::wstring subKeyPath;
    HKEY root = GetRootKey(parentPath, subKeyPath);

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKeyPath.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        LONG r = RegDeleteTreeW(hKey, subKey.c_str());
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}
