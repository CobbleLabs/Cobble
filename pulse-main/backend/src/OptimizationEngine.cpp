#include "OptimizationEngine.h"
#include "NetshUtility.h"
#include "GameConfigManager.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <algorithm>
#include <sstream>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

OptimizationEngine& OptimizationEngine::Instance() {
    static OptimizationEngine inst;
    return inst;
}

std::wstring OptimizationEngine::EscapePS(const std::wstring& s) {
    std::wstring result = s;
    size_t pos = 0;
    while ((pos = result.find(L"'", pos)) != std::wstring::npos) {
        result.replace(pos, 1, L"''");
        pos += 2;
    }
    return result;
}

NetworkAdapterInfo OptimizationEngine::GetActiveInterface() {
    NetworkAdapterInfo result;
    ULONG bufLen = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, nullptr, &bufLen);
    std::vector<BYTE> buf(bufLen);
    auto pAddrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());

    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, pAddrs, &bufLen) != NO_ERROR)
        return result;

    struct CandidateAdapter {
        std::wstring name;
        std::wstring id;
        bool isWireless;
        ULONGLONG traffic;
        bool hasGateway;
        bool hasUnicast;
    };
    std::vector<CandidateAdapter> candidates;

    for (auto p = pAddrs; p; p = p->Next) {
        if (p->OperStatus != IfOperStatusUp) continue;
        if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (p->IfType == 131) continue; // Tunnel

        std::wstring name = p->FriendlyName ? p->FriendlyName : L"";
        std::wstring desc = p->Description  ? p->Description  : L"";
        std::wstring nameLow = name, descLow = desc;
        std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(), ::towlower);
        std::transform(descLow.begin(), descLow.end(), descLow.begin(), ::towlower);

        auto contains = [](const std::wstring& s, const wchar_t* sub) {
            return s.find(sub) != std::wstring::npos;
        };
        if (contains(nameLow, L"virtual") || contains(descLow, L"virtual") ||
            contains(nameLow, L"vpn")     || contains(descLow, L"vpn")     ||
            contains(nameLow, L"tap")     || contains(descLow, L"tap")     ||
            contains(nameLow, L"tun")     || contains(descLow, L"tun")     ||
            contains(nameLow, L"vbox")    || contains(descLow, L"vbox")    ||
            contains(nameLow, L"vmware")  || contains(descLow, L"vmware")  ||
            contains(nameLow, L"hyper-v") || contains(descLow, L"hyper-v") ||
            contains(nameLow, L"pseudo")  || contains(descLow, L"pseudo")  ||
            contains(nameLow, L"host-only") || contains(descLow, L"host-only") ||
            contains(descLow, L"microsoft loopback"))
            continue;

        // Check gateway
        bool hasGateway = false;
        for (auto gw = p->FirstGatewayAddress; gw; gw = gw->Next) {
            SOCKADDR_IN* sa = (SOCKADDR_IN*)gw->Address.lpSockaddr;
            if (sa->sin_addr.s_addr != 0) { hasGateway = true; break; }
        }

        // Check unicast IPv4
        bool hasUnicast = false;
        for (auto ua = p->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family == AF_INET) { hasUnicast = true; break; }
        }

        // Get traffic stats
        MIB_IF_ROW2 row = {};
        row.InterfaceIndex = p->IfIndex;
        ULONGLONG traffic = 0;
        if (GetIfEntry2(&row) == NO_ERROR)
            traffic = row.InOctets + row.OutOctets;

        // GUID string -> adapter ID for registry
        GUID guid = p->NetworkGuid;
        wchar_t guidStr[64] = {};
        StringFromGUID2(guid, guidStr, 64);

        CandidateAdapter cand;
        cand.name = name;
        cand.id = guidStr;
        cand.isWireless = (p->IfType == IF_TYPE_IEEE80211);
        cand.traffic = traffic;
        cand.hasGateway = hasGateway;
        cand.hasUnicast = hasUnicast;
        candidates.push_back(cand);
    }

    if (candidates.empty()) return result;

    // Sort: prefer has-gateway + has-unicast, then by traffic
    auto best = std::max_element(candidates.begin(), candidates.end(), [](const CandidateAdapter& a, const CandidateAdapter& b) {
        int scoreA = (a.hasGateway ? 4 : 0) + (a.hasUnicast ? 2 : 0);
        int scoreB = (b.hasGateway ? 4 : 0) + (b.hasUnicast ? 2 : 0);
        if (scoreA != scoreB) return scoreA < scoreB;
        return a.traffic < b.traffic;
    });

    result.name      = best->name;
    result.id        = best->id;
    result.isWireless = best->isWireless;
    result.isValid   = true;
    return result;
}

