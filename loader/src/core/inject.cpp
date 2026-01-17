#include "inject.h"
#include <TlHelp32.h>
#include <fstream>
#include <iostream>
#include <vector>
#include "../util/aes_string.hpp"

// =========================================================================
// LOADER SHELLCODE
// This function is injected into the target process.
// It acts as a custom "LoadLibrary" for our manually mapped DLL.
// =========================================================================
void __stdcall libraryLoader(MANUAL_MAPPING_DATA* pData) {
    if (!pData) {
        return;
    }

    // 1. Get Base Address of the mapped DLL
    BYTE* pBase = reinterpret_cast<BYTE*>(pData->hMod);

    // 2. Parse PE Headers
    // The DOS header is at the start of the file.
    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    // The NT headers (PE signature, ImageBase, etc.) are at e_lfanew offset.
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDos->e_lfanew);
    // Optional header contains execution entry point and directories like Imports/Relocs.
    auto* pOpt = &pNt->OptionalHeader;

    // -------------------------------------------------------------------------
    // 3. Process Base Relocations
    // DLLs prefer a specific base address (ImageBase). If we mapped it somewhere else,
    // we must adjust absolute addresses in the code (relocation).
    // -------------------------------------------------------------------------
    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        // Calculate the delta: (Actual Address) - (Preferred Address)
        auto delta = reinterpret_cast<ULONG_PTR>(pBase) - pOpt->ImageBase;

        // If there is a difference, we need to relocate
        if (delta != 0) {
            // Pointer to the first relocation block
            auto* pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);

            // Loop through all relocation blocks until we hit a null block
            while (pReloc->VirtualAddress) {
                // Block consists of a header + list of entries (WORDs)
                auto* pRelocInfo = reinterpret_cast<WORD*>(pReloc + 1);
                // Calculate number of entries in this block: (BlockSize - HeaderSize) / 2
                int numEntries = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;

                for (int i = 0; i < numEntries; i++) {
                    // 4 bits for Type, 12 bits for Offset
                    // Relocation Type: High 4 bits (e.g., IMAGE_REL_BASED_HIGHLOW)
                    int type = pRelocInfo[i] >> 12;
                    // Offset relative to the block's VirtualAddress: Low 12 bits
                    int offset = pRelocInfo[i] & 0xFFF;

                    // Apply relocation based on type
                    if (type == IMAGE_REL_BASED_DIR64) {
                        // 64-bit relocation: Add delta to the 64-bit value at the address
                        *reinterpret_cast<ULONG_PTR*>(pBase + pReloc->VirtualAddress + offset) += delta;
                    } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                        // 32-bit relocation: Add delta to the 32-bit value
                        *reinterpret_cast<DWORD*>(pBase + pReloc->VirtualAddress + offset) += (DWORD)delta;
                    }
                }
                // Move to next relocation block
                pReloc =
                    reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pReloc) + pReloc->SizeOfBlock);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Resolve Imports
    // The DLL depends on other system DLLs (kernel32.dll, user32.dll, etc.).
    // We must load them and get function pointers for the IAT (Import Address Table).
    // -------------------------------------------------------------------------
    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        // Pointer to the first import descriptor
        auto* pImportDescr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        while (pImportDescr->Name) {
            // Load the dependent DLL (e.g., "USER32.dll")
            char* szMod = reinterpret_cast<char*>(pBase + pImportDescr->Name);
            HINSTANCE hDll = pData->pLoadLibraryA(szMod);

            if (!hDll) {
                // Dependency failed to load. Skip this descriptor or abort?
                // Skipping might cause crash later when calling functions, but better than immediate crash here.
                ++pImportDescr;
                continue;
            }

            // Thunks point to the function names and where to store the addresses
            ULONG_PTR* pThunkRef =
                reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->OriginalFirstThunk);  // Name Array
            ULONG_PTR* pFuncRef =
                reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->FirstThunk);  // IAT (Address Array)

            // If OriginalFirstThunk is null, fallback to FirstThunk
            if (!pThunkRef)
                pThunkRef = pFuncRef;

            for (; *pThunkRef; ++pThunkRef, ++pFuncRef) {
                // Check if import is by Ordinal or Name
                if (IMAGE_SNAP_BY_ORDINAL(*pThunkRef)) {
                    // Import by Ordinal
                    *pFuncRef = pData->pGetProcAddress(hDll, reinterpret_cast<char*>(*pThunkRef & 0xFFFF));
                } else {
                    // Import by Name
                    auto* pImport = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + (*pThunkRef));
                    *pFuncRef = pData->pGetProcAddress(hDll, pImport->Name);
                }
            }
            ++pImportDescr;
        }
    }

    // -------------------------------------------------------------------------
    // 5. Execute DllMain
    // Now the DLL is fully loaded in memory, we call its entry point.
    // -------------------------------------------------------------------------
    if (pOpt->AddressOfEntryPoint) {
        auto EntryPoint = reinterpret_cast<f_DllMain>(pBase + pOpt->AddressOfEntryPoint);
        // Call DllMain(hModule, DLL_PROCESS_ATTACH, lpReserved)
        // We pass pData as lpReserved so DllMain can access it if needed
        EntryPoint((HINSTANCE)pBase, DLL_PROCESS_ATTACH, pData);
    }
}

