#include <process.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <atomic>
#include <vector>
#include <cmath>
#include <conio.h>
#include <fstream>
#include <ShlObj.h>

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
#include "src/update/updater.h"
#include "src/update/download.h"

// ============================================================================
// CONFIG & GLOBALS
// ============================================================================

// Server configuration
inline const char* getServerPrimary() { return _xor_("syra.club"); }
inline const char* getLicenseServerPrimary() { return _xor_("https://syra.club"); }

#define SERVER_PORT_HTTPS 443
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
// Orange Gradients
const RGB COL_START = { 255, 165, 0 };   // Orange
const RGB COL_END   = { 255, 69, 0 };    // Red-Orange

// DPI-Adaptive Console Dimensions
int getScaledConsoleWidth() {
	HDC screen = GetDC(NULL);
	int dpi = GetDeviceCaps(screen, LOGPIXELSX);
	ReleaseDC(NULL, screen);
	
	// Adaptive sizing based on DPI
	if (dpi >= 144) return 70;  // High DPI (150%+)
	if (dpi >= 120) return 77;  // Medium DPI (125%)
	return 84;                  // Normal DPI (100%)
}

int getScaledConsoleHeight() {
	HDC screen = GetDC(NULL);
	int dpi = GetDeviceCaps(screen, LOGPIXELSX);
	ReleaseDC(NULL, screen);
	
	if (dpi >= 144) return 24;  // High DPI
	if (dpi >= 120) return 26;  // Medium DPI
	return 28;                  // Normal DPI
}

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
	vtEnabled = true; 
	
	int CONSOLE_WIDTH = getScaledConsoleWidth();
	int CONSOLE_HEIGHT = getScaledConsoleHeight();
	
	// Force Resize via ANSI (Best for Windows Terminal / Win11)
	// \x1b[8;{rows};{cols}t
	std::cout << "\x1b[8;" << CONSOLE_HEIGHT << ";" << CONSOLE_WIDTH << "t";
	
	std::cout << VT_HIDE_CURSOR;
	
	HWND hWnd = GetConsoleWindow();
	LONG style = GetWindowLong(hWnd, GWL_STYLE);
	style &= ~(WS_MAXIMIZEBOX | WS_SIZEBOX);
	SetWindowLong(hWnd, GWL_STYLE, style);
	
	// Resize Strategy: MINIMIZE WINDOW -> SET BUFFER -> EXPAND WINDOW
	// This prevents "The parameter is incorrect" if the new buffer is smaller than the current window.
	
	// 1. Shrink window to avoid buffer constraint errors
	SMALL_RECT minWin = { 0, 0, 1, 1 };
	SetConsoleWindowInfo(hConsole, TRUE, &minWin);

	// 2. Set Buffer Size
	COORD bufferSize = { (SHORT)CONSOLE_WIDTH, (SHORT)CONSOLE_HEIGHT };
	if (!SetConsoleScreenBufferSize(hConsole, bufferSize))
	{
		// Fallback: try smaller size if first attempt fails
		bufferSize = { 80, 25 };
		SetConsoleScreenBufferSize(hConsole, bufferSize);
		CONSOLE_WIDTH = 80;
		CONSOLE_HEIGHT = 25;
	}

	// 3. Set Window Size
	SMALL_RECT windowSize = { 0, 0, (SHORT)(CONSOLE_WIDTH - 1), (SHORT)(CONSOLE_HEIGHT - 1) };
	SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
	
	// 4. Fix buffer again just in case window resize forced a change
	SetConsoleScreenBufferSize(hConsole, bufferSize);
	
	// 5. Verify the resize worked
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo(hConsole, &csbi))
	{
		int actualWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		int actualHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
		
		// If verification failed significantly, we're on a system with incompatible settings
		// Just proceed - the content will adapt
	}
    
    // Center logic
    RECT rect;
    GetWindowRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    MoveWindow(hWnd, (screenWidth - width) / 2, (screenHeight - height) / 2, width, height, TRUE);
}

