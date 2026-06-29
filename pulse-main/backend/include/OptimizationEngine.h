#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include <functional>
#include "RegistryManager.h"

struct NetworkAdapterInfo {
    std::wstring name;
    std::wstring id;          // GUID as string
    bool isWireless = false;
    bool isValid    = false;
};

class OptimizationEngine {
public:
    static OptimizationEngine& Instance();

    NetworkAdapterInfo GetActiveInterface();
    bool ApplyOptimizations(bool smartPackets, double hitReg, double kbReduction,
                            const std::wstring& connectionType,
                            std::function<void(const std::wstring&)> statusCb);
    void RestoreAll(std::function<void(const std::wstring&)> statusCb);
    bool IsOptimized() const { return m_isOptimized; }

private:
    OptimizationEngine() = default;
    OptimizationEngine(const OptimizationEngine&) = delete;
    OptimizationEngine& operator=(const OptimizationEngine&) = delete;

    static void RunCommandLine(const std::wstring& exe, const std::wstring& args);
    static void RunPowerShellCommand(const std::wstring& cmd);
    static std::wstring RunPowerShellCommandWithOutput(const std::wstring& cmd);
    static void RestartAdapter(const std::wstring& adapterName);
    static std::wstring EscapePS(const std::wstring& s);

    std::vector<RegistryValueBackup> m_registryBackups;
    std::wstring m_originalRscState = L"enabled";
    DWORD        m_originalMtu      = 1500;
    std::wstring m_originalTxBuffers;
    std::wstring m_originalRxBuffers;
    bool         m_isOptimized      = false;
    std::wstring m_optimizedAdapterName;
};
