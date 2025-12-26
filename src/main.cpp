#pragma once

#include <Windows.h>
#include "cobble.hpp"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);

		HANDLE hThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(cobble::context::initialize), hModule, 0, nullptr);

		if (hThread)
			CloseHandle(hThread);
	}

	return TRUE;
}
