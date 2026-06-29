#pragma once
#include <windows.h>
#include <string>
#include <variant>

struct RegistryValueBackup {
    std::wstring subKeyPath;
    std::wstring valueName;
    bool existed = false;
    DWORD dwValue = 0;
    std::wstring strValue;
    DWORD valueKind = REG_NONE;
};

class RegistryManager {
public:
    static HKEY GetRootKey(const std::wstring& path, std::wstring& subKeyPath);
    static RegistryValueBackup CreateBackup(const std::wstring& fullKeyPath, const std::wstring& valueName);
    static bool WriteValueDword(const std::wstring& fullKeyPath, const std::wstring& valueName, DWORD value);
    static bool WriteValueString(const std::wstring& fullKeyPath, const std::wstring& valueName, const std::wstring& value);
    static bool DeleteValue(const std::wstring& fullKeyPath, const std::wstring& valueName);
    static bool RestoreBackup(const RegistryValueBackup& backup);
    static bool DeleteSubKeyTree(const std::wstring& parentPath, const std::wstring& subKey);
};
