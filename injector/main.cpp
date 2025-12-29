#include <Windows.h>
#include <iostream>
#include <string>
#include <filesystem>

#include "process.h"
#include "inject.h"

// Console colors
enum Color {
	BLACK = 0,
	DARK_BLUE = 1,
	DARK_GREEN = 2,
	DARK_CYAN = 3,
	DARK_RED = 4,
	DARK_MAGENTA = 5,
	DARK_YELLOW = 6,
	GRAY = 7,
	DARK_GRAY = 8,
	BLUE = 9,
	GREEN = 10,
	CYAN = 11,
	RED = 12,
	MAGENTA = 13,
	YELLOW = 14,
	WHITE = 15
};

HANDLE hConsole;

void setColor(int color) {
	SetConsoleTextAttribute(hConsole, color);
}

void setupConsole() {
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	
	// Enable UTF-8 output
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	
	// Set window title
	SetConsoleTitleW(L"Cobble");
	
	// Resize console window to be compact
	SMALL_RECT windowSize = { 0, 0, 59, 18 };
	SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
	
	COORD bufferSize = { 60, 19 };
	SetConsoleScreenBufferSize(hConsole, bufferSize);
	
	// Hide cursor
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(hConsole, &cursorInfo);
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(hConsole, &cursorInfo);
}

void printBanner() {
	std::cout << "\n";
	setColor(MAGENTA);
	std::cout << "         ______      __    __    __   \n";
	std::cout << "        / ____/___  / /_  / /_  / /__ \n";
	std::cout << "       / /   / __ \\/ __ \\/ __ \\/ / _ \\\n";
	std::cout << "      / /___/ /_/ / /_/ / /_/ / /  __/\n";
	std::cout << "      \\____/\\____/_.___/_.___/_/\\___/ \n";
	setColor(DARK_GRAY);
	std::cout << "               Ghost Client v0.8\n";
	setColor(GRAY);
	std::cout << "\n";
}

void printLine() {
	setColor(DARK_GRAY);
	std::cout << "   ";
	for (int i = 0; i < 44; i++) std::cout << "-";
	std::cout << "\n";
	setColor(GRAY);
}

void printStatus(const std::string& msg, int color = GRAY) {
	setColor(DARK_GRAY);
	std::cout << "    [";
	setColor(color);
	if (color == GREEN) std::cout << "+";
	else if (color == RED) std::cout << "!";
	else if (color == CYAN) std::cout << "*";
	else if (color == YELLOW) std::cout << "~";
	else std::cout << " ";
	setColor(DARK_GRAY);
	std::cout << "] ";
	setColor(GRAY);
	std::cout << msg << "\n";
}

void printSuccess(const std::string& msg) { printStatus(msg, GREEN); }
void printError(const std::string& msg) { printStatus(msg, RED); }
void printInfo(const std::string& msg) { printStatus(msg, CYAN); }
void printWait(const std::string& msg) { printStatus(msg, YELLOW); }

std::wstring getDllPath() {
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	
	std::filesystem::path exePath(path);
	std::filesystem::path dllPath = exePath.parent_path() / L"Cobble.dll";
	
	return dllPath.wstring();
}

int wmain(int argc, wchar_t* argv[]) {
	setupConsole();
	printBanner();
	printLine();
	
	// Check if DLL exists
	std::wstring dllPath = getDllPath();
	
	if (!std::filesystem::exists(dllPath)) {
		printError("Cobble.dll not found!");
		printInfo("Place Cobble.dll next to this exe.");
		std::cout << "\n    Press any key to exit...";
		std::cin.get();
		return 1;
	}
	
	printSuccess("Found Cobble.dll");
	
	// Find Lunar Client
	printWait("Searching for Minecraft...");
	
	ProcessInfo lunar = findLunarClient();
	
	if (lunar.pid == 0) {
		printError("Minecraft not found!");
		printInfo("Launch Minecraft via Lunar Client.");
		std::cout << "\n    Press any key to exit...";
		std::cin.get();
		return 1;
	}
	
	// Convert window title to narrow string for display
	char narrowTitle[128];
	WideCharToMultiByte(CP_UTF8, 0, lunar.windowTitle.c_str(), -1, narrowTitle, 128, nullptr, nullptr);
	printSuccess(std::string("Found: ") + narrowTitle);
	
	// Inject DLL
	printWait("Injecting...");
	
	if (!injectDll(lunar.pid, dllPath)) {
		printError("Injection failed!");
		printInfo("Try running as Administrator.");
		std::cout << "\n    Press any key to exit...";
		std::cin.get();
		return 1;
	}
	
	printSuccess("Injection successful!");
	
	printLine();
	std::cout << "\n";
	setColor(GREEN);
	std::cout << "    Cobble is now active!\n";
	setColor(DARK_GRAY);
	std::cout << "    Open config/index.html to configure.\n";
	std::cout << "    Use the Eject button in web UI to unload.\n";
	setColor(GRAY);
	std::cout << "\n";
	
	// Auto-close after 3 seconds
	for (int i = 3; i > 0; i--) {
		std::cout << "\r    Closing in " << i << "...  ";
		Sleep(1000);
	}
	
	return 0;
}
