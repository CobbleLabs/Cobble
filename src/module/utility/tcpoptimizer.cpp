#include "tcpoptimizer.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include <shellapi.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

TcpOptimizer* tcpOptimizer = nullptr;

TcpOptimizer::TcpOptimizer() : Module("TCP Optimizer") {
    keybind = 0;
}

// ======================== REGISTRY HELPERS ========================

HKEY TcpOptimizer::GetRootKey(const std::wstring& path, std::wstring& subKey) {
    if (path.substr(0, 5) == L"HKLM\\") { subKey = path.substr(5); return HKEY_LOCAL_MACHINE; }
    if (path.substr(0, 19) == L"HKEY_LOCAL_MACHINE\\") { subKey = path.substr(19); return HKEY_LOCAL_MACHINE; }
    if (path.substr(0, 5) == L"HKCU\\") { subKey = path.substr(5); return HKEY_CURRENT_USER; }
    subKey = path;
    return HKEY_LOCAL_MACHINE;
}

RegistryBackup TcpOptimizer::backupRegValue(const std::wstring& keyPath, const std::wstring& valueName) {
    RegistryBackup b;
    b.keyPath = keyPath;
    b.valueName = valueName;
    b.existed = false;

    std::wstring sub;
    HKEY root = GetRootKey(keyPath, sub);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS && hKey) {
        DWORD type = 0, size = sizeof(DWORD);
        DWORD dw = 0;
        if (RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, (LPBYTE)&dw, &size) == ERROR_SUCCESS) {
            if (type == REG_DWORD) {
                b.existed = true; b.dwValue = dw; b.valueKind = REG_DWORD;
            } else if (type == REG_SZ) {
                size = 0;
                RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, nullptr, &size);
                std::wstring str(size / sizeof(wchar_t), L'\0');
                if (RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type, (LPBYTE)str.data(), &size) == ERROR_SUCCESS) {
                    b.existed = true; b.strValue = str; b.valueKind = REG_SZ;
                }
            }
        }
        RegCloseKey(hKey);
    }
    return b;
}

bool TcpOptimizer::writeRegDword(const std::wstring& keyPath, const std::wstring& valueName, DWORD value) {
    std::wstring sub;
    HKEY root = GetRootKey(keyPath, sub);
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS && hKey) {
        LONG r = RegSetValueExW(hKey, valueName.c_str(), 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}

bool TcpOptimizer::writeRegString(const std::wstring& keyPath, const std::wstring& valueName, const std::wstring& value) {
    std::wstring sub;
    HKEY root = GetRootKey(keyPath, sub);
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS && hKey) {
        DWORD sz = (DWORD)((value.size() + 1) * sizeof(wchar_t));
        LONG r = RegSetValueExW(hKey, valueName.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(), sz);
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}

bool TcpOptimizer::deleteRegValue(const std::wstring& keyPath, const std::wstring& valueName) {
    std::wstring sub;
    HKEY root = GetRootKey(keyPath, sub);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS && hKey) {
        LONG r = RegDeleteValueW(hKey, valueName.c_str());
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}

bool TcpOptimizer::deleteRegSubKey(const std::wstring& parentPath, const std::wstring& subKey) {
    std::wstring sub;
    HKEY root = GetRootKey(parentPath, sub);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        LONG r = RegDeleteTreeW(hKey, subKey.c_str());
        RegCloseKey(hKey);
        return r == ERROR_SUCCESS;
    }
    return false;
}

void TcpOptimizer::restoreBackup(const RegistryBackup& backup) {
    if (backup.existed) {
        if (backup.valueKind == REG_DWORD)
            writeRegDword(backup.keyPath, backup.valueName, backup.dwValue);
        else if (backup.valueKind == REG_SZ)
            writeRegString(backup.keyPath, backup.valueName, backup.strValue);
    } else {
        deleteRegValue(backup.keyPath, backup.valueName);
    }
}

// ======================== PROCESS HELPERS ========================

