#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <string>
#include <vector>

namespace optimizer
{
    struct RegistryValueBackup {
        std::wstring keyPath;
        std::wstring valueName;
        bool existed = false;
        DWORD type = 0;
        std::vector<BYTE> data;
    };

    struct GameConfigBackup {
        std::wstring filePath;
        std::string originalContent;
    };

    struct State {
        bool smart_packets = true;
        float hit_reg = 100.f;
        float kb_reduction = 100.f;
        int connection_type = 0; // 0 = Fiber, 1 = Wi-Fi
        bool is_optimized = false;
        bool is_processing = false;
        std::string status_text = "Ready to pulse.";
        
        // Adapter info
        std::wstring active_adapter_id;
        std::wstring active_adapter_name;
        bool is_wireless = false;

        // Backups
        std::vector<RegistryValueBackup> registry_backups;
        std::vector<GameConfigBackup> config_backups;
        std::wstring original_rsc_state = L"enabled";
        DWORD original_mtu = 1500;
        std::wstring original_tx_buffers;
        std::wstring original_rx_buffers;
        std::wstring original_flow_control;
        std::wstring original_interrupt_mod;
        std::wstring original_vlan_id;
        std::wstring original_power_plan_guid;
    };

    extern State g_state;

    void detect_connection_type();
    void apply_tweaks_async();
    void restore_defaults_async();
    void cleanup_on_exit();
}
