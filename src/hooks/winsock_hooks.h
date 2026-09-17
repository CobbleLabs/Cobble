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

// Exempts the calling thread from the StopOutbound/StopInbound stalls above.
//
// Those gates have to be process-wide (Minecraft's socket work is spread across
// netty threads and Java NIO, so there is no single socket to filter on), which
// means any *other* background request in the process gets frozen along with the
// game. The Alt Manager login worker uses this so a sign-in started while Blink
// is on still completes - and, more importantly, so ejecting mid-blink cannot
// deadlock joining a worker parked in the busy-wait.
void ExemptCurrentThread();
void UnexemptCurrentThread();

// Lets the stalls go before teardown, so no thread is left parked in the
// busy-wait while the hooks themselves are being removed.
void ReleaseStalls();

bool IsInitialized();
}  // namespace WinsockHooks