void OptimizationEngine::RunCommandLine(const std::wstring& exe, const std::wstring& args) {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = exe + L" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void OptimizationEngine::RunPowerShellCommand(const std::wstring& cmd) {
    std::wstring args = L"-NoProfile -WindowStyle Hidden -Command \"" + cmd + L"\"";
    RunCommandLine(L"powershell.exe", args);
}

std::wstring OptimizationEngine::RunPowerShellCommandWithOutput(const std::wstring& cmd) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return L"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"powershell.exe -NoProfile -WindowStyle Hidden -Command \"" + cmd + L"\"";
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite);
        return L"";
    }
    CloseHandle(hWrite);

    std::string output;
    char chunk[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, chunk, sizeof(chunk) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        chunk[bytesRead] = '\0';
        output += chunk;
    }
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);

    // Trim trailing whitespace
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' '))
        output.pop_back();

    int wlen = MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, nullptr, 0);
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, result.data(), wlen);
    return result;
}

void OptimizationEngine::RestartAdapter(const std::wstring& adapterName) {
    std::wstring esc = EscapePS(adapterName);
    std::wstring cmd = L"Disable-NetAdapter -Name '" + esc +
                       L"' -Confirm:$false; Start-Sleep -Milliseconds 500; Enable-NetAdapter -Name '" + esc +
                       L"' -Confirm:$false";
    RunPowerShellCommand(cmd);
}

