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
		bool isManualMap = (lpReserved != nullptr);

		if (!isManualMap) {
			DisableThreadLibraryCalls(hModule);
		}

		HANDLE hThread = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
		if (hThread)
			CloseHandle(hThread);
	}

	return TRUE;
}
