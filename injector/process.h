#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>

struct ProcessInfo {
	DWORD pid;
	std::wstring name;
	std::wstring windowTitle;
};

// Find all processes with given name
std::vector<ProcessInfo> findProcessesByName(const std::wstring& name);

// Find Lunar Client process (javaw.exe with Lunar window)
ProcessInfo findLunarClient();

// Check if process is running
bool isProcessRunning(DWORD pid);
