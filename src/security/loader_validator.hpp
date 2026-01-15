#pragma once

// ============================================================================
// LOADER VALIDATOR
// Multiple validation layers to detect injection without legitimate loader
// ============================================================================

#include <TlHelp32.h>
#include <Windows.h>
#include <string>


#include "audit_reporter.hpp"
#include "system_info.hpp"

namespace LoaderValidator {

// Expected loader signature in memory
constexpr char LOADER_SIGNATURE[] = "COBBLE_LOADER_V2";
constexpr DWORD LOADER_SIGNATURE_LEN = 16;

// Validation results
struct ValidationResult {
    bool passed = false;
    std::string failedValidator;
    std::string details;
};

// ============================================================================
// VALIDATOR 1: Shared Memory Exists
// Check that the loader's shared memory section exists
// ============================================================================
inline bool ValidateSharedMemory() {
    HANDLE hMapping = OpenFileMappingA(FILE_MAP_READ, FALSE, "Local\\CobbleInjection");
    if (hMapping == nullptr) {
        return false;
    }
    CloseHandle(hMapping);
    return true;
}

// ============================================================================
// VALIDATOR 2: Parent Process Check
// Verify parent is CobbleLoader.exe or expected process chain
// ============================================================================
inline bool ValidateParentProcess() {
    std::string parent = SystemInfo::GetParentProcessName();

    // Convert to lowercase for comparison
    std::string parentLower = parent;
    for (char& c : parentLower)
        c = tolower(c);

    // Expected: javaw.exe (since loader injects into java)
    // The loader process should have terminated or be our actual parent
    if (parentLower.find("javaw") != std::string::npos || parentLower.find("java") != std::string::npos) {
        return true;
    }

    // If injected via debugger/manual, parent might be explorer, cmd, etc.
    // These are suspicious
    if (parentLower.find("explorer") != std::string::npos || parentLower.find("cmd") != std::string::npos ||
        parentLower.find("powershell") != std::string::npos || parentLower.find("x64dbg") != std::string::npos ||
        parentLower.find("ida") != std::string::npos || parentLower.find("ollydbg") != std::string::npos ||
        parentLower.find("windbg") != std::string::npos) {
        return false;
    }

    return true;  // Unknown parent, allow but could log
}

// ============================================================================
// VALIDATOR 3: Loader Process Running
// Check if CobbleLoader.exe is running in system
// ============================================================================
inline bool ValidateLoaderProcessExists() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(pe32);

    bool found = false;
    if (Process32First(hSnapshot, &pe32)) {
        do {
            std::string procName = pe32.szExeFile;
            for (char& c : procName)
                c = tolower(c);

            if (procName.find("cobbleloader") != std::string::npos) {
                found = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return found;
}

// ============================================================================
// VALIDATOR 4: Injection Timing
// Check that injection happened within reasonable time of process start
// ============================================================================
inline bool ValidateInjectionTiming() {
    // Get current process creation time
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
        return false;
    }

    // Get current time
    FILETIME nowTime;
    GetSystemTimeAsFileTime(&nowTime);

    // Calculate difference in seconds
    ULARGE_INTEGER create, now;
    create.LowPart = createTime.dwLowDateTime;
    create.HighPart = createTime.dwHighDateTime;
    now.LowPart = nowTime.dwLowDateTime;
    now.HighPart = nowTime.dwHighDateTime;

    // 100-nanosecond intervals to seconds
    ULONGLONG diffSeconds = (now.QuadPart - create.QuadPart) / 10000000ULL;

    // If DLL is being loaded more than 15 minutes after process start, suspicious
    if (diffSeconds > 900) {
        return false;
    }

    return true;
}

// ============================================================================
// VALIDATOR 5: Anti-Debugger Check
// Simple check for attached debugger
// ============================================================================
inline bool ValidateNoDebugger() {
    if (IsDebuggerPresent()) {
        return false;
    }

    // Check remote debugger
    BOOL isRemoteDebugger = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &isRemoteDebugger);
    if (isRemoteDebugger) {
        return false;
    }

    return true;
}

// ============================================================================
// VALIDATOR 6: Module List Check
// Check for suspicious modules loaded (injection tools)
// ============================================================================
inline bool ValidateNoSuspiciousModules() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return true;  // Can't check, allow
    }

    MODULEENTRY32 me32;
    me32.dwSize = sizeof(me32);

    bool clean = true;
    if (Module32First(hSnapshot, &me32)) {
        do {
            std::string modName = me32.szModule;
            for (char& c : modName)
                c = tolower(c);

            // Known injection tool modules
            if (modName.find("minhook") != std::string::npos || modName.find("detours") != std::string::npos ||
                modName.find("frida") != std::string::npos || modName.find("cheatengine") != std::string::npos) {
                clean = false;
                break;
            }
        } while (Module32Next(hSnapshot, &me32));
    }

    CloseHandle(hSnapshot);
    return clean;
}

// ============================================================================
// MAIN VALIDATION FUNCTION
// Runs all validators and reports failures
// ============================================================================
inline ValidationResult ValidateAll() {
    ValidationResult result;
    result.passed = true;

    // Validator 1: Shared Memory
    if (!ValidateSharedMemory()) {
        result.passed = false;
        result.failedValidator = "SharedMemoryValidator";
        result.details = "Shared memory 'Local\\CobbleInjection' not found";
        AuditReporter::ReportLoaderValidationFailed(result.failedValidator, result.details);
        return result;
    }

    // Validator 2: Parent Process
    if (!ValidateParentProcess()) {
        std::string parent = SystemInfo::GetParentProcessName();
        result.passed = false;
        result.failedValidator = "ProcessChainValidator";
        result.details = "Suspicious parent process: " + parent;
        AuditReporter::ReportLoaderValidationFailed(result.failedValidator, result.details);
        return result;
    }

    // Validator 3: Injection Timing
    if (!ValidateInjectionTiming()) {
        result.passed = false;
        result.failedValidator = "TimestampValidator";
        result.details = "Injection timing suspicious (>5 min after process start)";
        AuditReporter::ReportLoaderValidationFailed(result.failedValidator, result.details);
        return result;
    }

    // Validator 4: No Debugger (warning only, don't block legitimate debugging)
    if (!ValidateNoDebugger()) {
        // Just report, don't fail - might be legitimate user debugging
        AuditReporter::ReportSuspiciousActivity("DebuggerValidator", "Debugger detected");
        // Continue validation, don't return
    }

    // Validator 5: No Suspicious Modules
    if (!ValidateNoSuspiciousModules()) {
        result.passed = false;
        result.failedValidator = "ModuleValidator";
        result.details = "Suspicious modules detected (injection tools)";
        AuditReporter::ReportLoaderValidationFailed(result.failedValidator, result.details);
        return result;
    }

    return result;
}

// Run validation on startup (non-blocking - failures are reported but don't crash)
// Returns true if should continue, false if should poison/exit
inline bool RunStartupValidation() {
    // Initialize system info first
    SystemInfo::Initialize();

    auto result = ValidateAll();

    if (!result.passed) {
        // Validation failed - return false to trigger JNI poison
        return false;
    }

    return true;
}

}  // namespace LoaderValidator
