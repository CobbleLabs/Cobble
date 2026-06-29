#include "anti_tamper.h"
#include "xorstr.h"
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <thread>
#include <vector>
#include <string>
#include <chrono>

namespace
{
    typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    typedef LONG(NTAPI* pfnNtSetInformationThread)(HANDLE, ULONG, PVOID, ULONG);

    void hide_thread(HANDLE hThread)
    {
        OPAQUE_JUNK();
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll) return;
        auto NtSetInfThread = reinterpret_cast<pfnNtSetInformationThread>(GetProcAddress(hNtdll, "NtSetInformationThread"));
        if (NtSetInfThread)
        {
            NtSetInfThread(hThread, 0x11 /*ThreadHideFromDebugger*/, nullptr, 0);
        }
    }

    bool check_hardware_breakpoints()
    {
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        HANDLE hThread = GetCurrentThread();
        if (GetThreadContext(hThread, &ctx))
        {
            if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0)
                return true;
        }
        return false;
    }

    bool check_debug_flags()
    {
        if (IsDebuggerPresent()) return true;

        BOOL remote = FALSE;
        CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
        if (remote) return true;

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll) return false;

        auto NtQueryInfProc = reinterpret_cast<pfnNtQueryInformationProcess>(GetProcAddress(hNtdll, "NtQueryInformationProcess"));
        if (!NtQueryInfProc) return false;

        DWORD_PTR debugPort = 0;
        if (NT_SUCCESS(NtQueryInfProc(GetCurrentProcess(), (PROCESSINFOCLASS)7 /*ProcessDebugPort*/, &debugPort, sizeof(debugPort), nullptr)))
        {
            if (debugPort != 0) return true;
        }

        DWORD debugFlags = 0;
        if (NT_SUCCESS(NtQueryInfProc(GetCurrentProcess(), (PROCESSINFOCLASS)31 /*ProcessDebugFlags*/, &debugFlags, sizeof(debugFlags), nullptr)))
        {
            if (debugFlags == 0) return true; // 0 = Debugger present (NoDebugInherit is false)
        }

        HANDLE debugObjectHandle = nullptr;
        if (NT_SUCCESS(NtQueryInfProc(GetCurrentProcess(), (PROCESSINFOCLASS)30 /*ProcessDebugObjectHandle*/, &debugObjectHandle, sizeof(debugObjectHandle), nullptr)))
        {
            if (debugObjectHandle != nullptr) return true;
        }

        return false;
    }

    bool check_blacklisted_tools()
    {
        const wchar_t* badTools[] = {
            L"x64dbg.exe", L"x32dbg.exe", L"ida64.exe", L"ida.exe",
            L"cheatengine-x86_64.exe", L"cheatengine-i386.exe", L"ProcessHacker.exe",
            L"KsDumperClient.exe", L"Scylla.exe", L"HttpDebuggerUI.exe", L"dnSpy.exe",
            L"Fiddler.exe", L"Charles.exe", L"Wireshark.exe", L"HTTPDebuggerSvc.exe",
            L"ResourceHacker.exe", L"PE-bear.exe", L"pestudio.exe", L"MegaDumper.exe",
            L"PETools.exe", L"LordPE.exe", L"GH Injector.exe", L"x96dbg.exe", L"ollydbg.exe"
        };

        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(pe32);
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;

        bool found = false;
        if (Process32FirstW(hSnap, &pe32))
        {
            do
            {
                for (const auto* bt : badTools)
                {
                    if (_wcsicmp(pe32.szExeFile, bt) == 0)
                    {
                        found = true;
                        break;
                    }
                }
                if (found) break;
            } while (Process32NextW(hSnap, &pe32));
        }
        CloseHandle(hSnap);
        return found;
    }

    bool check_vm_environment()
    {
        // Detectar si está corriendo dentro de VMware o VirtualBox
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VMware, Inc.\\VMware Tools", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return true;
        }
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Oracle\\VirtualBox Guest Additions", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return true;
        }
        return false;
    }

    void vigilance_loop()
    {
        hide_thread(GetCurrentThread());

        while (true)
        {
            if (check_debug_flags() || check_hardware_breakpoints() || check_blacklisted_tools())
            {
                // Alerta de seguridad y crash inmediato
                MessageBoxW(nullptr, 
                    L"SECURITY ALERT:\nAnalizador de paquetes, depurador o herramienta de cracking detectada (Cheat Engine, x64dbg, Wireshark, Process Hacker, etc.).\n\nEl acceso a Pulse ha sido bloqueado por protección Anti-Tamper.", 
                    L"Pulse Security Engine", MB_ICONERROR | MB_OK);
                TerminateProcess(GetCurrentProcess(), 0xBAD);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    }
}

namespace anti_tamper
{
    void initialize()
    {
        /*
        if (check_vm_environment())
        {
            MessageBoxW(nullptr, L"Pulse no puede ser ejecutado dentro de máquinas virtuales (Sandboxes).", L"Pulse Protection", MB_ICONWARNING | MB_OK);
            TerminateProcess(GetCurrentProcess(), 0xBAD);
        }
        */

        std::thread t(vigilance_loop);
        t.detach();
    }
}
