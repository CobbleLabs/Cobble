#include <process.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <atomic>
#include <vector>

#include "process.h"
#include "inject.h"
#include "resource.h"
#include "security.h"
#include "xor.h"
#include "license.h"
#include "hwid.h"
#include "version.h"
#include "src/util/hash.h"
#include "src/util/dll_hash.h"

// User info structure for shared memory
struct UserInfoData {
    char username[64];
    int daysRemaining;
};

// ============================================================================
// LICENSE CONFIG
// ============================================================================

#define LICENSE_SERVER_PRIMARY "http://syra.club:8080"
#define LICENSE_SERVER_FALLBACK "http://172.235.136.152:8080"
#define LICENSE_CHECK_ENABLED true

// ============================================================================
// GLOBALS
// ============================================================================

std::atomic<bool> g_SecurityViolation(false);
std::atomic<ULONGLONG> g_Heartbeat1(0);
std::atomic<ULONGLONG> g_Heartbeat2(0);

// ============================================================================
// CONSOLE HELPERS
// ============================================================================

enum Color {
	BLACK = 0, DARK_BLUE = 1, DARK_GREEN = 2, DARK_CYAN = 3,
	DARK_RED = 4, DARK_MAGENTA = 5, DARK_YELLOW = 6, GRAY = 7,
	DARK_GRAY = 8, BLUE = 9, GREEN = 10, CYAN = 11,
	RED = 12, MAGENTA = 13, YELLOW = 14, WHITE = 15
};

HANDLE hConsole;

void setColor(int color) {
	SetConsoleTextAttribute(hConsole, color);
}

void setupConsole() {
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	SetConsoleTitleW(L"Cobble");
	
	SMALL_RECT windowSize = { 0, 0, 59, 18 };
	SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
	
	COORD bufferSize = { 60, 19 };
	SetConsoleScreenBufferSize(hConsole, bufferSize);
	
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(hConsole, &cursorInfo);
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(hConsole, &cursorInfo);
}

