#include "GameConfigManager.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>

std::vector<GameConfigBackup> GameConfigManager::s_backups;

static std::wstring GetSpecialFolder(int csidl) {
    wchar_t path[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, csidl, nullptr, 0, path);
    return path;
}

static bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool DirExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string ReadFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static bool WriteFile(const std::wstring& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(content.c_str(), content.size());
    return true;
}

static void FindRecursively(const std::wstring& baseDir, int currentDepth, int maxDepth, std::vector<std::wstring>& outList) {
    if (currentDepth > maxDepth || !DirExists(baseDir)) return;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((baseDir + L"\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (_wcsicmp(fd.cFileName, L"assets") != 0 && _wcsicmp(fd.cFileName, L"libraries") != 0 && _wcsicmp(fd.cFileName, L"versions") != 0 && _wcsicmp(fd.cFileName, L"logs") != 0) {
                    FindRecursively(baseDir + L"\\" + fd.cFileName, currentDepth + 1, maxDepth, outList);
                }
            } else {
                if ((_wcsnicmp(fd.cFileName, L"options", 7) == 0 && wcsstr(fd.cFileName, L".txt") != nullptr) ||
                    _wcsicmp(fd.cFileName, L"launcher_profiles.json") == 0 || _wcsicmp(fd.cFileName, L"launcher_profiles_microsoft_store.json") == 0) {
                    outList.push_back(baseDir + L"\\" + fd.cFileName);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

std::vector<std::wstring> GameConfigManager::FindOptionFiles() {
    std::vector<std::wstring> list;
    std::wstring appdata = GetSpecialFolder(CSIDL_APPDATA);
    std::wstring localappdata = GetSpecialFolder(CSIDL_LOCAL_APPDATA);
    std::wstring profile = GetSpecialFolder(CSIDL_PROFILE);

    const std::wstring* bases[] = { &appdata, &localappdata, &profile };
    const wchar_t* targetFolders[] = {
        L"\\.minecraft", L"\\.lunarclient", L"\\cheatbreaker", L"\\.badlion", L"\\.feather",
        L"\\curseforge", L"\\modrinthapp", L"\\prismlauncher", L"\\multimc"
    };

    for (const auto* b : bases) {
        if (b->empty()) continue;
        for (const wchar_t* tf : targetFolders) {
            FindRecursively(*b + tf, 0, 3, list);
        }
    }
    return list;
}

std::string GameConfigManager::RegexReplace(const std::string& text, const std::string& pattern, const std::string& replacement) {
    try {
        std::regex re(pattern);
        return std::regex_replace(text, re, replacement);
    } catch (...) {
        return text;
    }
}

void GameConfigManager::ApplyOptimizations() {
    s_backups.clear();
    auto files = FindOptionFiles();
    for (auto& filePath : files) {
        try {
            std::string content = ReadFile(filePath);
            if (content.empty()) continue;
            s_backups.push_back({ filePath, content });

            std::string modified = content;
            if (filePath.find(L".json") != std::wstring::npos) {
                std::string g1gc = "-XX:+UseG1GC -XX:MaxGCPauseMillis=20 -XX:SurvivorRatio=32 -XX:+DisableExplicitGC -XX:TargetSurvivorRatio=90 -XX:G1NewSizePercent=35 -XX:G1MaxNewSizePercent=60 -XX:G1MixedGCLiveThresholdPercent=50 -XX:+AlwaysPreTouch ";
                if (modified.find("javaArgs") != std::string::npos) {
                    modified = RegexReplace(modified, "\"javaArgs\"\\s*:\\s*\"([^\"]*)\"", "\"javaArgs\": \"" + g1gc + "$1\"");
                } else if (modified.find("lastVersionId") != std::string::npos) {
                    modified = RegexReplace(modified, "\"lastVersionId\"", "\"javaArgs\": \"" + g1gc + "\",\n      \"lastVersionId\"");
                }
            } else {
                const char* kvPairs[][2] = {
                    { "enableVsync", "false" },
                    { "maxFps", "0" },
                    { "fboEnable", "true" },
                    { "renderClouds", "false" },
                    { "ao", "0" },
                    { "mipmapLevels", "0" },
                    { "particles", "2" },
                    { "graphics", "0" }
                };
                for (const auto& kv : kvPairs) {
                    std::string key = kv[0];
                    std::string val = kv[1];
                    if (modified.find(key + ":") != std::string::npos) {
                        modified = RegexReplace(modified, key + ":\\s*\\w+", key + ":" + val);
                    } else {
                        modified += "\n" + key + ":" + val;
                    }
                }
            }
            WriteFile(filePath, modified);
        } catch (...) {}
    }
}

void GameConfigManager::RestoreOriginals() {
    for (auto& backup : s_backups) {
        if (FileExists(backup.filePath))
            WriteFile(backup.filePath, backup.originalContent);
    }
    s_backups.clear();
}