// Stub to calculate shellcode size - compiler places this after libraryLoader
void __stdcall libraryLoaderEnd() {}

// =========================================================================
// HOST MANUAL MAP FUNCTION (Disk Wrapper)
// =========================================================================
bool manualMap(DWORD pid, const std::wstring& dllPath) {
    // 1. Read DLL file into local buffer
    std::ifstream file(dllPath, std::ios::binary | std::ios::ate);
    if (file.fail())
        return false;

    size_t fileSize = file.tellg();
    if (fileSize < 0x1000)
        return false;  // Too small to be valid PE header

    BYTE* pSrcData = new BYTE[fileSize];
    if (!pSrcData) {
        file.close();
        return false;
    }

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(pSrcData), fileSize);
    file.close();

    // Delegate to memory mapper
    bool result = manualMap(pid, pSrcData, fileSize);

    delete[] pSrcData;
    return result;
}

// =========================================================================
// HOST MANUAL MAP FUNCTION (Memory Core)
// Maps data from memory buffer into target process.
// =========================================================================
bool manualMap(DWORD pid, BYTE* pSrcData, size_t dataSize) {
    if (!pSrcData || dataSize < 0x1000)
        return false;

    // 2. Validate PE Headers locally
    auto* pDosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData);
    if (pDosHeader->e_magic != 0x5A4D) {  // "MZ"
        return false;
    }

    auto* pNtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(pSrcData + pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != 0x4550) {  // "PE"
        return false;
    }

    // 3. Open Target Process
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        return false;
    }

    // 4. Allocate Memory for the DLL in Target
    BYTE* pTargetBase =
        reinterpret_cast<BYTE*>(VirtualAllocEx(hProc,
                                               nullptr,  // Try allocating anywhere
                                               pNtHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE,
                                               PAGE_EXECUTE_READWRITE  // RWX needed for execution
                                               ));

    if (!pTargetBase) {
        CloseHandle(hProc);
        return false;
    }

    // 5. Copy Headers (DOS + NT)
    if (!WriteProcessMemory(hProc, pTargetBase, pSrcData, pNtHeaders->OptionalHeader.SizeOfHeaders, nullptr)) {
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // 6. Copy Sections
    auto* pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++) {
        if (pSectionHeader[i].SizeOfRawData) {
            // Copy section data from file offset to virtual address
            if (!WriteProcessMemory(hProc, pTargetBase + pSectionHeader[i].VirtualAddress,
                                    pSrcData + pSectionHeader[i].PointerToRawData, pSectionHeader[i].SizeOfRawData,
                                    nullptr)) {
                VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
                CloseHandle(hProc);
                return false;
            }
        }
    }

    // 6.1 Apply proper section protections
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++) {
        DWORD protection = 0;
        DWORD oldProtection = 0;
        DWORD size = pSectionHeader[i].Misc.VirtualSize;
        if (size == 0)
            size = pSectionHeader[i].SizeOfRawData;

        if (pSectionHeader[i].Characteristics & IMAGE_SCN_MEM_DISCARDABLE) {
            continue;
        }

        bool executable = (pSectionHeader[i].Characteristics & IMAGE_SCN_MEM_EXECUTE);
        bool readable = (pSectionHeader[i].Characteristics & IMAGE_SCN_MEM_READ);
        bool writeable = (pSectionHeader[i].Characteristics & IMAGE_SCN_MEM_WRITE);

        if (!executable && !readable && !writeable)
            protection = PAGE_NOACCESS;
        else if (!executable && !readable && writeable)
            protection = PAGE_WRITECOPY;
        else if (!executable && readable && !writeable)
            protection = PAGE_READONLY;
        else if (!executable && readable && writeable)
            protection = PAGE_READWRITE;
        else if (executable && !readable && !writeable)
            protection = PAGE_EXECUTE;
        else if (executable && !readable && writeable)
            protection = PAGE_EXECUTE_WRITECOPY;
        else if (executable && readable && !writeable)
            protection = PAGE_EXECUTE_READ;
        else if (executable && readable && writeable)
            protection = PAGE_EXECUTE_READWRITE;

        if (pSectionHeader[i].Characteristics & IMAGE_SCN_MEM_NOT_CACHED)
            protection |= PAGE_NOCACHE;

        VirtualProtectEx(hProc, pTargetBase + pSectionHeader[i].VirtualAddress, size, protection, &oldProtection);
    }

    // 7. Prepare Shellcode Data
    MANUAL_MAPPING_DATA data{0};
    data.pLoadLibraryA = reinterpret_cast<f_LoadLibraryA>(LoadLibraryA);
    data.pGetProcAddress = reinterpret_cast<f_GetProcAddress>(GetProcAddress);
    data.hMod = (HINSTANCE)pTargetBase;  // The base address we allocated

    // 8. Allocate Memory for Shellcode + Data
    size_t shellcodeSize = (DWORD64)libraryLoaderEnd - (DWORD64)libraryLoader;
    size_t totalAllocSize = shellcodeSize + sizeof(MANUAL_MAPPING_DATA);

    BYTE* pShellcodeMem = reinterpret_cast<BYTE*>(
        VirtualAllocEx(hProc, nullptr, totalAllocSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    if (!pShellcodeMem) {
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // 9. Write Shellcode and Data to Target
    WriteProcessMemory(hProc, pShellcodeMem + shellcodeSize, &data, sizeof(MANUAL_MAPPING_DATA), nullptr);
    WriteProcessMemory(hProc, pShellcodeMem, libraryLoader, shellcodeSize, nullptr);

    // 10. Execute Shellcode in Target
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pShellcodeMem),
                                        pShellcodeMem + shellcodeSize,  // Pass pointer to data
                                        0, nullptr);

    if (!hThread) {
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pShellcodeMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    // 11. Wait for completion
    WaitForSingleObject(hThread, 10000);

    // 12. Cleanup
    CloseHandle(hThread);
    VirtualFreeEx(hProc, pShellcodeMem, 0, MEM_RELEASE);
    CloseHandle(hProc);

    return true;
}

// =========================================================================
// LEGACY / UTILS
// =========================================================================

bool injectDllLoadLibrary(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess)
        return false;

    // Allocate memory in target for path
    size_t pathSize = (dllPath.length() + 1) * sizeof(wchar_t);
    void* pRemotePath = VirtualAllocEx(hProcess, nullptr, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemotePath) {
        CloseHandle(hProcess);
        return false;
    }

    // Write path to target
    if (!WriteProcessMemory(hProcess, pRemotePath, dllPath.c_str(), pathSize, nullptr)) {
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Get LoadLibraryW address
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC pLoadLibraryW = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!pLoadLibraryW) {
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Create remote thread
    HANDLE hThread =
        CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryW, pRemotePath, 0, nullptr);

    if (!hThread) {
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Wait for completion
    WaitForSingleObject(hThread, INFINITE);

    // Cleanup
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return true;
}

HMODULE getRemoteModuleHandle(DWORD pid, const std::wstring& moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return nullptr;

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

bool isDllInjected(DWORD pid, const std::wstring& dllName) {
    // Robust detection via Named Mutex
    // The client creates a global mutex on startup
    std::wstring mutexName = std::wstring(OBFUSCATE_WSTRING(L"Global\\Cobble_Instance_")) + std::to_wstring(pid);

    HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, mutexName.c_str());
    if (hMutex) {
        CloseHandle(hMutex);
        return true;
    }

    return false;
}

bool ejectDll(DWORD pid, const std::wstring& dllName) {
    HMODULE remoteModule = getRemoteModuleHandle(pid, dllName);
    if (!remoteModule)
        return false;

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION, FALSE, pid);

    if (!process)
        return false;

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE freeLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "FreeLibrary"));

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
