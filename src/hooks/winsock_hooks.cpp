#include "winsock_hooks.h"
#include "../sdk/core/minecraft.h"
#include "../util/logger.hpp"

#include <Windows.h>
#include <WinSock2.h>
#include <winternl.h>  // For NTSTATUS
#include "../../infra/ext/minhook/minhook.h"

#pragma comment(lib, "ws2_32.lib")

// NTAPI calling convention if not defined
#ifndef NTAPI
#define NTAPI __stdcall
#endif

// IOCTL code for AFD send operations (used by Java NIO)
#define IOCTL_AFD_SEND 0x1201F

// NtDeviceIoControlFile function signature (from ntdll.dll)
typedef NTSTATUS(NTAPI* NtDeviceIoControlFile_t)(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
                                                 PVOID IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer,
                                                 ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);

namespace {
// Original function pointers
typedef int(WINAPI* WSASend_t)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int(WINAPI* WSARecv_t)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, LPWSAOVERLAPPED,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int(WINAPI* send_t)(SOCKET, const char*, int, int);

WSASend_t OriginalWSASend = nullptr;
WSARecv_t OriginalWSARecv = nullptr;
send_t OriginalSend = nullptr;
NtDeviceIoControlFile_t OriginalNtDeviceIoControlFile = nullptr;

bool hooksInitialized = false;
std::atomic<int> blockedPacketCount{0};
std::atomic<int> sendCallCount{0};
std::atomic<int> afdSendBlockedCount{0};  // Track NIO blocked sends

// Hooked NtDeviceIoControlFile - intercepts Java NIO socket sends
NTSTATUS NTAPI HookedNtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
                                           PVOID IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer,
                                           ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength) {
    // Check if this is an AFD send operation (used by Java NIO for socket sends)
    if (IoControlCode == IOCTL_AFD_SEND && WinsockHooks::StopOutbound.load()) {
        afdSendBlockedCount++;

        // Block by busy-waiting until StopOutbound is false
        while (WinsockHooks::StopOutbound.load()) {
            Sleep(1);
        }
    }

    return OriginalNtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode,
                                         InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);
}

// Hooked WSASend - blocks outbound packets when StopOutbound is true
int WINAPI HookedWSASend(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent, DWORD dwFlags,
                         LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
    if (WinsockHooks::StopOutbound.load()) {
        blockedPacketCount++;
        while (WinsockHooks::StopOutbound.load()) {
            Sleep(1);
        }
    }

    return OriginalWSASend(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped,
                           lpCompletionRoutine);
}

// Hooked send - counts packets for debugging
int WINAPI HookedSend(SOCKET s, const char* buf, int len, int flags) {
    if (WinsockHooks::StopOutbound.load()) {
        sendCallCount++;
    }

    return OriginalSend(s, buf, len, flags);
}

// Hooked WSARecv - blocks inbound packets when StopInbound is true
int WINAPI HookedWSARecv(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                         LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped,
                         LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
    while (WinsockHooks::StopInbound.load()) {
        Sleep(1);
    }

    return OriginalWSARecv(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped,
                           lpCompletionRoutine);
}
}  // namespace

bool WinsockHooks::Init() {
    if (hooksInitialized)
        return true;

    // Initialize MinHook
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        log_err("Failed to initialize MinHook: %d", status);
        return false;
    }

    // Get ntdll.dll for NtDeviceIoControlFile (Java NIO interception)
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        log_err("Failed to get ntdll.dll");
        return false;
    }

    LPVOID pNtDeviceIoControlFile = (LPVOID)GetProcAddress(ntdll, "NtDeviceIoControlFile");
    if (!pNtDeviceIoControlFile) {
        log_err("Failed to find NtDeviceIoControlFile");
        return false;
    }

    // Hook NtDeviceIoControlFile (critical for Java NIO)
    if (MH_CreateHook(pNtDeviceIoControlFile, &HookedNtDeviceIoControlFile, (LPVOID*)&OriginalNtDeviceIoControlFile) !=
        MH_OK) {
        log_err("Failed to create NtDeviceIoControlFile hook");
        return false;
    }

    if (MH_EnableHook(pNtDeviceIoControlFile) != MH_OK) {
        log_err("Failed to enable NtDeviceIoControlFile hook");
        MH_RemoveHook(pNtDeviceIoControlFile);
        return false;
    }

    log_ok("NtDeviceIoControlFile hook installed (Java NIO interception)");

    // Also hook WS2_32 for fallback
    HMODULE ws2_32 = GetModuleHandleA("WS2_32.dll");
    if (ws2_32) {
        LPVOID pWSASend = (LPVOID)GetProcAddress(ws2_32, "WSASend");
        LPVOID pWSARecv = (LPVOID)GetProcAddress(ws2_32, "WSARecv");
        LPVOID pSend = (LPVOID)GetProcAddress(ws2_32, "send");

        if (pWSASend) {
            if (MH_CreateHook(pWSASend, &HookedWSASend, (LPVOID*)&OriginalWSASend) == MH_OK) {
                MH_EnableHook(pWSASend);
            }
        }

        if (pSend) {
            if (MH_CreateHook(pSend, &HookedSend, (LPVOID*)&OriginalSend) == MH_OK) {
                MH_EnableHook(pSend);
            }
        }

        if (pWSARecv) {
            if (MH_CreateHook(pWSARecv, &HookedWSARecv, (LPVOID*)&OriginalWSARecv) == MH_OK) {
                MH_EnableHook(pWSARecv);
            }
        }
    }

    hooksInitialized = true;
    log_ok("Network hooks installed (NtDeviceIoControlFile + Winsock)");
    return true;
}

void WinsockHooks::Shutdown() {
    if (!hooksInitialized)
        return;

    // Ensure packets are flowing before unhooking
    StopOutbound = false;
    StopInbound = false;

    Sleep(50);  // Give time for any blocked calls to complete

    // Disable and remove all hooks
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);

    hooksInitialized = false;

    MH_Uninitialize();

    log_ok("Network hooks removed");
}

bool WinsockHooks::IsInitialized() {
    return hooksInitialized;
}

int WinsockHooks::GetBlockedCount() {
    return blockedPacketCount.load();
}

int WinsockHooks::GetSendCount() {
    return sendCallCount.load();
}

int WinsockHooks::GetAfdSendBlockedCount() {
    return afdSendBlockedCount.load();
}

void WinsockHooks::ResetBlockedCount() {
    blockedPacketCount = 0;
    sendCallCount = 0;
    afdSendBlockedCount = 0;
}
