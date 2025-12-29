#pragma once

#include <Windows.h>
#include "cobble.hpp"

DWORD WINAPI MainThread(LPVOID lpParam)
{
	cobble::context::initialize(static_cast<HMODULE>(lpParam));
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		// Check if manually mapped (lpReserved is usually non-null for manual map payload)
		// We can also check if hModule is in LDR, but lpReserved is sufficient for our loader.
		bool isManualMap = (lpReserved != nullptr);

		// DisableThreadLibraryCalls checks LDR, so skip it if manual mapped
		if (!isManualMap) {
			DisableThreadLibraryCalls(hModule);
		} else {
			// Store checking flag for cobble context if needed
			// For now just proceeding.
		}

		HANDLE hThread = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);

		if (hThread)
			CloseHandle(hThread);
	}

	return TRUE;
}
