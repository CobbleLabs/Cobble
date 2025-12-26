#pragma once

#include <Windows.h>
#include <string>

// Inject DLL into target process
bool injectDll(DWORD pid, const std::wstring& dllPath);

// Eject DLL from target process
bool ejectDll(DWORD pid, const std::wstring& dllName);
