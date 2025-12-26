#include "process.h"
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")

// Callback for EnumWindows
struct EnumWindowsData {
	DWORD targetPid;
	std::wstring windowTitle;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
	auto* data = reinterpret_cast<EnumWindowsData*>(lParam);
	
	DWORD pid;
	GetWindowThreadProcessId(hwnd, &pid);
	
	if (pid == data->targetPid) {
		wchar_t title[256];
		if (GetWindowTextW(hwnd, title, 256) > 0) {
			data->windowTitle = title;
			return FALSE; // Stop enumeration
		}
	}
	return TRUE;
}

std::wstring getWindowTitle(DWORD pid) {
	EnumWindowsData data = { pid, L"" };
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
	return data.windowTitle;
}

std::vector<ProcessInfo> findProcessesByName(const std::wstring& name) {
	std::vector<ProcessInfo> results;
	
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return results;
	
	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);
	
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
				ProcessInfo info;
				info.pid = entry.th32ProcessID;
				info.name = entry.szExeFile;
				info.windowTitle = getWindowTitle(info.pid);
				results.push_back(info);
			}
		} while (Process32NextW(snapshot, &entry));
	}
	
	CloseHandle(snapshot);
	return results;
}

ProcessInfo findLunarClient() {
	// Look for javaw.exe with "Lunar Client" in window title
	auto javaProcesses = findProcessesByName(L"javaw.exe");
	
	for (const auto& proc : javaProcesses) {
		if (proc.windowTitle.find(L"Lunar Client") != std::wstring::npos ||
			proc.windowTitle.find(L"Minecraft") != std::wstring::npos) {
			return proc;
		}
	}
	
	// Not found
	return { 0, L"", L"" };
}

bool isProcessRunning(DWORD pid) {
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return false;
	
	DWORD exitCode;
	if (GetExitCodeProcess(process, &exitCode)) {
		CloseHandle(process);
		return exitCode == STILL_ACTIVE;
	}
	
	CloseHandle(process);
	return false;
}
