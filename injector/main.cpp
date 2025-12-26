#include <Windows.h>
#include <iostream>
#include <string>
#include <filesystem>

#include "process.h"
#include "inject.h"

// Console colors
void setColor(int color) {
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}

void printBanner() {
	setColor(11); // Cyan
	std::wcout << L"\n";
	std::wcout << L"   ██████╗ ██████╗ ██████╗ ██████╗ ██╗     ███████╗\n";
	std::wcout << L"  ██╔════╝██╔═══██╗██╔══██╗██╔══██╗██║     ██╔════╝\n";
	std::wcout << L"  ██║     ██║   ██║██████╔╝██████╔╝██║     █████╗  \n";
	std::wcout << L"  ██║     ██║   ██║██╔══██╗██╔══██╗██║     ██╔══╝  \n";
	std::wcout << L"  ╚██████╗╚██████╔╝██████╔╝██████╔╝███████╗███████╗\n";
	std::wcout << L"   ╚═════╝ ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝╚══════╝\n";
	setColor(7); // White
	std::wcout << L"                    Injector v1.0\n\n";
}

void printStatus(const std::wstring& msg, int color = 7) {
	setColor(8); // Gray
	std::wcout << L"  [";
	setColor(color);
	std::wcout << L"*";
	setColor(8);
	std::wcout << L"] ";
	setColor(7);
	std::wcout << msg << L"\n";
}

void printSuccess(const std::wstring& msg) { printStatus(msg, 10); } // Green
void printError(const std::wstring& msg) { printStatus(msg, 12); }   // Red
void printInfo(const std::wstring& msg) { printStatus(msg, 11); }    // Cyan

std::wstring getDllPath() {
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	
	std::filesystem::path exePath(path);
	std::filesystem::path dllPath = exePath.parent_path() / L"Cobble.dll";
	
	return dllPath.wstring();
}

int wmain(int argc, wchar_t* argv[]) {
	SetConsoleTitleW(L"Cobble Injector");
	printBanner();
	
	// Check if DLL exists
	std::wstring dllPath = getDllPath();
	
	if (!std::filesystem::exists(dllPath)) {
		printError(L"Cobble.dll not found!");
		printInfo(L"Make sure Cobble.dll is in the same folder as this injector.");
		std::wcout << L"\n  Press any key to exit...";
		std::cin.get();
		return 1;
	}
	
	printSuccess(L"Found Cobble.dll");
	
	// Find Lunar Client
	printInfo(L"Searching for Lunar Client...");
	
	ProcessInfo lunar = findLunarClient();
	
	if (lunar.pid == 0) {
		printError(L"Lunar Client not found!");
		printInfo(L"Make sure Minecraft is running via Lunar Client.");
		std::wcout << L"\n  Press any key to exit...";
		std::cin.get();
		return 1;
	}
	
	printSuccess(L"Found: " + lunar.windowTitle + L" (PID: " + std::to_wstring(lunar.pid) + L")");
	
	// Inject DLL
	printInfo(L"Injecting Cobble.dll...");
	
	if (!injectDll(lunar.pid, dllPath)) {
		printError(L"Injection failed!");
		printInfo(L"Try running as Administrator.");
		std::wcout << L"\n  Press any key to exit...";
		std::cin.get();
		return 1;
	}
	
	printSuccess(L"Injected successfully!");
	
	std::wcout << L"\n";
	setColor(10); // Green
	std::wcout << L"  Cobble is now running!\n";
	setColor(7);
	std::wcout << L"  Open config/index.html to configure.\n";
	std::wcout << L"\n";
	
	printInfo(L"Press E to eject, or close this window to keep running.");
	
	// Wait for eject command
	while (true) {
		if (GetAsyncKeyState('E') & 0x8000) {
			printInfo(L"Ejecting...");
			
			if (ejectDll(lunar.pid, L"Cobble.dll")) {
				printSuccess(L"Ejected successfully!");
			} else {
				printError(L"Eject failed (might already be unloaded)");
			}
			break;
		}
		
		// Check if Minecraft still running
		if (!isProcessRunning(lunar.pid)) {
			printInfo(L"Minecraft closed, exiting...");
			break;
		}
		
		Sleep(100);
	}
	
	std::wcout << L"\n  Press any key to exit...";
	std::cin.get();
	return 0;
}