int getConsoleWidth() {
	static int width = getScaledConsoleWidth();
	return width;
}

// Utils
std::string rgbToVt(RGB c) {
    return "\x1b[38;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
}

std::string bgToVt(RGB c) {
    return "\x1b[48;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
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
    // Clear Screen (keep this as it ensures clean slate)
    std::cout << "\x1b[2J\x1b[H";
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

// 3.5 Download Progress Bar (similar style to showSmartLoading)
static size_t g_downloadTotal = 0;
static size_t g_downloadLast = 0;

void showDownloadProgress(size_t downloaded, size_t total) {
    // Throttle updates to avoid console spam
    if (downloaded - g_downloadLast < 4096 && downloaded != total) return;
    g_downloadLast = downloaded;
    
    if (total == 0) total = 1; // Avoid div by 0
    g_downloadTotal = total;
    
    int width = getConsoleWidth();
    int barLen = 30;
    float pct = (float)downloaded / total;
    if (pct > 1.0f) pct = 1.0f;
    
    // Partial block characters
    const char* partials[] = {
        " ",
        "\xE2\x96\x8F", // 1/8
        "\xE2\x96\x8E", // 2/8
        "\xE2\x96\x8D", // 3/8
        "\xE2\x96\x8C", // 4/8
        "\xE2\x96\x8B", // 5/8
        "\xE2\x96\x8A", // 6/8
        "\xE2\x96\x89", // 7/8
        "\xE2\x96\x88"  // 8/8 (Full)
    };
    
    // Colors
    RGB barStart = {255, 150, 50};  // Orange
    RGB barEnd = {80, 200, 120};    // Green
    RGB barBg = {40, 40, 50};
    
    // Calculate progress
    int totalSubSteps = barLen * 8;
    int steps = (int)(pct * totalSubSteps);
    int fullChars = steps / 8;
    int remainder = steps % 8;
    
    // Size display
    int downloadedKB = (int)(downloaded / 1024);
    int totalKB = (int)(total / 1024);
    char sizeStr[64];
    sprintf_s(sizeStr, "Downloading... %d / %d KB", downloadedKB, totalKB);
    
    // Clear and draw text
    std::cout << "\r" << "\x1b[K";
    printCentered(sizeStr, "\x1b[37m");
    
    // Draw bar
    int padding = (width - barLen) / 2;
    std::cout << std::string(padding, ' ');
    std::cout << bgToVt(barBg);
    
    for (int b = 0; b < fullChars; b++) {
        float t = (float)b / barLen;
        std::cout << rgbToVt(lerpColor(barStart, barEnd, t)) << "\xE2\x96\x88";
    }
    
    if (fullChars < barLen) {
        float t = (float)fullChars / barLen;
        std::cout << rgbToVt(lerpColor(barStart, barEnd, t)) << partials[remainder];
    }
    
    int occupied = fullChars + (fullChars < barLen ? 1 : 0);
    for (int b = occupied; b < barLen; b++) std::cout << " ";
    
    std::cout << VT_RESET;
    std::cout << "\x1b[A"; // Move cursor up
    std::cout.flush();
}

// 4. Loading Bar with Personality (Ultra Smooth + Solid Background)
void showSmartLoading(int durationMs) {
    int width = getConsoleWidth();
    int barLen = 30;
    
    // 8 Sub-steps per character for smooth filling
    const char* partials[] = {
        " ",
        "\xE2\x96\x8F", // 1/8
        "\xE2\x96\x8E", // 2/8
        "\xE2\x96\x8D", // 3/8
        "\xE2\x96\x8C", // 4/8
        "\xE2\x96\x8B", // 5/8
        "\xE2\x96\x8A", // 6/8
        "\xE2\x96\x89", // 7/8
        "\xE2\x96\x88"  // 8/8 (Full)
    };
    
    int totalSubSteps = barLen * 8;
    int sleepPerStep = durationMs / totalSubSteps;
    
    const char* stages[] = {
        "Initializing security...",
        "Verifying integrity...",
        "Loading components...",
        "Preparing environment..."
    };
    
    RGB barStart = { 255, 165, 0 }; // Orange (Start immediately with color)
    RGB barEnd = COL_START;        // Orange
    RGB barBg = { 25, 25, 25 };    // Dark Gray Background (Solid Track)
    
    // Initial print logic
    
    // PUSH DOWN: Add vertical space before the bar
    std::cout << "\n\n\n\n"; 
    
    std::cout << "\n"; // Space for text
    std::cout << "\n"; // Space for bar
    std::cout << "\x1b[2A"; // Move back up 2 lines
    
    for (int i = 0; i <= totalSubSteps; i++) {
        float pct = (float)i / totalSubSteps;
        
        // --- 1. Draw Text (Top Line) ---
        int stageIdx = (int)(pct * 3.99f); 
        std::string currentStage = stages[stageIdx];
        
        std::cout << "\r" << "\x1b[K"; // Clear line
        printCentered(currentStage, "\x1b[37m");
        
        // --- 2. Draw Bar (Bottom Line) ---
        int padding = (width - barLen) / 2;
        std::cout << std::string(padding, ' ');
        
        // Set solid background for the whole bar
        std::cout << bgToVt(barBg);
        
        int fullChars = i / 8;
        int remainder = i % 8;
        
        // Print Full Parts
        for(int b=0; b<fullChars; b++) {
            float t = (float)b / barLen;
            std::cout << rgbToVt(lerpColor(barStart, COL_END, t)) << "\xE2\x96\x88"; // Full Block
        }
        
        // Print Partial Part
        if (fullChars < barLen) {
            float t = (float)fullChars / barLen;
            // FG: Gradient, BG: barBg (inherited from start of line)
            std::cout << rgbToVt(lerpColor(barStart, COL_END, t)) << partials[remainder];
        }
        
        // Print Empty Parts (Solid Background)
        // Just print spaces - the background color handles the "Fill" look
        int occupied = fullChars + (fullChars < barLen ? 1 : 0);
        for(int b=occupied; b<barLen; b++) std::cout << " "; 
        
        std::cout << VT_RESET;
        
        // Move Cursor Up for next frame
        std::cout << "\x1b[A"; 
        
        if (sleepPerStep < 1) sleepPerStep = 1;
        Sleep(sleepPerStep);
    }
    std::cout << "\n\n\n";
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

// Elegant centered text input with visible typing
std::string getInputCentered(const std::string& prompt) {
    int width = getConsoleWidth();
    std::string fullPrompt = prompt + ": ";
    int leftPad = (width - (int)fullPrompt.length() - 20) / 2;
    if (leftPad < 2) leftPad = 2;
    
    std::cout << VT_SHOW_CURSOR;
    std::cout << std::string(leftPad, ' ') << "\x1b[37m" << fullPrompt << VT_RESET;
    
    std::string input;
    char c;
    while ((c = _getch()) != '\r' && c != '\n') {
        if (c == '\b' || c == 127) { // Backspace
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b";
            }
        } else if (c >= 32 && c < 127) { // Printable
            input += c;
            std::cout << c;
        }
    }
    std::cout << "\n" << VT_HIDE_CURSOR;
    return input;
}

// Secure password input (masked with dots)
std::string getPasswordCentered(const std::string& prompt) {
    int width = getConsoleWidth();
    std::string fullPrompt = prompt + ": ";
    int leftPad = (width - (int)fullPrompt.length() - 20) / 2;
    if (leftPad < 2) leftPad = 2;
    
    std::cout << VT_SHOW_CURSOR;
    std::cout << std::string(leftPad, ' ') << "\x1b[37m" << fullPrompt << VT_RESET;
    
    std::string password;
    char c;
    while ((c = _getch()) != '\r' && c != '\n') {
        if (c == '\b' || c == 127) { // Backspace
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b";
            }
        } else if (c >= 32 && c < 127) { // Printable
            password += c;
            std::cout << "\x1b[90m" << "\xE2\x80\xA2" << VT_RESET; // Bullet point
        }
    }
    std::cout << "\n" << VT_HIDE_CURSOR;
    return password;
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

// Helper for update mode - supports cross-drive moves
bool moveWithRetry(const std::string& src, const std::string& dst, int maxRetries = 10) {
    for (int i = 0; i < maxRetries; i++) {
        // Try MoveFileEx first (same drive, faster)
        if (MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        // If ERROR_NOT_SAME_DEVICE, use CopyFile + DeleteFile
        if (GetLastError() == ERROR_NOT_SAME_DEVICE) {
            if (CopyFileA(src.c_str(), dst.c_str(), FALSE)) {
                DeleteFileA(src.c_str());
                return true;
            }
        }
        Sleep(500);
    }
    return false;
}

int wmain(int argc, wchar_t* argv[]) {
	// ========== UPDATE MODE ==========
	// If called with --update-pid, we act as the updater (silent, no UI)
	for (int i = 1; i < argc; i++) {
		if (wcscmp(argv[i], L"--update-pid") == 0 && i + 5 < argc) {
			DWORD oldPid = (DWORD)_wtoi(argv[i + 1]);
			std::wstring oldPathW = argv[i + 3];
			std::wstring newPathW = argv[i + 5];
			
			char oldPathBuf[MAX_PATH], newPathBuf[MAX_PATH];
			WideCharToMultiByte(CP_UTF8, 0, oldPathW.c_str(), -1, oldPathBuf, MAX_PATH, nullptr, nullptr);
			WideCharToMultiByte(CP_UTF8, 0, newPathW.c_str(), -1, newPathBuf, MAX_PATH, nullptr, nullptr);
			std::string oldPath = oldPathBuf;
			std::string newPath = newPathBuf;
			std::string backupPath = oldPath + ".old";
			
			// Wait for old loader to exit
			HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, oldPid);
			if (hProcess) {
				WaitForSingleObject(hProcess, 30000);
				CloseHandle(hProcess);
			}
			Sleep(500);
			
			// Delete old backup if exists
			DeleteFileA(backupPath.c_str());
			
			// Backup old -> old.old
			if (!moveWithRetry(oldPath, backupPath)) {
				return 1;
			}
			
			// Move new -> old location
			if (!moveWithRetry(newPath, oldPath)) {
				moveWithRetry(backupPath, oldPath);
				return 2;
			}
			
			// Launch the updated loader
			STARTUPINFOA si = { sizeof(si) };
			PROCESS_INFORMATION pi;
			char cmdLine[MAX_PATH * 2];
			sprintf_s(cmdLine, "\"%s\"", oldPath.c_str());
			
			if (CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
				CloseHandle(pi.hThread);
				CloseHandle(pi.hProcess);
				Sleep(500);
				DeleteFileA(backupPath.c_str());
			} else {
				DeleteFileA(oldPath.c_str());
				moveWithRetry(backupPath, oldPath);
				return 3;
			}
			
			return 0;
		}
	}
	
	// ========== NORMAL LOADER MODE ==========
	
	// Update highest_seen for anti-downgrade (we successfully launched this version)
	Updater::setHighestSeen(COBBLE_VERSION);
	
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
	
	// ========== PHASE 2.5: UPDATE CHECK (BEFORE LOADING!) ==========
	{
		auto result = Updater::Check(COBBLE_VERSION, getServerPrimary(), SERVER_PORT_HTTPS);
		if (result.success) {
			if (result.blocked || result.updateAvailable) {
				// Force update if blocked (version too old) OR if update is marked as mandatory
				bool mustUpdate = result.blocked || result.mandatory;
				
				std::cout << "\n";
				if (mustUpdate) {
					printCentered("Update required!", "\x1b[33m");
					printCentered("Downloading...", "\x1b[90m");
				} else {
					printCentered("Update v" + result.manifest.version + " available!", "\x1b[33m");
					printCentered("Press Y to update, any other key to skip", "\x1b[90m");
					std::cout << VT_SHOW_CURSOR;
					
					char key = 0;
					while (!_kbhit()) Sleep(50);
					key = _getch();
					std::cout << VT_HIDE_CURSOR;
					
					if (key != 'y' && key != 'Y') {
						printBanner();
						goto skip_update;
					}
					printCentered("Downloading update...", "\x1b[90m");
				}
				
				std::string hwid = HWID::Generate();
				std::string tempDir = Updater::getTempDownloadDir();
				std::string newLoaderPath = tempDir + "\\CobbleLoader.exe";
				std::string downloadUrl = std::string(_xor_("https://")) + getServerPrimary() + _xor_("/api/update/loader");
				
				// Setup progress bar (need 2 lines: text + bar)
				g_downloadLast = 0;
				std::cout << "\n"; // Line for progress bar
				
				if (Updater::downloadAndVerify(downloadUrl, newLoaderPath, 
				                                result.manifest.loader.sha256, 
				                                result.manifest.loader.size, hwid,
				                                showDownloadProgress)) {
					std::cout << "\n"; // Finish bar
					if (Updater::spawnStub(Updater::getSelfPath(), newLoaderPath)) {
						std::cout << "\n";
						printCentered("Update downloaded!", "\x1b[32m");
						printCentered("Restarting...", "\x1b[90m");
						Sleep(800);
						return 0;
					}
				}
				
				Updater::cleanupTempDir(tempDir);
				if (mustUpdate) {
					printCentered("Update failed, please download manually", "\x1b[31m");
					Sleep(4000);
					return 1;
				} else {
					printCentered("Update failed, continuing...", "\x1b[31m");
					Sleep(1500);
				}
				printBanner();
			}
		}
	}
	skip_update:
	
	showSmartLoading(1500);

	// ========== PHASE 3: LICENSE ==========
#if LICENSE_CHECK_ENABLED
	bool licenseValid = false;
	
	// First, try silent HWID verification (for returning users)
	if (License::Verify(getLicenseServerPrimary())) {
		licenseValid = true;
	}
	
	// If verification failed and we need login (new device/first time)
	if (!licenseValid && License::RequiresLogin()) {
		std::cout << "\n";
		printCentered("Login required", "\x1b[33m");
		printCentered("Enter your Cobble account credentials", "\x1b[90m");
		std::cout << "\n";
		
		// Get credentials with elegant input
		std::string username = getInputCentered("Username");
		std::string password = getPasswordCentered("Password");
		
		if (username.empty() || password.empty()) {
			printCentered("Cancelled", "\x1b[31m");
			Sleep(1500);
			return 1;
		}
		
		std::cout << "\n";
		printCentered("Authenticating...", "\x1b[90m");
		
		if (License::LoginWithCredentials(getLicenseServerPrimary(), username, password)) {
			licenseValid = true;
			// Clear the authenticating message
			std::cout << "\r\x1b[K\x1b[A\x1b[K";
			printCentered("Device registered", "\x1b[32m");
			Sleep(800);
		} else {
			std::cout << "\r\x1b[K\x1b[A\x1b[K";
			std::cout << "\n";
			printCentered("Authentication failed", "\x1b[31m");
			printCentered(License::GetLastError(), "\x1b[90m");
			std::cout << "\n";
			printCentered("Press any key to exit...", "\x1b[90m");
			std::cout << VT_SHOW_CURSOR;
			while (!_kbhit()) Sleep(50);
			_getch();
			return 1;
		}
	}
	
	// Handle other failure cases (banned, expired, etc.)
	if (!licenseValid) {
		if (License::NeedsUpdate()) {
			printCentered("Version outdated", "\x1b[33m");
			Sleep(1000);
		} else {
			std::cout << "\n";
			printCentered("Access denied", "\x1b[31m");
			printCentered(License::GetLastError(), "\x1b[90m");
			std::cout << "\n";
			printCentered("Press any key to exit...", "\x1b[90m");
			std::cout << VT_SHOW_CURSOR;
			while (!_kbhit()) Sleep(50);
			_getch();
			return 1;
		}
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
