#include "inject.h"
#include <TlHelp32.h>

bool injectDll(DWORD pid, const std::wstring& dllPath) {
	// Open target process
	HANDLE process = OpenProcess(
		PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | 
		PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
		FALSE, pid
	);
	
	if (!process) return false;
	
	// Allocate memory in target for DLL path
	size_t pathSize = (dllPath.length() + 1) * sizeof(wchar_t);
	LPVOID remotePath = VirtualAllocEx(process, nullptr, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	
	if (!remotePath) {
		CloseHandle(process);
		return false;
	}
	
	// Write DLL path to target process
	if (!WriteProcessMemory(process, remotePath, dllPath.c_str(), pathSize, nullptr)) {
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}
	
	// Get LoadLibraryW address
	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	LPTHREAD_START_ROUTINE loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(kernel32, "LoadLibraryW")
	);
	
	// Create remote thread to load DLL
	HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
	
	if (!thread) {
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}
	
	// Wait for thread to complete
	WaitForSingleObject(thread, 5000);
	
	// Cleanup
	VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
	CloseHandle(thread);
	CloseHandle(process);
	
	return true;
}

HMODULE getRemoteModuleHandle(DWORD pid, const std::wstring& moduleName) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snapshot == INVALID_HANDLE_VALUE) return nullptr;
	
	MODULEENTRY32W entry;
	entry.dwSize = sizeof(entry);
	
	HMODULE result = nullptr;
	
	if (Module32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szModule, moduleName.c_str()) == 0) {
				result = entry.hModule;
				break;
			}
		} while (Module32NextW(snapshot, &entry));
	}
	
	CloseHandle(snapshot);
	return result;
}

bool ejectDll(DWORD pid, const std::wstring& dllName) {
	HMODULE remoteModule = getRemoteModuleHandle(pid, dllName);
	if (!remoteModule) return false;
	
	HANDLE process = OpenProcess(
		PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
		FALSE, pid
	);
	
	if (!process) return false;
	
	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	LPTHREAD_START_ROUTINE freeLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(kernel32, "FreeLibrary")
	);
	
	HANDLE thread = CreateRemoteThread(process, nullptr, 0, freeLibrary, remoteModule, 0, nullptr);
	
	if (!thread) {
		CloseHandle(process);
		return false;
	}
	
	WaitForSingleObject(thread, 5000);
	
	CloseHandle(thread);
	CloseHandle(process);
	
	return true;
}
