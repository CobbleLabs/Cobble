/**
 * Temp File Monitor - Educational Tool
 * 
 * Demonstrates how attackers capture DLLs written to temp directories.
 * This monitors %TEMP% for new files and copies them before they're deleted.
 * 
 * Compile: cl /EHsc temp_monitor.cpp /link /out:temp_monitor.exe
 */

#include <Windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

void CopyFileToCapture(const std::wstring& sourcePath) {
    // Create capture directory
    fs::path captureDir = fs::current_path() / "captured";
    fs::create_directories(captureDir);
    
    // Get filename
    fs::path source(sourcePath);
    std::wstring filename = source.filename().wstring();
    
    // Add timestamp to avoid overwrites
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[64];
    swprintf_s(timestamp, L"%02d%02d%02d_%02d%02d%02d_", 
        st.wYear % 100, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    
    fs::path destPath = captureDir / (std::wstring(timestamp) + filename);
    
    try {
        // Wait a bit for file to be fully written
        Sleep(50);
        
        // Copy the file
        fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing);
        
        // Check if it's a PE file (DLL/EXE)
        std::ifstream file(destPath, std::ios::binary);
        char magic[2] = {0};
        file.read(magic, 2);
        file.close();
        
        bool isPE = (magic[0] == 'M' && magic[1] == 'Z');
        
        std::wcout << L"[CAPTURED] " << destPath.filename().wstring();
        if (isPE) {
            std::wcout << L" << PE FILE DETECTED (DLL/EXE)!";
        }
        std::wcout << std::endl;
        
    } catch (const std::exception& e) {
        // File might be locked or already deleted
    }
}

int wmain() {
    std::wcout << L"========================================\n";
    std::wcout << L"     Temp File Monitor - Cobble Tool    \n";
    std::wcout << L"========================================\n\n";
    
    // Get temp path
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    
    std::wcout << L"[*] Monitoring: " << tempPath << L"\n";
    std::wcout << L"[*] Captured files will be saved to: ./captured/\n";
    std::wcout << L"[*] Press Ctrl+C to stop\n\n";
    
    // Create directory change notification handle
    HANDLE hDir = FindFirstChangeNotificationW(
        tempPath,
        FALSE,  // Don't watch subdirectories
        FILE_NOTIFY_CHANGE_FILE_NAME  // Watch for new files
    );
    
    if (hDir == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[-] Failed to monitor directory. Error: " << GetLastError() << std::endl;
        return 1;
    }
    
    // Alternative: Use ReadDirectoryChangesW for more detailed info
    HANDLE hDirRead = CreateFileW(
        tempPath,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    
    if (hDirRead == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[-] Failed to open directory for reading. Error: " << GetLastError() << std::endl;
        FindCloseChangeNotification(hDir);
        return 1;
    }
    
    BYTE buffer[4096];
    DWORD bytesReturned;
    
    std::wcout << L"[+] Monitoring started. Waiting for files...\n\n";
    
    while (true) {
        if (ReadDirectoryChangesW(
            hDirRead,
            buffer,
            sizeof(buffer),
            FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE,
            &bytesReturned,
            NULL,
            NULL
        )) {
            FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)buffer;
            
            do {
                // Get filename
                std::wstring filename(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
                std::wstring fullPath = std::wstring(tempPath) + filename;
                
                if (fni->Action == FILE_ACTION_ADDED || fni->Action == FILE_ACTION_MODIFIED) {
                    // Filter: capture .dll, .tmp, .exe files
                    std::wstring ext = fs::path(filename).extension().wstring();
                    
                    // Convert to lowercase for comparison
                    for (auto& c : ext) c = towlower(c);
                    
                    if (ext == L".dll" || ext == L".tmp" || ext == L".exe" || 
                        ext == L".bin" || ext == L".dat") {
                        std::wcout << L"[!] New file detected: " << filename << std::endl;
                        CopyFileToCapture(fullPath);
                    }
                }
                
                // Move to next entry
                if (fni->NextEntryOffset == 0) break;
                fni = (FILE_NOTIFY_INFORMATION*)((BYTE*)fni + fni->NextEntryOffset);
                
            } while (true);
        }
    }
    
    CloseHandle(hDirRead);
    FindCloseChangeNotification(hDir);
    
    return 0;
}