bool OptimizationEngine::ApplyOptimizations(bool smartPackets, double hitReg, double kbReduction,
                                             const std::wstring& connectionType,
                                             std::function<void(const std::wstring&)> statusCb) {
    if (m_isOptimized) { statusCb(L"Already optimized."); return true; }

    statusCb(L"Detecting active network adapter...");
    NetworkAdapterInfo adapter = GetActiveInterface();
    if (!adapter.isValid) {
        statusCb(L"Error: Active network adapter not found.");
        return false;
    }
    m_optimizedAdapterName = adapter.name;
    statusCb(L"Active adapter: " + adapter.name);

    m_registryBackups.clear();

    std::wstring tcpIfPath   = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + adapter.id;
    std::wstring mmProfilePath = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";

    m_registryBackups.push_back(RegistryManager::CreateBackup(tcpIfPath,     L"TcpAckFrequency"));
    m_registryBackups.push_back(RegistryManager::CreateBackup(tcpIfPath,     L"TCPNoDelay"));
    m_registryBackups.push_back(RegistryManager::CreateBackup(tcpIfPath,     L"TcpDelAckTicks"));
    m_registryBackups.push_back(RegistryManager::CreateBackup(mmProfilePath, L"NetworkThrottlingIndex"));
    m_registryBackups.push_back(RegistryManager::CreateBackup(mmProfilePath, L"SystemResponsiveness"));

    m_originalRscState = NetshUtility::QueryGlobalRscState();

    // Backup MTU
    m_originalMtu = 1500;
    // (Reading actual MTU via GetAdaptersAddresses is complex; we'll save default)

    // Backup adapter buffers
    std::wstring esc = EscapePS(adapter.name);
    m_originalTxBuffers = RunPowerShellCommandWithOutput(
        L"Get-NetAdapterAdvancedProperty -Name '" + esc +
        L"' -DisplayName 'Transmit Buffers' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty DisplayValue");
    m_originalRxBuffers = RunPowerShellCommandWithOutput(
        L"Get-NetAdapterAdvancedProperty -Name '" + esc +
        L"' -DisplayName 'Receive Buffers' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty DisplayValue");

    std::wstring qosPath = L"HKLM\\Software\\Policies\\Microsoft\\Windows\\QoS\\PulseJavawQoS";

    try {
        // Hit Registration tweaks
        if (hitReg >= 1.0) {
            statusCb(L"Applying Hit Reg level 1 (TCPNoDelay, TcpAckFrequency)...");
            RegistryManager::WriteValueDword(tcpIfPath, L"TcpAckFrequency", 1);
            RegistryManager::WriteValueDword(tcpIfPath, L"TCPNoDelay",      1);
            if (hitReg >= 61.0) {
                statusCb(L"Applying Hit Reg level 2 (MTU=1492, disabling LSO)...");
                RunCommandLine(L"netsh", L"interface ipv4 set subinterface \"" + adapter.name + L"\" mtu=1492 store=active");
                RunPowerShellCommand(L"Disable-NetAdapterLso -Name '" + esc + L"' -IPv4 -Confirm:$false");
            }
        }

        // Knockback Reduction tweaks
        if (kbReduction >= 1.0) {
            statusCb(L"Applying Knockback Reduction level 1 (TcpDelAckTicks=0)...");
            RegistryManager::WriteValueDword(tcpIfPath, L"TcpDelAckTicks", 0);
            if (kbReduction >= 61.0) {
                statusCb(L"Applying Knockback Reduction level 2 (Buffers, Dynamic Tick)...");
                if (!m_originalTxBuffers.empty())
                    RunPowerShellCommand(L"Set-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName 'Transmit Buffers' -DisplayValue '256' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
                if (!m_originalRxBuffers.empty())
                    RunPowerShellCommand(L"Set-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName 'Receive Buffers' -DisplayValue '512' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
                RunCommandLine(L"bcdedit.exe", L"/set disabledynamictick yes");
            }
        }

        // Smart Packets (RSC)
        if (smartPackets) {
            statusCb(L"Disabling RSC (Smart Packets)...");
            NetshUtility::SetRscState(false);
        }

        // TCP Auto-tuning
        std::wstring tuning = (connectionType == L"Wi-Fi") ? L"highlyrestricted" : L"normal";
        RunCommandLine(L"netsh", L"interface tcp set global autotuninglevel=" + tuning);

        // QoS policy for javaw.exe (Minecraft)
        statusCb(L"Configuring network packet priority for Minecraft...");
        RegistryManager::WriteValueString(qosPath, L"Version",                L"1.0");
        RegistryManager::WriteValueString(qosPath, L"Application Name",       L"javaw.exe");
        RegistryManager::WriteValueString(qosPath, L"DSCP Value",             L"46");
        RegistryManager::WriteValueString(qosPath, L"Protocol",               L"*");
        RegistryManager::WriteValueString(qosPath, L"Local Port",             L"*");
        RegistryManager::WriteValueString(qosPath, L"Local IP",               L"*");
        RegistryManager::WriteValueString(qosPath, L"Local IP Prefix Length", L"*");
        RegistryManager::WriteValueString(qosPath, L"Remote Port",            L"*");
        RegistryManager::WriteValueString(qosPath, L"Remote IP",              L"*");
        RegistryManager::WriteValueString(qosPath, L"Remote IP Prefix Length",L"*");

        // Minecraft options.txt
        statusCb(L"Applying Minecraft options.txt performance tweaks...");
        GameConfigManager::ApplyOptimizations();

        // Multimedia scheduler
        RegistryManager::WriteValueDword(mmProfilePath, L"NetworkThrottlingIndex", (DWORD)-1);
        RegistryManager::WriteValueDword(mmProfilePath, L"SystemResponsiveness",  0);

        // Restart adapter
        statusCb(L"Refreshing network card silently to apply adjustments...");
        RestartAdapter(adapter.name);
        statusCb(L"Network adapter restarted successfully.");

        m_isOptimized = true;
        statusCb(L"Pulse Engine: Active & Optimized.");
        return true;
    } catch (...) {
        statusCb(L"Optimization failed.");
        RestoreAll(statusCb);
        return false;
    }
}

void OptimizationEngine::RestoreAll(std::function<void(const std::wstring&)> statusCb) {
    if (!m_isOptimized && m_registryBackups.empty()) {
        statusCb(L"Pulse Engine: Restored.");
        return;
    }
    statusCb(L"Restoring original network settings...");

    for (auto& backup : m_registryBackups)
        RegistryManager::RestoreBackup(backup);
    m_registryBackups.clear();

    // Remove QoS key
    RegistryManager::DeleteSubKeyTree(L"HKLM\\Software\\Policies\\Microsoft\\Windows\\QoS", L"PulseJavawQoS");

    // Restore RSC
    NetshUtility::SetRscState(m_originalRscState == L"enabled");

    if (!m_optimizedAdapterName.empty()) {
        std::wstring esc = EscapePS(m_optimizedAdapterName);
        RunCommandLine(L"netsh", L"interface ipv4 set subinterface \"" + m_optimizedAdapterName +
                       L"\" mtu=" + std::to_wstring(m_originalMtu) + L" store=active");
        RunPowerShellCommand(L"Enable-NetAdapterLso -Name '" + esc + L"' -IPv4 -Confirm:$false");

        if (!m_originalTxBuffers.empty())
            RunPowerShellCommand(L"Set-NetAdapterAdvancedProperty -Name '" + esc +
                                 L"' -DisplayName 'Transmit Buffers' -DisplayValue '" + m_originalTxBuffers +
                                 L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
        if (!m_originalRxBuffers.empty())
            RunPowerShellCommand(L"Set-NetAdapterAdvancedProperty -Name '" + esc +
                                 L"' -DisplayName 'Receive Buffers' -DisplayValue '" + m_originalRxBuffers +
                                 L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
    }

    RunCommandLine(L"netsh",     L"interface tcp set global autotuninglevel=normal");
    RunCommandLine(L"bcdedit.exe", L"/set disabledynamictick no");

    statusCb(L"Restoring Lunar & Minecraft option files...");
    GameConfigManager::RestoreOriginals();

    if (!m_optimizedAdapterName.empty()) {
        statusCb(L"Restarting adapter to restore defaults...");
        RestartAdapter(m_optimizedAdapterName);
    }

    m_isOptimized = false;
    statusCb(L"Pulse Engine: Cleaned and Restored.");
}
