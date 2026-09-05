#pragma once

// The marker SDK is linked only for Pack builds. Development builds retain
// identical source structure while compiling the markers as inert stubs.
#ifdef COBBLE_DEV_BUILD

inline void REProtectBeginUltra(const char*) {}
inline void REProtectEnd() {}

#else

#include <REProtectSDK.h>

#endif

#define COBBLE_DLL_ULTRA_START(name) REProtectBeginUltra(name);
#define COBBLE_DLL_ULTRA_END()        REProtectEnd();