void TcpOptimizer::runHidden(const std::wstring& exe, const std::wstring& args, DWORD waitMs) {
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = exe + L" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, waitMs);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

void TcpOptimizer::runPS(const std::wstring& cmd) {
    runHidden(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"" + cmd + L"\"");
}

std::wstring TcpOptimizer::runPSOutput(const std::wstring& cmd) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return L"";

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite; si.hStdError = hWrite; si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"powershell.exe -NoProfile -WindowStyle Hidden -Command \"" + cmd + L"\"";
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite); return L"";
    }
    CloseHandle(hWrite);

    std::string output;
    char chunk[4096]; DWORD bytesRead;
    while (ReadFile(hRead, chunk, sizeof(chunk) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        chunk[bytesRead] = '\0'; output += chunk;
    }
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);

    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' '))
        output.pop_back();

    int wlen = MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, nullptr, 0);
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, result.data(), wlen);
    return result;
}

std::wstring TcpOptimizer::escapePS(const std::wstring& s) {
    std::wstring r = s; size_t pos = 0;
    while ((pos = r.find(L"'", pos)) != std::wstring::npos) { r.replace(pos, 1, L"''"); pos += 2; }
    return r;
}

void TcpOptimizer::restartAdapter(const std::wstring& name) {
    std::wstring esc = escapePS(name);
    runPS(L"Disable-NetAdapter -Name '" + esc + L"' -Confirm:$false; Start-Sleep -Milliseconds 500; Enable-NetAdapter -Name '" + esc + L"' -Confirm:$false");
}

// ======================== RSC (NETSH) ========================

std::wstring TcpOptimizer::queryRscState() {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return L"enabled";

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite; si.hStdError = hWrite; si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"netsh.exe interface tcp show global";
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite); return L"enabled";
    }
    CloseHandle(hWrite);

    std::string output;
    char chunk[4096]; DWORD bytesRead;
    while (ReadFile(hRead, chunk, sizeof(chunk) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        chunk[bytesRead] = '\0'; output += chunk;
    }
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);

    int wlen = MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, nullptr, 0);
    std::wstring wout(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, wout.data(), wlen);

    std::wistringstream ss(wout);
    std::wstring line;
    while (std::getline(ss, line)) {
        std::wstring lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        bool hasRsc = lower.find(L"fusi") != std::wstring::npos ||
                      lower.find(L"coalesc") != std::wstring::npos ||
                      (lower.find(L"segment") != std::wstring::npos &&
                       (lower.find(L"recep") != std::wstring::npos || lower.find(L"recei") != std::wstring::npos));
        if (hasRsc) {
            if (lower.find(L"disabled") != std::wstring::npos || lower.find(L"deshabilitado") != std::wstring::npos)
                return L"disabled";
            if (lower.find(L"enabled") != std::wstring::npos || lower.find(L"habilitado") != std::wstring::npos)
                return L"enabled";
        }
    }
    return L"enabled";
}

bool TcpOptimizer::setRscState(bool enable) {
    std::wstring state = enable ? L"enabled" : L"disabled";
    runHidden(L"netsh.exe", L"interface tcp set global rsc=" + state, 3000);
    return true;
}

// ======================== ADAPTER DETECTION ========================

