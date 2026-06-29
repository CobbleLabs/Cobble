#pragma once

// The marker SDK is linked only for Pack builds. Development builds retain
// identical source structure while compiling the markers as inert stubs.
#ifdef COBBLE_DEV_BUILD

inline void VMProtectBeginUltra(const char*) {}
inline void VMProtectEnd() {}

#else

#include <VMProtectSDK.h>

#endif

#define COBBLE_DLL_ULTRA_START(name) VMProtectBeginUltra(name);
#define COBBLE_DLL_ULTRA_END()        VMProtectEnd();
