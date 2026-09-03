#pragma once

#include "../java.h"

// ============================================================================
// PacketInterceptor - Apollo Teams packet interception
// Lunar Client team/nametag packets for friend/team detection.
// ============================================================================

namespace PacketInterceptor {

// Initialize the packet interceptor
bool Init();

// Cleanup. Returns false when any bytecode or native bridge teardown stage is
// incomplete; callers must keep the DLL loaded so retained entrypoints remain valid.
bool Shutdown();

// Check if initialized successfully
bool IsReady();

}  // namespace PacketInterceptor