bool TcpOptimizer::detectAdapter() {
    ULONG bufLen = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, nullptr, &bufLen);
    std::vector<BYTE> buf(bufLen);
    auto pAddrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, pAddrs, &bufLen) != NO_ERROR)
        return false;

    struct Candidate { std::wstring name, id; bool wireless; ULONGLONG traffic; bool gw, uni; };
    std::vector<Candidate> candidates;

    for (auto p = pAddrs; p; p = p->Next) {
        if (p->OperStatus != IfOperStatusUp) continue;
        if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK || p->IfType == 131) continue;

        std::wstring nm = p->FriendlyName ? p->FriendlyName : L"";
        std::wstring desc = p->Description ? p->Description : L"";
        std::wstring nmL = nm, descL = desc;
        std::transform(nmL.begin(), nmL.end(), nmL.begin(), ::towlower);
        std::transform(descL.begin(), descL.end(), descL.begin(), ::towlower);

        auto has = [](const std::wstring& s, const wchar_t* sub) { return s.find(sub) != std::wstring::npos; };
        if (has(nmL,L"virtual")||has(descL,L"virtual")||has(nmL,L"vpn")||has(descL,L"vpn")||
            has(nmL,L"tap")||has(descL,L"tap")||has(nmL,L"tun")||has(descL,L"tun")||
            has(nmL,L"vbox")||has(descL,L"vbox")||has(nmL,L"vmware")||has(descL,L"vmware")||
            has(nmL,L"hyper-v")||has(descL,L"hyper-v")||has(nmL,L"pseudo")||has(descL,L"pseudo")||
            has(nmL,L"host-only")||has(descL,L"host-only")||has(descL,L"microsoft loopback"))
            continue;

        bool hasGw = false;
        for (auto gw = p->FirstGatewayAddress; gw; gw = gw->Next) {
            SOCKADDR_IN* sa = (SOCKADDR_IN*)gw->Address.lpSockaddr;
            if (sa->sin_addr.s_addr != 0) { hasGw = true; break; }
        }
        bool hasUni = false;
        for (auto ua = p->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family == AF_INET) { hasUni = true; break; }
        }

        MIB_IF_ROW2 row = {}; row.InterfaceIndex = p->IfIndex;
        ULONGLONG tr = 0;
        if (GetIfEntry2(&row) == NO_ERROR) tr = row.InOctets + row.OutOctets;

        wchar_t guidStr[64] = {};
        StringFromGUID2(p->NetworkGuid, guidStr, 64);

        candidates.push_back({nm, guidStr, p->IfType == IF_TYPE_IEEE80211, tr, hasGw, hasUni});
    }

    if (candidates.empty()) return false;

    auto best = std::max_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        int sa = (a.gw?4:0)+(a.uni?2:0), sb = (b.gw?4:0)+(b.uni?2:0);
        if (sa != sb) return sa < sb;
        return a.traffic < b.traffic;
    });

    adapterName = best->name;
    adapterId = best->id;
    return true;
}

// ======================== APPLY / RESTORE ========================

static bool runCmdsAsAdmin(const std::vector<std::wstring>& cmds) {
    if (cmds.empty()) return false;
    std::wstring psCmd;
    for (const auto& cmd : cmds) {
        psCmd += cmd + L"; ";
    }
    std::wstring args = L"-NoProfile -NonInteractive -WindowStyle Hidden -Command \"" + psCmd + L"\"";
    
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    
    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess != nullptr) {
            WaitForSingleObject(sei.hProcess, 15000);
            CloseHandle(sei.hProcess);
        }
        return true;
    }
    return false;
}

static std::wstring regSetDwordCmd(const std::wstring& path, const std::wstring& name, DWORD value) {
    std::wstring psPath = path;
    if (psPath.substr(0, 5) == L"HKLM\\") {
        psPath = L"HKLM:\\" + psPath.substr(5);
    } else if (psPath.substr(0, 19) == L"HKEY_LOCAL_MACHINE\\") {
        psPath = L"HKLM:\\" + psPath.substr(19);
    }
    std::replace(psPath.begin(), psPath.end(), L'\\', L'/');
    return L"Set-ItemProperty -Path '" + psPath + L"' -Name '" + name + L"' -Value " + std::to_wstring(value) + L" -Type DWord -Force";
}

static std::wstring regSetStringCmd(const std::wstring& path, const std::wstring& name, const std::wstring& value) {
    std::wstring psPath = path;
    if (psPath.substr(0, 5) == L"HKLM\\") {
        psPath = L"HKLM:\\" + psPath.substr(5);
    } else if (psPath.substr(0, 19) == L"HKEY_LOCAL_MACHINE\\") {
        psPath = L"HKLM:\\" + psPath.substr(19);
    }
    std::replace(psPath.begin(), psPath.end(), L'\\', L'/');
    return L"Set-ItemProperty -Path '" + psPath + L"' -Name '" + name + L"' -Value '" + value + L"' -Type String -Force";
}

static std::wstring regDeleteValueCmd(const std::wstring& path, const std::wstring& name) {
    std::wstring psPath = path;
    if (psPath.substr(0, 5) == L"HKLM\\") {
        psPath = L"HKLM:\\" + psPath.substr(5);
    } else if (psPath.substr(0, 19) == L"HKEY_LOCAL_MACHINE\\") {
        psPath = L"HKLM:\\" + psPath.substr(19);
    }
    std::replace(psPath.begin(), psPath.end(), L'\\', L'/');
    return L"Remove-ItemProperty -Path '" + psPath + L"' -Name '" + name + L"' -ErrorAction SilentlyContinue";
}

