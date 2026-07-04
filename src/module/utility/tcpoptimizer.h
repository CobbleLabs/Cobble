#pragma once

#include "../module.h"
#include <string>
#include <vector>

struct RegistryBackup {
    std::wstring keyPath;
    std::wstring valueName;
    bool existed = false;
    DWORD dwValue = 0;
    std::wstring strValue;
    DWORD valueKind = 0; // REG_NONE
};

class TcpOptimizer : public Module {
public:
    TcpOptimizer();

    void onEnable() override;
    void onDisable() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

private:
    // Settings
    bool smartPackets = true;
    float hitReg = 50.0f;
    float kbReduction = 50.0f;
    int connectionType = 0; // 0=Ethernet, 1=Wi-Fi

    // State
    bool optimized = false;
    std::vector<RegistryBackup> regBackups;
    std::wstring adapterName;
    std::wstring adapterId;
    std::wstring originalRscState;
    DWORD originalMtu = 1500;
    std::wstring originalTxBuffers;
    std::wstring originalRxBuffers;

    // Helpers
    bool detectAdapter();
    void applyOptimizations();
    void restoreAll();

    static HKEY GetRootKey(const std::wstring& path, std::wstring& subKey);
    static RegistryBackup backupRegValue(const std::wstring& keyPath, const std::wstring& valueName);
    static bool writeRegDword(const std::wstring& keyPath, const std::wstring& valueName, DWORD value);
    static bool writeRegString(const std::wstring& keyPath, const std::wstring& valueName, const std::wstring& value);
    static bool deleteRegValue(const std::wstring& keyPath, const std::wstring& valueName);
    static bool deleteRegSubKey(const std::wstring& parentPath, const std::wstring& subKey);
    static void restoreBackup(const RegistryBackup& backup);

    static void runHidden(const std::wstring& exe, const std::wstring& args, DWORD waitMs = 2000);
    static void runPS(const std::wstring& cmd);
    static std::wstring runPSOutput(const std::wstring& cmd);
    static std::wstring escapePS(const std::wstring& s);
    static void restartAdapter(const std::wstring& name);
    static std::wstring queryRscState();
    static bool setRscState(bool enable);
};

extern TcpOptimizer* tcpOptimizer;
