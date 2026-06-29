#pragma once
#include <string>
#include <vector>

struct GameConfigBackup {
    std::wstring filePath;
    std::string  originalContent;
};

class GameConfigManager {
public:
    static void ApplyOptimizations();
    static void RestoreOriginals();

private:
    static std::vector<GameConfigBackup> s_backups;
    static std::vector<std::wstring> FindOptionFiles();
    static void AddOptionsIfExist(const std::wstring& dirPath, std::vector<std::wstring>& filesList);
    static std::string RegexReplace(const std::string& text, const std::string& pattern, const std::string& replacement);
};
