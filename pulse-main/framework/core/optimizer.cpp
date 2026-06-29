#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "optimizer.h"

#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <thread>

#pragma comment(lib, "iphlpapi.lib")

namespace fs = std::filesystem;

namespace optimizer
{
    State g_state;

    // Forward declarations
    void restore_defaults_thread();

    // Helper conversion functions
    std::wstring ToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    std::string ToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    // Helper command executor
    bool RunCommandLine(const std::wstring& cmd, const std::wstring& args)
    {
        std::wstring commandLine = L"\"" + cmd + L"\" " + args;
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        BOOL success = CreateProcessW(
            NULL, 
            &commandLine[0], 
            NULL, NULL, FALSE, 
            CREATE_NO_WINDOW, 
            NULL, NULL, &si, &pi
        );
        
        if (success)
        {
            WaitForSingleObject(pi.hProcess, 4000); // 4 seconds timeout
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return exitCode == 0;
        }
        return false;
    }

    std::wstring RunPowerShellWithOutput(const std::wstring& psCommand)
    {
        std::wstring commandLine = L"powershell.exe -NoProfile -WindowStyle Hidden -Command \"[Console]::OutputEncoding = [System.Text.Encoding]::GetEncoding(850); " + psCommand + L"\"";
        
        HANDLE hReadPipe = NULL;
        HANDLE hWritePipe = NULL;
        SECURITY_ATTRIBUTES sa = { sizeof(sa) };
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;
        
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
            return L"";
            
        if (!SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0))
        {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            return L"";
        }
        
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        
        BOOL success = CreateProcessW(
            NULL,
            &commandLine[0],
            NULL, NULL, TRUE,
            CREATE_NO_WINDOW,
            NULL, NULL, &si, &pi
        );
        
        CloseHandle(hWritePipe);
        
        if (!success)
        {
            CloseHandle(hReadPipe);
            return L"";
        }
        
        std::string outputBytes;
        char buffer[512];
        DWORD bytesRead = 0;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
        {
            outputBytes.append(buffer, bytesRead);
        }
        