void printBanner() {
	std::cout << "\n";
	setColor(MAGENTA);
	std::cout << _xor_("         ______      __    __    __   \n");
	std::cout << _xor_("        / ____/___  / /_  / /_  / /__ \n");
	std::cout << _xor_("       / /   / __ \\/ __ \\/ __ \\/ / _ \\\n");
	std::cout << _xor_("      / /___/ /_/ / /_/ / /_/ / /  __/\n");
	std::cout << _xor_("      \\____/\\____/_.___/_.___/_/\\___/ \n");
	setColor(DARK_GRAY);
	std::cout << _xor_("               Ghost Client v") << COBBLE_VERSION << "\n";
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

// ============================================================================
// SECURITY THREADS
// ============================================================================

DWORD WINAPI SecurityThread1(LPVOID lpParam) {
	while (!g_SecurityViolation) {
		if (Security::IsBeingDebugged()) {
			g_SecurityViolation = true;
			break;
		}
		g_Heartbeat1 = GetTickCount64();
		Sleep(500);
	}
	return 0;
}

DWORD WINAPI SecurityThread2(LPVOID lpParam) {
	while (!g_SecurityViolation) {
		if (Security::HasAPIHooks()) {
			g_SecurityViolation = true;
			break;
		}
		g_Heartbeat2 = GetTickCount64();
		Sleep(500);
	}
	return 0;
}

// ============================================================================
// MAIN
// ============================================================================

int wmain(int argc, wchar_t* argv[]) {
	// ========== PHASE 1: SECURITY ==========
	
	if (Security::IsBeingDebugged()) {
		MessageBoxW(NULL, 
			L"Cobble has detected debugging tools.\n\nPlease close any debuggers and try again.", 
			L"Cobble - Security",
			MB_ICONERROR | MB_OK);
		return 1;
	}
	
	Security::Initialize();
	
	HANDLE hThread1 = CreateThread(NULL, 0, SecurityThread1, NULL, 0, NULL);
	HANDLE hThread2 = CreateThread(NULL, 0, SecurityThread2, NULL, 0, NULL);
	if (hThread1) CloseHandle(hThread1);
	if (hThread2) CloseHandle(hThread2);
	
	Sleep(100);
	
	// ========== PHASE 2: UI ==========
	
	setupConsole();
	printBanner();
	printLine();
	
	// ========== PHASE 3: LICENSE ==========
	
#if LICENSE_CHECK_ENABLED
	printWait(_xor_("Verifying license..."));
	
	std::string hwid = HWID::Generate();
	bool licenseValid = false;
	
	// Try primary
	if (License::Verify(LICENSE_SERVER_PRIMARY)) {
		licenseValid = true;
	} 
	// Try fallback
	else {
		printInfo(_xor_("Retrying connection..."));
		if (License::Verify(LICENSE_SERVER_FALLBACK)) {
			licenseValid = true;
		}
	}

	if (!licenseValid) {
		printError(_xor_("License verification failed!"));
		printInfo(License::GetLastError().c_str());
		
		std::cout << "\n";
		setColor(YELLOW);
		std::cout << _xor_("    Your HWID: ") << hwid << "\n";
		setColor(GRAY);
		
		if (OpenClipboard(nullptr)) {
			EmptyClipboard();
			HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, hwid.size() + 1);
			if (hMem) {
				memcpy(GlobalLock(hMem), hwid.c_str(), hwid.size() + 1);
				GlobalUnlock(hMem);
				SetClipboardData(CF_TEXT, hMem);
			}
			CloseClipboard();
			printSuccess(_xor_("HWID copied to clipboard!"));
		}
		
		printInfo(_xor_("Send your HWID to an admin to get access."));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	
	printSuccess(_xor_("License verified!"));
#endif
	
	// ========== PHASE 4: DLL ==========
	
	HMODULE hModule = GetModuleHandle(nullptr);
	HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDR_COBBLE_DLL), RT_RCDATA);
	
	BYTE* dllData = nullptr;
	DWORD dllSize = 0;
	
	if (hRes) {
		HGLOBAL hData = LoadResource(hModule, hRes);
		if (hData) {
			dllData = (BYTE*)LockResource(hData);
			dllSize = SizeofResource(hModule, hRes);
		}
	}
	
	if (!dllData) {
		printError(_xor_("Cobble.dll not found in resources!"));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	
#if DLL_HASH_ENABLED
	printWait(_xor_("Verifying DLL integrity..."));
	
	// Verify buffer from RCDATA (never from disk - prevents swap attacks)
	// Size check first (fast fail)
	if (dllSize != DLL_EXPECTED_SIZE) {
		printError(_xor_("DLL size mismatch!"));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	
	// Hash verification using chunked comparison (non-linear order for obfuscation)
	if (!Hash::VerifyChunked(dllData, dllSize, 
		DLL_HASH_CHUNK1, DLL_HASH_CHUNK2, DLL_HASH_CHUNK3, DLL_HASH_CHUNK4)) {
		printError(_xor_("DLL integrity check failed!"));
		printInfo(_xor_("The DLL may be corrupted or tampered with."));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	printSuccess(_xor_("DLL integrity verified!"));
#endif
	
	// ========== PHASE 5: TARGET ==========
	
	if (g_SecurityViolation) return 0;
	
	printWait(_xor_("Searching for Minecraft..."));
	
	ProcessInfo lunar = findLunarClient();
	
	if (lunar.pid == 0) {
		printError(_xor_("Minecraft not found!"));
		printInfo(_xor_("Launch Minecraft via Lunar Client."));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	
	char narrowTitle[128];
	WideCharToMultiByte(CP_UTF8, 0, lunar.windowTitle.c_str(), -1, narrowTitle, 128, nullptr, nullptr);
	printSuccess(std::string(_xor_("Found: ")) + narrowTitle);
	
	if (isDllInjected(lunar.pid, L"Cobble.dll")) {
		printWait(_xor_("Cobble is already injected!"));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 0;
	}
	
	// ========== PHASE 6: INJECT ==========
	
	if (g_SecurityViolation) return 0;
	
	ULONGLONG now = GetTickCount64();
	if ((now - g_Heartbeat1 > 5000) || (now - g_Heartbeat2 > 5000)) {
		return 0;
	}
	
	printWait(_xor_("Injecting (LoadLibrary)..."));
	
	// Write DLL to temp file for LoadLibrary (unique name to avoid conflicts)
	wchar_t tempPath[MAX_PATH];
	GetTempPathW(MAX_PATH, tempPath);
	
	// Generate unique filename using timestamp
	DWORD tick = GetTickCount();
	wchar_t uniqueName[64];
	swprintf_s(uniqueName, L"Cobble_%08X.dll", tick);
	std::wstring dllTempPath = std::wstring(tempPath) + uniqueName;
	
	// Try to delete old temp file if exists
	DeleteFileW(dllTempPath.c_str());
	
	HANDLE hFile = CreateFileW(dllTempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		char errBuf[128];
		snprintf(errBuf, sizeof(errBuf), "Failed to write temp DLL (error %lu)", err);
		printError(errBuf);
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	
	DWORD written;
	if (!WriteFile(hFile, dllData, dllSize, &written, nullptr) || written != dllSize) {
		CloseHandle(hFile);
		DeleteFileW(dllTempPath.c_str());
		printError(_xor_("Failed to write DLL data!"));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	CloseHandle(hFile);
	
	if (!injectDllLoadLibrary(lunar.pid, dllTempPath)) {
		printError(_xor_("Injection failed!"));
		DeleteFileW(dllTempPath.c_str());
		printInfo(_xor_("Try running as Administrator."));
		std::cout << _xor_("\n    Press any key to exit...");
		std::cin.get();
		return 1;
	}
	
	// Delete temp file after a delay
	Sleep(2000);
	DeleteFileW(dllTempPath.c_str());
	
	printSuccess(_xor_("Injection successful!"));
	
	printLine();
	std::cout << "\n";
	setColor(GREEN);
	std::cout << _xor_("    Cobble is now active!\n");
	setColor(DARK_GRAY);
	std::cout << _xor_("    Use the Eject button in web UI to unload.\n");
	setColor(GRAY);
	std::cout << "\n";
	
	for (int i = 3; i > 0; i--) {
		if (g_SecurityViolation) return 0;
		std::cout << "\r    " << _xor_("Closing in ") << i << "...  ";
		Sleep(1000);
	}
	
	return 0;
}