static std::wstring regDeleteKeyCmd(const std::wstring& parentPath, const std::wstring& subKey) {
    std::wstring psPath = parentPath;
    if (psPath.substr(0, 5) == L"HKLM\\") {
        psPath = L"HKLM:\\" + psPath.substr(5);
    } else if (psPath.substr(0, 19) == L"HKEY_LOCAL_MACHINE\\") {
        psPath = L"HKLM:\\" + psPath.substr(19);
    }
    std::replace(psPath.begin(), psPath.end(), L'\\', L'/');
    return L"Remove-Item -Path '" + psPath + L"/" + subKey + L"' -Recurse -Force -ErrorAction SilentlyContinue";
}

void TcpOptimizer::applyOptimizations() {
    if (optimized) return;
    if (!detectAdapter()) return;

    regBackups.clear();

    std::wstring tcpIfPath = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + adapterId;
    std::wstring mmPath = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
    std::wstring tcpParamsPath = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";

    regBackups.push_back(backupRegValue(tcpIfPath, L"TcpAckFrequency"));
    regBackups.push_back(backupRegValue(tcpIfPath, L"TCPNoDelay"));
    regBackups.push_back(backupRegValue(tcpIfPath, L"TcpDelAckTicks"));
    regBackups.push_back(backupRegValue(mmPath, L"NetworkThrottlingIndex"));
    regBackups.push_back(backupRegValue(mmPath, L"SystemResponsiveness"));
    // Global TCP/IP stack tuning (latency-oriented, all restored on disable).
    regBackups.push_back(backupRegValue(tcpParamsPath, L"DefaultTTL"));
    regBackups.push_back(backupRegValue(tcpParamsPath, L"Tcp1323Opts"));
    regBackups.push_back(backupRegValue(tcpParamsPath, L"MaxUserPort"));
    regBackups.push_back(backupRegValue(tcpParamsPath, L"TcpTimedWaitDelay"));

    originalRscState = queryRscState();
    originalMtu = 1500;

    std::wstring esc = escapePS(adapterName);
    originalTxBuffers = runPSOutput(
        L"Get-NetAdapterAdvancedProperty -Name '" + esc +
        L"' -DisplayName 'Transmit Buffers' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty DisplayValue");
    originalRxBuffers = runPSOutput(
        L"Get-NetAdapterAdvancedProperty -Name '" + esc +
        L"' -DisplayName 'Receive Buffers' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty DisplayValue");

    std::vector<std::wstring> cmds;

    if (hitReg >= 1.0f) {
        cmds.push_back(regSetDwordCmd(tcpIfPath, L"TcpAckFrequency", 1));
        cmds.push_back(regSetDwordCmd(tcpIfPath, L"TCPNoDelay", 1));
        if (hitReg >= 61.0f) {
            cmds.push_back(L"netsh interface ipv4 set subinterface \"" + adapterName + L"\" mtu=1492 store=active");
            cmds.push_back(L"Disable-NetAdapterLso -Name '" + esc + L"' -IPv4 -Confirm:$false");
        }
    }

    if (kbReduction >= 1.0f) {
        cmds.push_back(regSetDwordCmd(tcpIfPath, L"TcpDelAckTicks", 0));
        if (kbReduction >= 61.0f) {
            if (!originalTxBuffers.empty()) {
                cmds.push_back(L"Set-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName 'Transmit Buffers' -DisplayValue '256' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
            }
            if (!originalRxBuffers.empty()) {
                cmds.push_back(L"Set-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName 'Receive Buffers' -DisplayValue '512' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
            }
            cmds.push_back(L"bcdedit.exe /set disabledynamictick yes");
        }
    }

    if (smartPackets) {
        cmds.push_back(L"netsh interface tcp set global rsc=disabled");
    }

    // ---- Global TCP/IP stack tuning (always applied) -------------------------
    // Lower fixed TTL, window-scaling on with timestamps off (Tcp1323Opts=1),
    // wider ephemeral port range and fast TIME_WAIT reuse for quick reconnects.
    cmds.push_back(regSetDwordCmd(tcpParamsPath, L"DefaultTTL", 64));
    cmds.push_back(regSetDwordCmd(tcpParamsPath, L"Tcp1323Opts", 1));
    cmds.push_back(regSetDwordCmd(tcpParamsPath, L"MaxUserPort", 65534));
    cmds.push_back(regSetDwordCmd(tcpParamsPath, L"TcpTimedWaitDelay", 30));

    // Strip per-packet overhead / spurious retransmit resilience that adds jitter,
    // and shrink the initial retransmit timeout so lost hits recover faster.
    cmds.push_back(L"netsh interface tcp set global timestamps=disabled");
    cmds.push_back(L"netsh interface tcp set global nonsackrttresiliency=disabled");
    cmds.push_back(L"netsh interface tcp set global initialRto=2000");
    cmds.push_back(L"netsh interface tcp set global rss=enabled");

    // ---- Aggressive NIC latency tuning (high Hit Reg tier) -------------------
    // Disables the driver features that batch/delay packets or sleep the link,
    // trading a little CPU for lower, more consistent latency (fewer whiffs).
    if (hitReg >= 61.0f) {
        auto nicProp = [&](const wchar_t* disp, const wchar_t* val) {
            cmds.push_back(L"Set-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName '" +
                           std::wstring(disp) + L"' -DisplayValue '" + val +
                           L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
        };
        nicProp(L"Interrupt Moderation", L"Disabled");
        nicProp(L"Flow Control", L"Disabled");
        nicProp(L"Energy-Efficient Ethernet", L"Disabled");
        nicProp(L"Green Ethernet", L"Disabled");
        nicProp(L"Gigabit Lite", L"Disabled");
        nicProp(L"Power Saving Mode", L"Disabled");
        nicProp(L"Ultra Low Power Mode", L"Disabled");
        cmds.push_back(L"Disable-NetAdapterRsc -Name '" + esc + L"' -IPv4 -IPv6 -NoRestart -ErrorAction SilentlyContinue");
    }

    std::wstring tuning = (connectionType == 1) ? L"highlyrestricted" : L"normal";
    cmds.push_back(L"netsh interface tcp set global autotuninglevel=" + tuning);

    std::wstring qosPath = L"HKLM\\Software\\Policies\\Microsoft\\Windows\\QoS\\CobbleTcpQoS";
    std::wstring qosPsPath = L"HKLM:\\Software\\Policies\\Microsoft\\Windows\\QoS\\CobbleTcpQoS";
    std::replace(qosPsPath.begin(), qosPsPath.end(), L'\\', L'/');
    cmds.push_back(L"New-Item -Path '" + qosPsPath + L"' -Force -ErrorAction SilentlyContinue | Out-Null");

    cmds.push_back(regSetStringCmd(qosPath, L"Version", L"1.0"));
    cmds.push_back(regSetStringCmd(qosPath, L"Application Name", L"javaw.exe"));
    cmds.push_back(regSetStringCmd(qosPath, L"DSCP Value", L"46"));
    cmds.push_back(regSetStringCmd(qosPath, L"Protocol", L"*"));
    cmds.push_back(regSetStringCmd(qosPath, L"Local Port", L"*"));
    cmds.push_back(regSetStringCmd(qosPath, L"Local IP", L"*"));
    cmds.push_back(regSetStringCmd(qosPath, L"Local IP Prefix Length", L"*"));
    cmds.push_back(regSetStringCmd(qosPath, L"Remote Port", L"*"));
    cmds.push_back(regSetStringCmd(qosPath, L"Remote IP", L"*"));
    cmds.push_back(regSetStringCmd(qosPath, L"Remote IP Prefix Length", L"*"));

    cmds.push_back(regSetDwordCmd(mmPath, L"NetworkThrottlingIndex", (DWORD)-1));
    cmds.push_back(regSetDwordCmd(mmPath, L"SystemResponsiveness", 0));

    cmds.push_back(L"Restart-NetAdapter -Name '" + esc + L"' -Confirm:$false");

    if (runCmdsAsAdmin(cmds)) {
        optimized = true;
    }
}

void TcpOptimizer::restoreAll() {
    if (!optimized && regBackups.empty()) return;

    std::vector<std::wstring> cmds;

    for (auto& b : regBackups) {
        if (b.existed) {
            if (b.valueKind == REG_DWORD) {
                cmds.push_back(regSetDwordCmd(b.keyPath, b.valueName, b.dwValue));
            } else if (b.valueKind == REG_SZ) {
                cmds.push_back(regSetStringCmd(b.keyPath, b.valueName, b.strValue));
            }
        } else {
            cmds.push_back(regDeleteValueCmd(b.keyPath, b.valueName));
        }
    }
    regBackups.clear();

    cmds.push_back(regDeleteKeyCmd(L"HKLM\\Software\\Policies\\Microsoft\\Windows\\QoS", L"CobbleTcpQoS"));

    std::wstring rscState = (originalRscState == L"enabled") ? L"enabled" : L"disabled";
    cmds.push_back(L"netsh interface tcp set global rsc=" + rscState);

    if (!adapterName.empty()) {
        std::wstring esc = escapePS(adapterName);
        cmds.push_back(L"netsh interface ipv4 set subinterface \"" + adapterName + L"\" mtu=" + std::to_wstring(originalMtu) + L" store=active");
        cmds.push_back(L"Enable-NetAdapterLso -Name '" + esc + L"' -IPv4 -Confirm:$false");

        if (!originalTxBuffers.empty()) {
            cmds.push_back(L"Set-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName 'Transmit Buffers' -DisplayValue '" + originalTxBuffers + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
        }
        if (!originalRxBuffers.empty()) {
            cmds.push_back(L"Set-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName 'Receive Buffers' -DisplayValue '" + originalRxBuffers + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue");
        }

        // Reset the aggressive NIC latency properties back to driver defaults.
        const wchar_t* resetProps[] = {L"Interrupt Moderation", L"Flow Control", L"Energy-Efficient Ethernet",
                                       L"Green Ethernet", L"Gigabit Lite", L"Power Saving Mode", L"Ultra Low Power Mode"};
        for (auto* dp : resetProps) {
            cmds.push_back(L"Reset-NetAdapterAdvancedProperty -Name '" + esc + L"' -DisplayName '" +
                           std::wstring(dp) + L"' -NoRestart -ErrorAction SilentlyContinue");
        }
        cmds.push_back(L"Enable-NetAdapterRsc -Name '" + esc + L"' -IPv4 -IPv6 -NoRestart -ErrorAction SilentlyContinue");
    }

    cmds.push_back(L"netsh interface tcp set global autotuninglevel=normal");
    cmds.push_back(L"netsh interface tcp set global nonsackrttresiliency=enabled");
    cmds.push_back(L"netsh interface tcp set global initialRto=3000");
    cmds.push_back(L"bcdedit.exe /set disabledynamictick no");

    if (!adapterName.empty()) {
        std::wstring esc = escapePS(adapterName);
        cmds.push_back(L"Restart-NetAdapter -Name '" + esc + L"' -Confirm:$false");
    }

    if (runCmdsAsAdmin(cmds)) {
        optimized = false;
    }
}

#include <thread>

// ======================== MODULE CALLBACKS ========================

void TcpOptimizer::onEnable() {
    std::thread([this]() {
        applyOptimizations();
    }).detach();
}

void TcpOptimizer::onDisable() {
    std::thread([this]() {
        restoreAll();
    }).detach();
}

void TcpOptimizer::onRenderMenu() {
    static const char* connTypes[] = {"Ethernet", "Wi-Fi"};

    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ui::Combo("Connection", &connectionType, connTypes, 2);
            ui::Checkbox("Smart Packets", &smartPackets);
        },
        "Tweaks",
        [this]() {
            ui::SliderFloat("Hit Reg", &hitReg, 0.0f, 100.0f, "%.0f%%");
            ui::SliderFloat("KB Reduction", &kbReduction, 0.0f, 100.0f, "%.0f%%");
            if (optimized) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Status: Optimized");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: Not Active");
            }
        });
}

void TcpOptimizer::saveSettings(json::Value& node) const {
    node["smartPackets"] = smartPackets;
    node["hitReg"] = (double)hitReg;
    node["kbReduction"] = (double)kbReduction;
    node["connectionType"] = connectionType;
}

void TcpOptimizer::loadSettings(const json::Value& node) {
    smartPackets = node["smartPackets"].as_bool(smartPackets);
    hitReg = (float)node["hitReg"].as_double((double)hitReg);
    kbReduction = (float)node["kbReduction"].as_double((double)kbReduction);
    connectionType = node["connectionType"].as_int(connectionType);
}
