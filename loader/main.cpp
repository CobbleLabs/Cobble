#include <process.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <atomic>
#include <vector>
#include <cmath>

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

// ============================================================================
// CONFIG & GLOBALS
// ============================================================================

#define LICENSE_SERVER_PRIMARY "http://syra.club:8080"
#define LICENSE_SERVER_FALLBACK "http://172.235.136.152:8080"
#define LICENSE_CHECK_ENABLED true

std::atomic<bool> g_SecurityViolation(false);
std::atomic<ULONGLONG> g_Heartbeat1(0);
std::atomic<ULONGLONG> g_Heartbeat2(0);

HANDLE hConsole;
bool vtEnabled = false;

// ============================================================================
// CONSOLE UX HELPERS
// ============================================================================

#define VT_RESET "\x1b[0m"
#define VT_HIDE_CURSOR "\x1b[?25l"
#define VT_SHOW_CURSOR "\x1b[?25h"

// Colors
struct RGB { int r, g, b; };
// Syra Orange Gradients
const RGB COL_START = { 255, 165, 0 };   // Orange
const RGB COL_END   = { 255, 69, 0 };    // Red-Orange

void setupConsole() {
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	SetConsoleTitleW(L"Cobble Client");
	
	// Enable VT
	DWORD mode = 0;
	GetConsoleMode(hConsole, &mode);
	mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hConsole, mode);
	vtEnabled = true; // Assume true for this customized UX
	
	// Hide Cursor
	std::cout << VT_HIDE_CURSOR;
	
	// Fix Window Size (No Resize)
	HWND hWnd = GetConsoleWindow();
	LONG style = GetWindowLong(hWnd, GWL_STYLE);
	style &= ~(WS_MAXIMIZEBOX | WS_SIZEBOX); // Disable maximize and resize
	SetWindowLong(hWnd, GWL_STYLE, style);
	
	// Set Size
	SMALL_RECT windowSize = { 0, 0, 89, 24 }; // 90x25
	SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
	COORD bufferSize = { 90, 25 };
	SetConsoleScreenBufferSize(hConsole, bufferSize);
    
    // Center Window on Screen
    RECT rect;
    GetWindowRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    MoveWindow(hWnd, (screenWidth - width) / 2, (screenHeight - height) / 2, width, height, TRUE);
}

int getConsoleWidth() { return 90; }

// Utils
std::string rgbToVt(RGB c) {
    return "\x1b[38;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
}

RGB lerpColor(RGB s, RGB e, float t) {
    return {
        (int)(s.r + (e.r - s.r) * t),
        (int)(s.g + (e.g - s.g) * t),
        (int)(s.b + (e.b - s.b) * t)
    };
}

void printCentered(const std::string& msg, std::string colorVt = "") {
	int padding = (getConsoleWidth() - (int)msg.length()) / 2;
	if (padding < 0) padding = 0;
	std::cout << "\r" << std::string(padding, ' ') << colorVt << msg << VT_RESET << "\n";
}

void printGradientText(const std::string& text) {
    // 1. Count visual characters (simplified UTF-8 check)
    size_t charCount = 0;
    for (size_t i = 0; i < text.length(); ) {
        unsigned char c = (unsigned char)text[i];
        int len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        i += len;
        charCount++;
    }

    int width = getConsoleWidth();
    int padding = (width - (int)charCount) / 2;
    if (padding < 0) padding = 0;

    std::cout << std::string(padding, ' ');
    
    // 2. Print loop
    size_t current = 0;
    for (size_t i = 0; i < text.length(); ) {
        unsigned char c = (unsigned char)text[i];
        int len = 1; // Default to 1 byte
        
        // Determine byte length of UTF-8 char
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        
        // Safety check to prevent reading past end
        if (i + len > text.length()) len = 1;

        // Calculate color for this character index
        float t = (float)current / (float)charCount;
        RGB color = lerpColor(COL_START, COL_END, t);
        
        // Print Color Codes + Full Multi-byte Character
        std::cout << rgbToVt(color);
        for (int k = 0; k < len; k++) std::cout << text[i + k];
        
        i += len;
        current++;
    }
    std::cout << VT_RESET << "\n";
}