        WaitForSingleObject(pi.hProcess, 4000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        
        while (!outputBytes.empty() && (outputBytes.back() == '\r' || outputBytes.back() == '\n' || outputBytes.back() == ' '))
            outputBytes.pop_back();
            
        int size_needed = MultiByteToWideChar(CP_OEMCP, 0, outputBytes.c_str(), (int)outputBytes.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_OEMCP, 0, outputBytes.c_str(), (int)outputBytes.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    std::wstring QueryGlobalRscState()
    {
        std::wstring output = RunPowerShellWithOutput(L"netsh interface tcp show global");
        std::wstring rsc = L"enabled";
        std::wstring lower_output = output;
        std::transform(lower_output.begin(), lower_output.end(), lower_output.begin(), ::towlower);
        
        std::vector<std::wstring> lines;
        size_t pos = 0;
        while ((pos = lower_output.find(L"\n")) != std::wstring::npos)
        {
            lines.push_back(lower_output.substr(0, pos));
            lower_output.erase(0, pos + 1);
        }
        lines.push_back(lower_output);
        
        for (const auto& line : lines)
        {
            if (line.find(L"fusi") != std::wstring::npos ||
                line.find(L"coalesc") != std::wstring::npos ||
                (line.find(L"segment") != std::wstring::npos && (line.find(L"recep") != std::wstring::npos || line.find(L"recei") != std::wstring::npos)))
            {
                if (line.find(L"disabled") != std::wstring::npos || line.find(L"deshabilitado") != std::wstring::npos)
                {
                    return L"disabled";
                }
                if (line.find(L"enabled") != std::wstring::npos || line.find(L"habilitado") != std::wstring::npos)
                {
                    return L"enabled";
                }
            }
        }
        return rsc;
    }

    std::wstring QueryAdapterProperty(const std::wstring& adapterName, const std::wstring& propName)
    {
        std::wstring name_escaped = adapterName;
        size_t pos = 0;
        while ((pos = name_escaped.find(L"'", pos)) != std::wstring::npos)
        {
            name_escaped.replace(pos, 1, L"''");
            pos += 2;
        }
        
        std::wstring cmd = L"Get-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName '" + propName + L"' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty DisplayValue";
        return RunPowerShellWithOutput(cmd);
    }

    // Registry helper functions
    HKEY ParseRootKey(const std::wstring& fullPath, std::wstring& subkey)
    {
        if (fullPath.rfind(L"HKLM\\", 0) == 0)
        {
            subkey = fullPath.substr(5);
            return HKEY_LOCAL_MACHINE;
        }
        if (fullPath.rfind(L"HKEY_LOCAL_MACHINE\\", 0) == 0)
        {
            subkey = fullPath.substr(19);
            return HKEY_LOCAL_MACHINE;
        }
        if (fullPath.rfind(L"HKCU\\", 0) == 0)
        {
            subkey = fullPath.substr(5);
            return HKEY_CURRENT_USER;
        }
        if (fullPath.rfind(L"HKEY_CURRENT_USER\\", 0) == 0)
        {
            subkey = fullPath.substr(18);
            return HKEY_CURRENT_USER;
        }
        subkey = fullPath;
        return HKEY_LOCAL_MACHINE;
    }

    bool WriteRegistryDWord(const std::wstring& fullKeyPath, const std::wstring& valueName, DWORD value)
    {
        std::wstring subkey;
        HKEY root = ParseRootKey(fullKeyPath, subkey);
        HKEY hKey = NULL;
        
        LSTATUS status = RegCreateKeyExW(
            root, 
            subkey.c_str(), 
            0, NULL, 
            REG_OPTION_NON_VOLATILE, 
            KEY_WRITE | KEY_WOW64_64KEY, 
            NULL, &hKey, NULL
        );
        
        if (status == ERROR_SUCCESS)
        {
            status = RegSetValueExW(hKey, valueName.c_str(), 0, REG_DWORD, (BYTE*)&value, sizeof(value));
            RegCloseKey(hKey);
            return status == ERROR_SUCCESS;
        }
        return false;
    }

    bool WriteRegistryString(const std::wstring& fullKeyPath, const std::wstring& valueName, const std::wstring& value)
    {
        std::wstring subkey;
        HKEY root = ParseRootKey(fullKeyPath, subkey);
        HKEY hKey = NULL;
        
        LSTATUS status = RegCreateKeyExW(
            root, 
            subkey.c_str(), 
            0, NULL, 
            REG_OPTION_NON_VOLATILE, 
            KEY_WRITE | KEY_WOW64_64KEY, 
            NULL, &hKey, NULL
        );
        
        if (status == ERROR_SUCCESS)
        {
            status = RegSetValueExW(
                hKey, 
                valueName.c_str(), 
                0, REG_SZ, 
                (BYTE*)value.c_str(), 
                (DWORD)(value.size() + 1) * sizeof(wchar_t)
            );
            RegCloseKey(hKey);
            return status == ERROR_SUCCESS;
        }
        return false;
    }

    bool WriteRegistryBinary(const std::wstring& fullKeyPath, const std::wstring& valueName, const BYTE* data, DWORD size)
    {
        std::wstring subkey;
        HKEY root = ParseRootKey(fullKeyPath, subkey);
        HKEY hKey = NULL;
        
        LSTATUS status = RegCreateKeyExW(
            root, 
            subkey.c_str(), 
            0, NULL, 
            REG_OPTION_NON_VOLATILE, 
            KEY_WRITE | KEY_WOW64_64KEY, 
            NULL, &hKey, NULL
        );
        
        if (status == ERROR_SUCCESS)
        {
            status = RegSetValueExW(hKey, valueName.c_str(), 0, REG_BINARY, data, size);
            RegCloseKey(hKey);
            return status == ERROR_SUCCESS;
        }
        return false;
    }

    bool DeleteRegistryValue(const std::wstring& fullKeyPath, const std::wstring& valueName)
    {
        std::wstring subkey;
        HKEY root = ParseRootKey(fullKeyPath, subkey);
        HKEY hKey = NULL;
        
        LSTATUS status = RegOpenKeyExW(
            root, 
            subkey.c_str(), 
            0, 
            KEY_SET_VALUE | KEY_WOW64_64KEY, 
            &hKey
        );
        
        if (status == ERROR_SUCCESS)
        {
            status = RegDeleteValueW(hKey, valueName.c_str());
            RegCloseKey(hKey);
            return status == ERROR_SUCCESS;
        }
        return false;
    }

    RegistryValueBackup CreateRegistryBackup(const std::wstring& fullKeyPath, const std::wstring& valueName)
    {
        RegistryValueBackup backup;
        backup.keyPath = fullKeyPath;
        backup.valueName = valueName;
        backup.existed = false;
        
        std::wstring subkey;
        HKEY root = ParseRootKey(fullKeyPath, subkey);
        HKEY hKey = NULL;
        
        LSTATUS status = RegOpenKeyExW(
            root, 
            subkey.c_str(), 
            0, 
            KEY_QUERY_VALUE | KEY_WOW64_64KEY, 
            &hKey
        );
        
        if (status == ERROR_SUCCESS)
        {
            DWORD size = 0;
            DWORD type = 0;
            status = RegQueryValueExW(hKey, valueName.c_str(), NULL, &type, NULL, &size);
            if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA)
            {
                backup.existed = true;
                backup.type = type;
                backup.data.resize(size);
                status = RegQueryValueExW(hKey, valueName.c_str(), NULL, &type, backup.data.data(), &size);
            }
            RegCloseKey(hKey);
        }
        return backup;
    }

    void RestoreRegistryBackup(const RegistryValueBackup& backup)
    {
        if (backup.existed)
        {
            std::wstring subkey;
            HKEY root = ParseRootKey(backup.keyPath, subkey);
            HKEY hKey = NULL;
            
            LSTATUS status = RegCreateKeyExW(
                root, 
                subkey.c_str(), 
                0, NULL, 
                REG_OPTION_NON_VOLATILE, 
                KEY_WRITE | KEY_WOW64_64KEY, 
                NULL, &hKey, NULL
            );
            
            if (status == ERROR_SUCCESS)
            {
                RegSetValueExW(hKey, backup.valueName.c_str(), 0, backup.type, backup.data.data(), (DWORD)backup.data.size());
                RegCloseKey(hKey);
            }
        }
        else
        {
            DeleteRegistryValue(backup.keyPath, backup.valueName);
        }
    }

    // Connection type detection
    void detect_connection_type()
    {
        g_state.connection_type = 0; 
        g_state.is_wireless = false;
        g_state.active_adapter_id = L"";
        g_state.active_adapter_name = L"";
        g_state.status_text = "Detecting active network adapter...";
        
        ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
        ULONG family = AF_UNSPEC;
        ULONG bufferSize = 15000;
        std::vector<BYTE> buffer(bufferSize);
        PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)buffer.data();
        ULONG iterations = 0;
        DWORD dwRetVal = 0;
        
        do {
            dwRetVal = GetAdaptersAddresses(family, flags, NULL, pAddresses, &bufferSize);
            if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
                buffer.resize(bufferSize);
                pAddresses = (PIP_ADAPTER_ADDRESSES)buffer.data();
            } else {
                break;
            }
            iterations++;
        } while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (iterations < 3));
        
        if (dwRetVal != NO_ERROR) {
            g_state.status_text = "No active connection detected. Defaulting to Fiber.";
            return;
        }
        
        struct AdapterCandidate {
            PIP_ADAPTER_ADDRESSES addr;
            unsigned long long total_bytes;
        };
        std::vector<AdapterCandidate> candidates;
        
        for (PIP_ADAPTER_ADDRESSES curr = pAddresses; curr != NULL; curr = curr->Next)
        {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK || curr->IfType == IF_TYPE_TUNNEL)
                continue;
                
            if (curr->OperStatus != IfOperStatusUp)
                continue;
                
            std::wstring friendlyName = curr->FriendlyName ? curr->FriendlyName : L"";
            std::wstring description = curr->Description ? curr->Description : L"";
            
            std::wstring name_lower = friendlyName;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::towlower);
            std::wstring desc_lower = description;
            std::transform(desc_lower.begin(), desc_lower.end(), desc_lower.begin(), ::towlower);
            
            if (name_lower.find(L"virtual") != std::wstring::npos || desc_lower.find(L"virtual") != std::wstring::npos ||
                name_lower.find(L"vpn") != std::wstring::npos || desc_lower.find(L"vpn") != std::wstring::npos ||
                name_lower.find(L"tap") != std::wstring::npos || desc_lower.find(L"tap") != std::wstring::npos ||
                name_lower.find(L"tun") != std::wstring::npos || desc_lower.find(L"tun") != std::wstring::npos ||
                name_lower.find(L"vbox") != std::wstring::npos || desc_lower.find(L"vbox") != std::wstring::npos ||
                name_lower.find(L"vmware") != std::wstring::npos || desc_lower.find(L"vmware") != std::wstring::npos ||
                name_lower.find(L"hyper-v") != std::wstring::npos || desc_lower.find(L"hyper-v") != std::wstring::npos ||
                name_lower.find(L"pseudo") != std::wstring::npos || desc_lower.find(L"pseudo") != std::wstring::npos ||
                name_lower.find(L"host-only") != std::wstring::npos || desc_lower.find(L"host-only") != std::wstring::npos ||
                desc_lower.find(L"microsoft loopback") != std::wstring::npos)
            {
                continue;
            }
            
            if (curr->FirstGatewayAddress == NULL)
                continue;
                
            bool has_ipv4 = false;
            for (PIP_ADAPTER_UNICAST_ADDRESS ua = curr->FirstUnicastAddress; ua != NULL; ua = ua->Next)
            {
                if (ua->Address.lpSockaddr->sa_family == AF_INET)
                {
                    has_ipv4 = true;
                    break;
                }
            }
            if (!has_ipv4)
                continue;
                
            unsigned long long bytes = 0;
            MIB_IF_ROW2 row = {};
            row.InterfaceLuid = curr->Luid;
            if (GetIfEntry2(&row) == NO_ERROR)
            {
                bytes = row.InOctets + row.OutOctets;
            }
            
            candidates.push_back({ curr, bytes });
        }
        
        PIP_ADAPTER_ADDRESSES best = NULL;
        if (!candidates.empty())
        {
            std::sort(candidates.begin(), candidates.end(), [](const AdapterCandidate& a, const AdapterCandidate& b) {
                return a.total_bytes > b.total_bytes;
            });
            best = candidates[0].addr;
        }
        
        if (best)
        {
            std::string id_narrow = best->AdapterName;
            g_state.active_adapter_id = std::wstring(id_narrow.begin(), id_narrow.end());
            g_state.active_adapter_name = best->FriendlyName;
            g_state.is_wireless = (best->IfType == IF_TYPE_IEEE80211);
            g_state.connection_type = g_state.is_wireless ? 1 : 0; 
            
            MIB_IF_ROW2 row = {};
            row.InterfaceLuid = best->Luid;
            if (GetIfEntry2(&row) == NO_ERROR)
            {
                g_state.original_mtu = row.Mtu;
            }
            else
            {
                g_state.original_mtu = 1500;
            }
            
            std::string name_utf8 = ToUTF8(best->FriendlyName);
            g_state.status_text = "Detected active " + std::string(g_state.is_wireless ? "wireless" : "wired") + " connection: " + name_utf8;
        }
        else
        {
            g_state.status_text = "No active connection detected. Defaulting to Ethernet.";
        }
    }

    // Minecraft options finder and tweaks
    void FindOptionsRecursively(const fs::path& baseDir, int currentDepth, int maxDepth, std::vector<fs::path>& outFiles)
    {
        if (currentDepth > maxDepth || !fs::exists(baseDir) || !fs::is_directory(baseDir)) return;
        try {
            for (const auto& entry : fs::directory_iterator(baseDir)) {
                if (entry.is_regular_file()) {
                    std::wstring fname = entry.path().filename().wstring();
                    std::transform(fname.begin(), fname.end(), fname.begin(), ::towlower);
                    if ((fname.rfind(L"options", 0) == 0 && fname.find(L".txt") != std::wstring::npos) ||
                        fname == L"launcher_profiles.json" || fname == L"launcher_profiles_microsoft_store.json") {
                        outFiles.push_back(entry.path());
                    }
                } else if (entry.is_directory()) {
                    std::wstring dname = entry.path().filename().wstring();
                    if (dname != L"assets" && dname != L"libraries" && dname != L"versions" && dname != L"cache" && dname != L"logs") {
                        FindOptionsRecursively(entry.path(), currentDepth + 1, maxDepth, outFiles);
                    }
                }
            }
        } catch (...) {}
    }

    std::vector<fs::path> FindOptionFiles()
    {
        std::vector<fs::path> files;
        wchar_t* envBuf = nullptr;
        size_t len = 0;

        const wchar_t* envVars[] = { L"APPDATA", L"LOCALAPPDATA", L"USERPROFILE" };
        const wchar_t* targetFolders[] = {
            L".minecraft", L".lunarclient", L"cheatbreaker", L".badlion", L".feather",
            L"curseforge", L"modrinthapp", L"prismlauncher", L"multimc"
        };

        for (const wchar_t* ev : envVars) {
            if (_wdupenv_s(&envBuf, &len, ev) == 0 && envBuf != nullptr) {
                fs::path baseP = fs::path(envBuf);
                for (const wchar_t* tf : targetFolders) {
                    FindOptionsRecursively(baseP / tf, 0, 3, files);
                }
                free(envBuf);
                envBuf = nullptr;
            }
        }
        return files;
    }

    void ApplyGameConfigOptimizations()
    {
        g_state.config_backups.clear();
        auto files = FindOptionFiles();
        
        for (const auto& filePath : files)
        {
            try
            {
                std::ifstream in(filePath, std::ios::in | std::ios::binary);
                if (!in.is_open()) continue;
                
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                in.close();
                
                GameConfigBackup backup;
                backup.filePath = filePath.wstring();
                backup.originalContent = content;
                g_state.config_backups.push_back(backup);
                
                std::string modified = content;
                std::wstring fname = filePath.filename().wstring();
                std::transform(fname.begin(), fname.end(), fname.begin(), ::towlower);

                if (fname.find(L".json") != std::wstring::npos)
                {
                    std::string g1gc = "-XX:+UseG1GC -XX:MaxGCPauseMillis=20 -XX:SurvivorRatio=32 -XX:+DisableExplicitGC -XX:TargetSurvivorRatio=90 -XX:G1NewSizePercent=35 -XX:G1MaxNewSizePercent=60 -XX:G1MixedGCLiveThresholdPercent=50 -XX:+AlwaysPreTouch ";
                    if (modified.find("javaArgs") != std::string::npos) {
                        modified = std::regex_replace(modified, std::regex("\"javaArgs\"\\s*:\\s*\"([^\"]*)\""), "\"javaArgs\": \"" + g1gc + "$1\"");
                    } else if (modified.find("lastVersionId") != std::string::npos) {
                        modified = std::regex_replace(modified, std::regex("\"lastVersionId\""), "\"javaArgs\": \"" + g1gc + "\",\n      \"lastVersionId\"");
                    }
                }
                else
                {
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
                        if (modified.find(key + ":") == std::string::npos) {
                            modified += "\n" + key + ":" + val;
                        } else {
                            modified = std::regex_replace(modified, std::regex(key + ":\\s*\\w+"), key + ":" + val);
                        }
                    }
                }
                
                std::ofstream out(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (out.is_open()) {
                    out.write(modified.c_str(), modified.size());
                    out.close();
                }
            }
            catch (...) {}
        }
    }

    void RestoreGameConfigOriginals()
    {
        for (const auto& backup : g_state.config_backups)
        {
            try {
                std::ofstream out(backup.filePath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (out.is_open()) {
                    out.write(backup.originalContent.c_str(), backup.originalContent.size());
                    out.close();
                }
            } catch (...) {}
        }
        g_state.config_backups.clear();
    }

    // Optimization triggers
    void apply_tweaks_thread()
    {
        g_state.is_processing = true;
        g_state.status_text = "Starting extreme PvP optimization engine...";
        
        g_state.status_text = "Detecting active network adapter...";
        detect_connection_type();
        
        if (g_state.active_adapter_id.empty()) {
            g_state.status_text = "Error: Active network adapter not found.";
            g_state.is_processing = false;
            return;
        }
        
        std::wstring adapterId = g_state.active_adapter_id;
        std::wstring adapterName = g_state.active_adapter_name;
        
        g_state.original_rsc_state = QueryGlobalRscState();
        g_state.original_tx_buffers = QueryAdapterProperty(adapterName, L"Transmit Buffers");
        g_state.original_rx_buffers = QueryAdapterProperty(adapterName, L"Receive Buffers");
        g_state.original_flow_control = QueryAdapterProperty(adapterName, L"Flow Control");
        g_state.original_interrupt_mod = QueryAdapterProperty(adapterName, L"Interrupt Moderation");
        g_state.original_vlan_id = QueryAdapterProperty(adapterName, L"Priority & VLAN");
        
        g_state.registry_backups.clear();
        
        // Key paths
        std::wstring tcpGlobal = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
        std::wstring tcpIf = tcpGlobal + L"\\Interfaces\\" + adapterId;
        std::wstring afdParams = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Afd\\Parameters";
        std::wstring sysProfile = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
        std::wstring mmcssGames = sysProfile + L"\\Tasks\\Games";
        std::wstring prioControl = L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
        std::wstring execTimer = L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Executive";
        std::wstring pwrThrottling = L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling";
        std::wstring usbSvc = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\USB";
        std::wstring hagsKey = L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers";
        std::wstring kbdResp = L"HKCU\\Control Panel\\Accessibility\\Keyboard Response";
        std::wstring kbdPanel = L"HKCU\\Control Panel\\Keyboard";
        std::wstring mousePanel = L"HKCU\\Control Panel\\Mouse";
        std::wstring pschedPol = L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\Psched";
        std::wstring dataColl = L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection";
        std::wstring appCompat = L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\AppCompat";
        std::wstring gpuPref = L"HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences";
        std::wstring gamedvrStore = L"HKCU\\System\\GameConfigStore";
        std::wstring gamedvrPol = L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR";

        // Backups
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpIf, L"TcpAckFrequency"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpIf, L"TCPNoDelay"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpIf, L"TcpDelAckTicks"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"TcpAckFrequency"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"TCPNoDelay"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"TcpDelAckTicks"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"TcpMaxDataRetransmissions"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"TcpWindowSize"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"GlobalMaxTcpWindowSize"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"Tcp1323Opts"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"MaxFreeTcbs"));
        g_state.registry_backups.push_back(CreateRegistryBackup(tcpGlobal, L"MaxHashTableSize"));
        
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"DefaultReceiveWindow"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"DefaultSendWindow"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"FastSendDatagramThreshold"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"FastCopyReceiveThreshold"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"IgnoreNetworkOptimize"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"DisableRawSecurity"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"DynamicBacklogGrowthDelta"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"PriorityBoost"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"TransmitWorker"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"ReceiveWorker"));
        g_state.registry_backups.push_back(CreateRegistryBackup(afdParams, L"BufferMultiplier"));

        g_state.registry_backups.push_back(CreateRegistryBackup(prioControl, L"Win32PrioritySeparation"));
        g_state.registry_backups.push_back(CreateRegistryBackup(execTimer, L"MaximumTimerResolution"));
        g_state.registry_backups.push_back(CreateRegistryBackup(pwrThrottling, L"PowerThrottlingOff"));
        g_state.registry_backups.push_back(CreateRegistryBackup(usbSvc, L"DisableSelectiveSuspend"));
        g_state.registry_backups.push_back(CreateRegistryBackup(sysProfile, L"NetworkThrottlingIndex"));
        g_state.registry_backups.push_back(CreateRegistryBackup(sysProfile, L"SystemResponsiveness"));
        g_state.registry_backups.push_back(CreateRegistryBackup(mmcssGames, L"GPU Priority"));
        g_state.registry_backups.push_back(CreateRegistryBackup(mmcssGames, L"Priority"));
        g_state.registry_backups.push_back(CreateRegistryBackup(mmcssGames, L"Scheduling Category"));
        g_state.registry_backups.push_back(CreateRegistryBackup(mmcssGames, L"SFIO Priority"));
        g_state.registry_backups.push_back(CreateRegistryBackup(mmcssGames, L"Background Only"));
        g_state.registry_backups.push_back(CreateRegistryBackup(hagsKey, L"HwSchMode"));
        
        g_state.registry_backups.push_back(CreateRegistryBackup(kbdResp, L"AutoRepeatDelay"));
        g_state.registry_backups.push_back(CreateRegistryBackup(kbdResp, L"AutoRepeatRate"));
        g_state.registry_backups.push_back(CreateRegistryBackup(kbdResp, L"BounceTime"));
        g_state.registry_backups.push_back(CreateRegistryBackup(kbdResp, L"DelayBeforeAcceptance"));
        g_state.registry_backups.push_back(CreateRegistryBackup(kbdResp, L"Flags"));
        g_state.registry_backups.push_back(CreateRegistryBackup(kbdPanel, L"KeyboardDelay"));
        g_state.registry_backups.push_back(CreateRegistryBackup(kbdPanel, L"KeyboardSpeed"));
        g_state.registry_backups.push_back(CreateRegistryBackup(mousePanel, L"SmoothMouseXCurve"));
        g_state.registry_backups.push_back(CreateRegistryBackup(mousePanel, L"SmoothMouseYCurve"));
        
        g_state.registry_backups.push_back(CreateRegistryBackup(pschedPol, L"NonBestEffortLimit"));
        g_state.registry_backups.push_back(CreateRegistryBackup(dataColl, L"AllowTelemetry"));
        g_state.registry_backups.push_back(CreateRegistryBackup(appCompat, L"AITEnable"));
        g_state.registry_backups.push_back(CreateRegistryBackup(appCompat, L"DisableHEC"));
        g_state.registry_backups.push_back(CreateRegistryBackup(appCompat, L"DisableIECompatLog"));
        g_state.registry_backups.push_back(CreateRegistryBackup(gpuPref, L"javaw.exe"));
        g_state.registry_backups.push_back(CreateRegistryBackup(gpuPref, L"lunarclient.exe"));
        g_state.registry_backups.push_back(CreateRegistryBackup(gamedvrStore, L"GameDVR_Enabled"));
        g_state.registry_backups.push_back(CreateRegistryBackup(gamedvrPol, L"AllowGameDVR"));

        const wchar_t* mcApps[] = { L"javaw.exe", L"java.exe", L"lunarclient.exe", L"cheatbreaker.exe", L"badlion.exe" };
        for (const wchar_t* app : mcApps) {
            std::wstring ifeo = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + std::wstring(app) + L"\\PerfOptions";
            g_state.registry_backups.push_back(CreateRegistryBackup(ifeo, L"CpuPriorityClass"));
        }

        try
        {
            // 1. Keyboard & Mouse 1:1
            g_state.status_text = "Applying Kernel Keyboard & Mouse 1:1 curves...";
            WriteRegistryString(kbdResp, L"AutoRepeatDelay", L"140");
            WriteRegistryString(kbdResp, L"AutoRepeatRate", L"10");
            WriteRegistryString(kbdResp, L"BounceTime", L"0");
            WriteRegistryString(kbdResp, L"DelayBeforeAcceptance", L"0");
            WriteRegistryString(kbdResp, L"Flags", L"123");
            WriteRegistryString(kbdPanel, L"KeyboardDelay", L"0");
            WriteRegistryString(kbdPanel, L"KeyboardSpeed", L"31");

            BYTE blankX[40] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00 };
            BYTE blankY[40] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00 };
            WriteRegistryBinary(mousePanel, L"SmoothMouseXCurve", blankX, 40);
            WriteRegistryBinary(mousePanel, L"SmoothMouseYCurve", blankY, 40);

            // 2. Timers & CPU Scheduling
            g_state.status_text = "Configuring Quantum (38) & System Timers (0.5ms)...";
            WriteRegistryDWord(prioControl, L"Win32PrioritySeparation", 38);
            WriteRegistryDWord(execTimer, L"MaximumTimerResolution", 5000);
            WriteRegistryDWord(pwrThrottling, L"PowerThrottlingOff", 1);
            WriteRegistryDWord(usbSvc, L"DisableSelectiveSuspend", 1);
            WriteRegistryDWord(sysProfile, L"NetworkThrottlingIndex", 0xFFFFFFFF);
            WriteRegistryDWord(sysProfile, L"SystemResponsiveness", 0);
            WriteRegistryDWord(mmcssGames, L"GPU Priority", 8);
            WriteRegistryDWord(mmcssGames, L"Priority", 6);
            WriteRegistryString(mmcssGames, L"Scheduling Category", L"High");
            WriteRegistryString(mmcssGames, L"SFIO Priority", L"High");
            WriteRegistryDWord(mmcssGames, L"Background Only", 0);
            WriteRegistryDWord(hagsKey, L"HwSchMode", 2);

            RunCommandLine(L"bcdedit.exe", L"/set disabledynamictick yes");
            RunCommandLine(L"bcdedit.exe", L"/set useplatformtick yes");
            RunCommandLine(L"bcdedit.exe", L"/set useplatformclock no");

            // Powercfg C-States & ASPM
            RunCommandLine(L"powercfg.exe", L"-setacvalueindex scheme_current sub_processor CPMinCores 100");
            RunCommandLine(L"powercfg.exe", L"-setacvalueindex scheme_current sub_processor CPMaxCores 100");
            RunCommandLine(L"powercfg.exe", L"-setacvalueindex scheme_current sub_processor PERFBOOSTMODE 2");
            RunCommandLine(L"powercfg.exe", L"-setacvalueindex scheme_current sub_processor PERFDECREASETIME 1");
            RunCommandLine(L"powercfg.exe", L"-setacvalueindex scheme_current sub_processor PERFINCREASETHRESHOLD 10");
            RunCommandLine(L"powercfg.exe", L"-setacvalueindex scheme_current sub_processor LATENCYHINTING 1");
            RunCommandLine(L"powercfg.exe", L"-setacvalueindex scheme_current sub_pciexpress aspm 0");
            RunCommandLine(L"powercfg.exe", L"-setactive scheme_current");

            // 3. TCP/IP Stack & Hit Reg / KB
            // 3. TCP/IP Stack & Blatant Reach/KB Engine
            g_state.status_text = "Injecting Minecraft Blatant Reach & Instant Velocity Engine...";
            WriteRegistryDWord(tcpGlobal, L"TcpAckFrequency", 1);
            WriteRegistryDWord(tcpGlobal, L"TCPNoDelay", 1);
            WriteRegistryDWord(tcpGlobal, L"TcpDelAckTicks", 0);
            WriteRegistryDWord(tcpGlobal, L"TcpMaxDataRetransmissions", 8);
            WriteRegistryDWord(tcpGlobal, L"TcpMaxDupAcks", 2);
            WriteRegistryDWord(tcpGlobal, L"TcpTimedWaitDelay", 30);
            WriteRegistryDWord(tcpGlobal, L"TcpWindowSize", 16777216);
            WriteRegistryDWord(tcpGlobal, L"GlobalMaxTcpWindowSize", 16777216);
            WriteRegistryDWord(tcpGlobal, L"Tcp1323Opts", 3); // RFC 1323 Window Scaling activado
            WriteRegistryDWord(tcpGlobal, L"MaxFreeTcbs", 65536);
            WriteRegistryDWord(tcpGlobal, L"MaxHashTableSize", 4096);

            WriteRegistryDWord(tcpIf, L"TcpAckFrequency", 1);
            WriteRegistryDWord(tcpIf, L"TCPNoDelay", 1);
            WriteRegistryDWord(tcpIf, L"TcpDelAckTicks", 0);

            // Iterate all NIC subkeys
            HKEY hIfKey = NULL;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces", 0, KEY_READ | KEY_WOW64_64KEY, &hIfKey) == ERROR_SUCCESS) {
                wchar_t subName[256];
                DWORD idx = 0, cLen = 256;
                while (RegEnumKeyExW(hIfKey, idx++, subName, &cLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    std::wstring sPath = L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + std::wstring(subName);
                    WriteRegistryDWord(sPath, L"TcpAckFrequency", 1);
                    WriteRegistryDWord(sPath, L"TCPNoDelay", 1);
                    WriteRegistryDWord(sPath, L"TcpDelAckTicks", 0);
                    cLen = 256;
                }
                RegCloseKey(hIfKey);
            }

            // Cálculo dinámico extremo conectando los sliders de la aplicación
            float kbFactor = fmaxf(0.f, fminf(1.f, g_state.kb_reduction / 100.0f));
            float hitFactor = fmaxf(0.f, fminf(1.f, g_state.hit_reg / 100.0f));

            DWORD rcvWin = 16777216 + (DWORD)(50331648.0f * kbFactor); // 16MB a 64MB según slider KB
            DWORD fastCopy = 2048 + (DWORD)(14336.0f * hitFactor);     // 2048 a 16KB según slider HitReg

            WriteRegistryDWord(afdParams, L"DefaultReceiveWindow", rcvWin);
            WriteRegistryDWord(afdParams, L"DefaultSendWindow", rcvWin);
            WriteRegistryDWord(afdParams, L"FastSendDatagramThreshold", 131072);
            WriteRegistryDWord(afdParams, L"FastCopyReceiveThreshold", fastCopy);
            WriteRegistryDWord(afdParams, L"IgnoreNetworkOptimize", 0);
            WriteRegistryDWord(afdParams, L"DisableRawSecurity", 1);
            WriteRegistryDWord(afdParams, L"DynamicBacklogGrowthDelta", 0);
            WriteRegistryDWord(afdParams, L"PriorityBoost", 15);
            WriteRegistryDWord(afdParams, L"TransmitWorker", 32);
            WriteRegistryDWord(afdParams, L"ReceiveWorker", 32);
            WriteRegistryDWord(afdParams, L"BufferMultiplier", 4096);

            // Netsh Kernel Commands (RTO dinámico de 300ms hasta 100ms instantáneo)
            std::wstring rtoVal = std::to_wstring(300 - (int)(200.0f * hitFactor)); // 300ms a 100ms
            RunCommandLine(L"netsh.exe", L"int tcp set global timestamps=disabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global ecncapability=disabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global rss=enabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global heuristics=disabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global autotuninglevel=experimental");
            RunCommandLine(L"netsh.exe", L"int tcp set global initialRto=" + rtoVal);
            RunCommandLine(L"netsh.exe", L"int tcp set supplemental template=custom congestionprovider=ctcp");
            RunCommandLine(L"netsh.exe", L"int tcp set supplemental template=internet congestionprovider=ctcp");
            RunCommandLine(L"netsh.exe", L"int tcp set global maxsynretransmissions=2");
            RunCommandLine(L"netsh.exe", L"int tcp set global pacingprofile=off");
            RunCommandLine(L"netsh.exe", L"int tcp set global nonsackrttresiliency=disabled");
            if (g_state.smart_packets) {
                RunCommandLine(L"netsh.exe", L"int tcp set global rsc=disabled");
            }

            // NIC Advanced Properties (Búferes extremos de tarjeta hasta 4096)
            std::wstring name_escaped = adapterName;
            size_t p = 0;
            while ((p = name_escaped.find(L"'", p)) != std::wstring::npos) {
                name_escaped.replace(p, 1, L"''");
                p += 2;
            }
            std::wstring rxBufs = std::to_wstring(512 + (int)(3584.0f * kbFactor)); // 512 a 4096 según slider
            std::wstring nicCmds = L"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Transmit Buffers' -DisplayValue '" + rxBufs + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue; "
                                   L"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Receive Buffers' -DisplayValue '" + rxBufs + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue; "
                                   L"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Flow Control' -DisplayValue 'Disabled' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue; "
                                   L"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Interrupt Moderation' -DisplayValue 'Disabled' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue";
            RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"" + nicCmds + L"\"");

            // 4. Exterminate Telemetry & QoS Limits
            g_state.status_text = "Disabling Windows Telemetry & QoS throttling...";
            WriteRegistryDWord(pschedPol, L"NonBestEffortLimit", 0);
            WriteRegistryDWord(dataColl, L"AllowTelemetry", 0);
            WriteRegistryDWord(appCompat, L"AITEnable", 0);
            WriteRegistryDWord(appCompat, L"DisableHEC", 1);
            WriteRegistryDWord(appCompat, L"DisableIECompatLog", 1);
            WriteRegistryDWord(gamedvrStore, L"GameDVR_Enabled", 0);
            WriteRegistryDWord(gamedvrPol, L"AllowGameDVR", 0);

            RunCommandLine(L"sc.exe", L"config DiagTrack start= disabled");
            RunCommandLine(L"net.exe", L"stop DiagTrack /y");
            RunCommandLine(L"sc.exe", L"config dmwappushservice start= disabled");
            RunCommandLine(L"net.exe", L"stop dmwappushservice /y");
            RunCommandLine(L"sc.exe", L"config WerSvc start= disabled");
            RunCommandLine(L"net.exe", L"stop WerSvc /y");

            // 5. Minecraft Process Prioritization & Multiclient
            g_state.status_text = "Configuring QoS DSCP 46 & High Priority for Minecraft...";
            for (const wchar_t* app : mcApps) {
                std::wstring ifeo = L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + std::wstring(app) + L"\\PerfOptions";
                WriteRegistryDWord(ifeo, L"CpuPriorityClass", 3);
            }
            WriteRegistryString(gpuPref, L"javaw.exe", L"GpuPreference=2;");
            WriteRegistryString(gpuPref, L"lunarclient.exe", L"GpuPreference=2;");

            std::wstring qosCmd = L"Remove-NetQosPolicy -Confirm:$false -ErrorAction SilentlyContinue; "
                                  L"New-NetQosPolicy -Name 'MinecraftJavaQoS' -AppPathName 'javaw.exe' -DSCPValue 46 -Confirm:$false -ErrorAction SilentlyContinue; "
                                  L"New-NetQosPolicy -Name 'Java1710QoS' -AppPathName 'java.exe' -DSCPValue 46 -Confirm:$false -ErrorAction SilentlyContinue; "
                                  L"New-NetQosPolicy -Name 'CheatBreakerQoS' -AppPathName 'cheatbreaker.exe' -DSCPValue 46 -Confirm:$false -ErrorAction SilentlyContinue; "
                                  L"New-NetQosPolicy -Name 'LunarClientQoS' -AppPathName 'lunarclient.exe' -DSCPValue 46 -Confirm:$false -ErrorAction SilentlyContinue";
            RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"" + qosCmd + L"\"");

            g_state.status_text = "Applying Multiclient Minecraft & JVM G1GC tweaks...";
            ApplyGameConfigOptimizations();

            // Restart NIC
            g_state.status_text = "Refreshing network adapter silently...";
            std::wstring restartCmd = L"Disable-NetAdapter -Name '" + name_escaped + L"' -Confirm:$false; Start-Sleep -Milliseconds 500; Enable-NetAdapter -Name '" + name_escaped + L"' -Confirm:$false";
            RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"" + restartCmd + L"\"");

            g_state.is_optimized = true;
            g_state.status_text = "Pulse Blatant: Reach Extender & Instant Velocity Active!";
        }
        catch (...) {
            g_state.status_text = "Optimization failed.";
            g_state.is_processing = false;
            restore_defaults_thread();
            return;
        }
        g_state.is_processing = false;
    }

    void apply_tweaks_async()
    {
        std::thread(apply_tweaks_thread).detach();
    }

    void restore_defaults_thread()
    {
        g_state.is_processing = true;
        g_state.status_text = "Restoring original system & network settings...";
        
        try {
            for (const auto& backup : g_state.registry_backups) {
                RestoreRegistryBackup(backup);
            }
            g_state.registry_backups.clear();

            std::wstring rsc_val = g_state.original_rsc_state;
            RunCommandLine(L"netsh.exe", L"int tcp set global rsc=" + rsc_val);
            RunCommandLine(L"netsh.exe", L"int tcp set global timestamps=enabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global ecncapability=enabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global rss=enabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global heuristics=enabled");
            RunCommandLine(L"netsh.exe", L"int tcp set global autotuninglevel=normal");

            RunCommandLine(L"bcdedit.exe", L"/set disabledynamictick no");
            RunCommandLine(L"bcdedit.exe", L"/set useplatformtick no");

            if (!g_state.active_adapter_name.empty()) {
                std::wstring adapterName = g_state.active_adapter_name;
                std::wstring name_escaped = adapterName;
                size_t pos = 0;
                while ((pos = name_escaped.find(L"'", pos)) != std::wstring::npos) {
                    name_escaped.replace(pos, 1, L"''");
                    pos += 2;
                }

                if (!g_state.original_tx_buffers.empty()) {
                    RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Transmit Buffers' -DisplayValue '" + g_state.original_tx_buffers + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue\"");
                }
                if (!g_state.original_rx_buffers.empty()) {
                    RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Receive Buffers' -DisplayValue '" + g_state.original_rx_buffers + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue\"");
                }
                if (!g_state.original_flow_control.empty()) {
                    RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Flow Control' -DisplayValue '" + g_state.original_flow_control + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue\"");
                }
                if (!g_state.original_interrupt_mod.empty()) {
                    RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"Set-NetAdapterAdvancedProperty -Name '" + name_escaped + L"' -DisplayName 'Interrupt Moderation' -DisplayValue '" + g_state.original_interrupt_mod + L"' -NoReboot -Confirm:$false -ErrorAction SilentlyContinue\"");
                }
            }

            RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"Remove-NetQosPolicy -Confirm:$false -ErrorAction SilentlyContinue\"");

            g_state.status_text = "Restoring Lunar & Minecraft configs...";
            RestoreGameConfigOriginals();

            if (!g_state.active_adapter_name.empty()) {
                g_state.status_text = "Restarting adapter to restore defaults...";
                std::wstring name_escaped = g_state.active_adapter_name;
                size_t pos = 0;
                while ((pos = name_escaped.find(L"'", pos)) != std::wstring::npos) {
                    name_escaped.replace(pos, 1, L"''");
                    pos += 2;
                }
                std::wstring restartCmd = L"Disable-NetAdapter -Name '" + name_escaped + L"' -Confirm:$false; Start-Sleep -Milliseconds 500; Enable-NetAdapter -Name '" + name_escaped + L"' -Confirm:$false";
                RunCommandLine(L"powershell.exe", L"-NoProfile -WindowStyle Hidden -Command \"" + restartCmd + L"\"");
            }

            g_state.is_optimized = false;
            g_state.status_text = "Pulse Engine: Cleaned and Restored.";
        }
        catch (...) {
            g_state.status_text = "Restoration error.";
        }
        g_state.is_processing = false;
    }

    void restore_defaults_async()
    {
        std::thread(restore_defaults_thread).detach();
    }

    void cleanup_on_exit()
    {
        if (g_state.is_optimized)
        {
            restore_defaults_thread();
        }
    }
}
