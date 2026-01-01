#pragma once

#include <atomic>

// Winsock packet interception hooks for Blink module
// Uses MinHook to intercept WSASend/WSARecv from WS2_32.dll

namespace WinsockHooks {
    // Atomic flags to control packet flow (thread-safe)
    inline std::atomic<bool> StopOutbound{false};  // Block outgoing packets (others see you frozen)
    inline std::atomic<bool> StopInbound{false};   // Block incoming packets (you see world frozen)
    
    // Initialize hooks - call from cobble::initialize()
    bool Init();
    
    // Remove hooks - call from cobble::shutdown()
    void Shutdown();
    
    // Check if hooks are installed
    bool IsInitialized();
    
    // Packet counting for debugging
    int GetBlockedCount();          // WSASend blocked count
    int GetSendCount();             // send() call count
    int GetAfdSendBlockedCount();   // NtDeviceIoControlFile AFD_SEND blocked (Java NIO)
    void ResetBlockedCount();
}