void printBanner() {
    std::cout << "\n\n";
    // Using explicit UTF-8 Hex sequences to prevent encoding issues
    // ░ = \xE2\x96\x91
    // █ = \xE2\x96\x88
    
    const char* art[] = {
        "  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88             \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88        \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88        \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88            ",
        " \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88   \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88            \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88        \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88        \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88            ",
        "\xE2\x96\x91\xE2\x96\x88\xE2\x96\x88         \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  ",
        "\xE2\x96\x91\xE2\x96\x88\xE2\x96\x88        \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 ",
        "\xE2\x96\x91\xE2\x96\x88\xE2\x96\x88        \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88 ",
        " \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88   \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88    \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88   \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88   \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88 \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88        ",
        "  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88   \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88  \xE2\x96\x91\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88  "
    };
    
    for (const char* line : art) {
        printGradientText(line);
    }

    std::cout << "\n";
    printCentered("v" + std::string(COBBLE_VERSION), "\x1b[90m"); // Dark Gray
    std::cout << "\n";
}

// 4. Loading Bar with Personality
void showSmartLoading(int durationMs) {
    int width = getConsoleWidth();
    int barLen = 30;
    int totalSteps = 50; // Resolution
    int sleepPerStep = durationMs / totalSteps;
    
    const char* stages[] = {
        "Initializing security...",
        "Verifying integrity...",
        "Loading components...",
        "Preparing environment..."
    };
    
    // Bar Gradient Colors
    RGB barStart = { 80, 80, 80 }; // Gray
    RGB barEnd = COL_START;
    
    for (int i = 0; i <= totalSteps; i++) {
        float pct = (float)i / totalSteps;
        
        // Pick Stage text
        int stageIdx = (int)(pct * 3.99f); 
        std::string currentStage = stages[stageIdx];
        
        // Print Stage Text Centered
        std::cout << "\r" << "\x1b[K"; // Clear line
        printCentered(currentStage, "\x1b[37m"); // White
        
        // Draw Bar
        int filled = (int)(pct * barLen);
        int padding = (width - barLen) / 2; // Simple bar (no percentages/brackets)
        
        std::cout << std::string(padding, ' ');
        
        // Draw filled part with gradient
        for(int b=0; b<filled; b++) {
            float t = (float)b / barLen;
            std::cout << rgbToVt(lerpColor(barStart, COL_END, t)) << "━";
        }
        // Draw empty part
        std::cout << "\x1b[90m"; // Dark Gray
        for(int b=filled; b<barLen; b++) std::cout << "━";
        std::cout << VT_RESET;
        
        // Move cursor back up 1 line to stage text for next update
        // We print text line, then bar line. To update text, go up 1 line
        std::cout << "\x1b[A"; 
        
        Sleep(sleepPerStep);
    }
    std::cout << "\n\n\n"; // Push down past the animation area
}

// 6. Elegant Error
void printElegantError(const std::string& msg, const std::string& detail = "") {
    printCentered(msg, "\x1b[31m"); // Red
    if (!detail.empty()) {
        printCentered(detail, "\x1b[90m"); // Gray detail
    }
    // Subtle beep for error
    Beep(200, 300); 
}

void printSearchSpinner(const std::string& msg) {
    const char spinners[] = { '|', '/', '-', '\\' };
    static int spinIdx = 0;
    
    std::string fullMsg = msg + " " + spinners[spinIdx];
    spinIdx = (spinIdx + 1) % 4;
    
    // Clear line and print centered
    std::cout << "\r" << "\x1b[K";
    printCentered(fullMsg, "\x1b[90m"); // Gray
    // Move up because printCentered adds newline
    std::cout << "\x1b[A"; 
}

// ============================================================================
// SECURITY THREADS
// ============================================================================

DWORD WINAPI SecurityThread1(LPVOID lpParam) {
	while (!g_SecurityViolation) {
		if (Security::IsBeingDebugged()) { g_SecurityViolation = true; break; }
		g_Heartbeat1 = GetTickCount64();
		Sleep(500);
	}
	return 0;
}

