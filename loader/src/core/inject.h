#pragma once

#include <Windows.h>
#include <string>
#include <vector>

using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char* lpLibFilename);
using f_GetProcAddress = UINT_PTR(WINAPI*)(HINSTANCE hModule, const char* lpProcName);
using f_DllMain = BOOL(WINAPI*)(void* hDll, DWORD dwReason, void* pReserved);

struct MANUAL_MAPPING_DATA {
    f_LoadLibraryA pLoadLibraryA;
    f_GetProcAddress pGetProcAddress;
    HINSTANCE hMod;
};

// Inject DLL into target process using Manual Mapping (from Disk)
bool manualMap(DWORD pid, const std::wstring& dllPath);

// Inject DLL into target process using Manual Mapping (from Memory)
bool manualMap(DWORD pid, BYTE* pData, size_t dataSize);

// Legacy inject (LoadLibrary) - kept for fallback/reference
bool injectDllLoadLibrary(DWORD pid, const std::wstring& dllPath);

// Check if DLL is already injected (checks for module name, might fail with manual map)
bool isDllInjected(DWORD pid, const std::wstring& dllName);

// Eject DLL from target process
bool ejectDll(DWORD pid, const std::wstring& dllName);