DWORD WINAPI SecurityThread2(LPVOID lpParam) {
	while (!g_SecurityViolation) {
		if (Security::HasAPIHooks()) { g_SecurityViolation = true; break; }
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
	if (Security::IsBeingDebugged()) return 1;
	Security::Initialize();
	
	HANDLE hThread1 = CreateThread(NULL, 0, SecurityThread1, NULL, 0, NULL);
	HANDLE hThread2 = CreateThread(NULL, 0, SecurityThread2, NULL, 0, NULL);
	if (hThread1) CloseHandle(hThread1);
	if (hThread2) CloseHandle(hThread2);
	Sleep(100);
	
	// ========== PHASE 2: UI INIT ==========
	setupConsole();
	printBanner();
	showSmartLoading(1500);

	// ========== PHASE 3: LICENSE ==========
#if LICENSE_CHECK_ENABLED
	// Silent verify
	std::string hwid = HWID::Generate();
	bool licenseValid = false;
	
	if (License::Verify(LICENSE_SERVER_PRIMARY)) licenseValid = true;
	else if (License::Verify(LICENSE_SERVER_FALLBACK)) licenseValid = true;

	if (!licenseValid) {
        std::cout << "\n";
        printElegantError("License verification failed", License::GetLastError());
        std::cout << "\n";
        
        printCentered("HWID copied to clipboard", "\x1b[90m");
		
		if (OpenClipboard(nullptr)) {
			EmptyClipboard();
			HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, hwid.size() + 1);
			if (hMem) {
				memcpy(GlobalLock(hMem), hwid.c_str(), hwid.size() + 1);
				GlobalUnlock(hMem);
				SetClipboardData(CF_TEXT, hMem);
			}
			CloseClipboard();
		}
		Sleep(4000);
		return 1;
	}
#endif
	
	// ========== PHASE 4: CHECK RESOURCES ==========
	HMODULE hModule = GetModuleHandle(nullptr);
	HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDR_COBBLE_DLL), RT_RCDATA);
	if (!hRes) { 
        printElegantError("Corruption detected", "Missing internal resources");
        Sleep(3000); return 1; 
    }
    
	HGLOBAL hData = LoadResource(hModule, hRes);
	BYTE* dllData = (BYTE*)LockResource(hData);
	DWORD dllSize = SizeofResource(hModule, hRes);
	
#if DLL_HASH_ENABLED
	if (dllSize != DLL_EXPECTED_SIZE || 
        !Hash::VerifyChunked(dllData, dllSize, DLL_HASH_CHUNK1, DLL_HASH_CHUNK2, DLL_HASH_CHUNK3, DLL_HASH_CHUNK4)) {
		printElegantError("Integrity check failed", "Possible tampering detected");
		Sleep(3000); return 1;
	}
#endif
	
	// ========== PHASE 5: WAIT FOR TARGET ==========
	if (g_SecurityViolation) return 0;
	
	ProcessInfo lunar;
    while(true) {
        lunar = findLunarClient();
        if (lunar.pid != 0) break;
        printSearchSpinner("Waiting for Minecraft");
        Sleep(100);
    }
    
    // Clear spinner line: move down to new line
    std::cout << "\n\n";
    
	if (isDllInjected(lunar.pid, L"Cobble.dll")) {
        printElegantError("Already injected");
		Sleep(2000); return 0;
	}
	
	// ========== PHASE 6: INJECTION ==========
	if (g_SecurityViolation) return 0;
    
    printCentered("Injecting...", "\x1b[90m");
	
	wchar_t tempPath[MAX_PATH];
	GetTempPathW(MAX_PATH, tempPath);
	DWORD tick = GetTickCount();
	wchar_t uniqueName[64];
	swprintf_s(uniqueName, L"u%08X.tmp", tick); // Even shorter/subtler name
	std::wstring dllTempPath = std::wstring(tempPath) + uniqueName;
	
    // Write
	DeleteFileW(dllTempPath.c_str());
	HANDLE hFile = CreateFileW(dllTempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) { printElegantError("Write Error"); Sleep(2000); return 1; }
	
	DWORD written;
	WriteFile(hFile, dllData, dllSize, &written, nullptr);
	CloseHandle(hFile);
	
    // Inject
	if (!injectDllLoadLibrary(lunar.pid, dllTempPath)) {
        printElegantError("Injection failed", "Try running as Administrator");
		DeleteFileW(dllTempPath.c_str());
		Sleep(4000); return 1;
	}
	
	Sleep(500); // Wait for thread
	DeleteFileW(dllTempPath.c_str());

    // ========== SUCCESS ==========
    // Clear injection text
    std::cout << "\r" << "\x1b[K" << "\x1b[A" << "\x1b[K"; 
    
    std::cout << "\n";
	printCentered("Active", "\x1b[32m"); // Green
    
    // Subtle Success Beep
    Beep(523, 100); // C5 - Short and crisp
    
	Sleep(1500);
    
    // Fade out / Close
	return 0;
}
