#include "secure_mapper.hpp"
#include <TlHelp32.h>
#include <string>
#include <vector>
#include "../security/xor.h"
#include "syscalls.hpp"

// ============================================================================
// SHELLCODE - Position Independent Code executed in target process
// Handles relocations, imports, TLS callbacks, and DllMain
// ============================================================================

#pragma code_seg(push, ".shell")
#pragma runtime_checks("", off)
#pragma optimize("", off)

// Shellcode entry point - called via CreateRemoteThread
void __stdcall ShellcodeEntry(SecureMapper::MAPPER_CONTEXT* ctx) {
    if (!ctx || !ctx->moduleBase) {
        ctx->lastError = 1;
        ctx->completionFlag = 0xDEAD;
        return;
    }

    BYTE* pBase = (BYTE*)ctx->moduleBase;

    // Parse PE headers
    auto* pDos = (IMAGE_DOS_HEADER*)pBase;
    if (pDos->e_magic != 0x5A4D) {
        ctx->lastError = 2;
        ctx->completionFlag = 0xDEAD;
        return;
    }

    auto* pNt = (IMAGE_NT_HEADERS*)(pBase + pDos->e_lfanew);
    if (pNt->Signature != 0x4550) {
        ctx->lastError = 3;
        ctx->completionFlag = 0xDEAD;
        return;
    }

    auto* pOpt = &pNt->OptionalHeader;

    // -------------------------------------------------------------------------
    // 1. Process Base Relocations
    // -------------------------------------------------------------------------
    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        auto delta = (ULONG_PTR)pBase - pOpt->ImageBase;

        if (delta != 0) {
            auto* pReloc =
                (IMAGE_BASE_RELOCATION*)(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);

            while (pReloc->VirtualAddress) {
                WORD* pRelocInfo = (WORD*)(pReloc + 1);
                int numEntries = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);

                for (int i = 0; i < numEntries; i++) {
                    int type = pRelocInfo[i] >> 12;
                    int offset = pRelocInfo[i] & 0xFFF;

                    if (type == IMAGE_REL_BASED_DIR64) {
                        *(ULONG_PTR*)(pBase + pReloc->VirtualAddress + offset) += delta;
                    } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                        *(DWORD*)(pBase + pReloc->VirtualAddress + offset) += (DWORD)delta;
                    }
                }
                pReloc = (IMAGE_BASE_RELOCATION*)((BYTE*)pReloc + pReloc->SizeOfBlock);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2. Resolve Imports using kernel32 from context
    // -------------------------------------------------------------------------
    using LoadLibraryA_t = HMODULE(__stdcall*)(const char*);
    using GetProcAddress_t = FARPROC(__stdcall*)(HMODULE, const char*);

    // Get function pointers from kernel32 base
    auto* pK32Dos = (IMAGE_DOS_HEADER*)ctx->kernel32Base;
    auto* pK32Nt = (IMAGE_NT_HEADERS*)((BYTE*)ctx->kernel32Base + pK32Dos->e_lfanew);
    auto* pK32Export =
        (IMAGE_EXPORT_DIRECTORY*)((BYTE*)ctx->kernel32Base +
                                  pK32Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

    DWORD* pNames = (DWORD*)((BYTE*)ctx->kernel32Base + pK32Export->AddressOfNames);
    WORD* pOrdinals = (WORD*)((BYTE*)ctx->kernel32Base + pK32Export->AddressOfNameOrdinals);
    DWORD* pFunctions = (DWORD*)((BYTE*)ctx->kernel32Base + pK32Export->AddressOfFunctions);

    LoadLibraryA_t pLoadLibraryA = nullptr;
    GetProcAddress_t pGetProcAddress = nullptr;

    // Find LoadLibraryA and GetProcAddress by name hash
    for (DWORD i = 0; i < pK32Export->NumberOfNames; i++) {
        char* name = (char*)((BYTE*)ctx->kernel32Base + pNames[i]);

        // Simple name comparison (shellcode can't use strcmp)
        if (name[0] == 'L' && name[4] == 'L' && name[11] == 'A') {
            pLoadLibraryA = (LoadLibraryA_t)((BYTE*)ctx->kernel32Base + pFunctions[pOrdinals[i]]);
        }
        if (name[0] == 'G' && name[3] == 'P' && name[7] == 'A') {
            pGetProcAddress = (GetProcAddress_t)((BYTE*)ctx->kernel32Base + pFunctions[pOrdinals[i]]);
        }
    }

    if (!pLoadLibraryA || !pGetProcAddress) {
        ctx->lastError = 4;
        ctx->completionFlag = 0xDEAD;
        return;
    }

    // Process imports
    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto* pImport =
            (IMAGE_IMPORT_DESCRIPTOR*)(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        while (pImport->Name) {
            char* szMod = (char*)(pBase + pImport->Name);
            HMODULE hDll = pLoadLibraryA(szMod);

            if (!hDll) {
                ++pImport;
                continue;
            }

            ULONG_PTR* pThunk = (ULONG_PTR*)(pBase + pImport->OriginalFirstThunk);
            ULONG_PTR* pFunc = (ULONG_PTR*)(pBase + pImport->FirstThunk);

            if (!pThunk)
                pThunk = pFunc;

            for (; *pThunk; ++pThunk, ++pFunc) {
                if (IMAGE_SNAP_BY_ORDINAL(*pThunk)) {
                    *pFunc = (ULONG_PTR)pGetProcAddress(hDll, (char*)(*pThunk & 0xFFFF));
                } else {
                    auto* pImportName = (IMAGE_IMPORT_BY_NAME*)(pBase + *pThunk);
                    *pFunc = (ULONG_PTR)pGetProcAddress(hDll, pImportName->Name);
                }
            }
            ++pImport;
        }
    }

    // -------------------------------------------------------------------------
    // 3. Execute TLS Callbacks
    // -------------------------------------------------------------------------
    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
        auto* pTls = (IMAGE_TLS_DIRECTORY*)(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);

        if (pTls->AddressOfCallBacks) {
            PIMAGE_TLS_CALLBACK* pCallbacks = (PIMAGE_TLS_CALLBACK*)pTls->AddressOfCallBacks;

            for (; *pCallbacks; ++pCallbacks) {
                (*pCallbacks)((PVOID)pBase, DLL_PROCESS_ATTACH, nullptr);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Call DllMain
    // -------------------------------------------------------------------------
    if (pOpt->AddressOfEntryPoint) {
        using DllMain_t = BOOL(__stdcall*)(HINSTANCE, DWORD, LPVOID);
        auto DllMain = (DllMain_t)(pBase + pOpt->AddressOfEntryPoint);
        DllMain((HINSTANCE)pBase, DLL_PROCESS_ATTACH, nullptr);
    }

    // Signal completion
    ctx->completionFlag = 0xC0BB1E;
}

void __stdcall ShellcodeEnd() {}

#pragma optimize("", on)
#pragma runtime_checks("", restore)
#pragma code_seg(pop)

// ============================================================================
// HOST FUNCTIONS
// ============================================================================

namespace {

// Get module base address in remote process
uint64_t GetRemoteModuleBase(HANDLE hProcess, const wchar_t* moduleName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetProcessId(hProcess));
    if (hSnap == INVALID_HANDLE_VALUE)
        return 0;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

    uint64_t result = 0;

    if (Module32FirstW(hSnap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0) {
                result = (uint64_t)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(hSnap, &me));
    }

    CloseHandle(hSnap);
    return result;
}

}  // anonymous namespace

bool SecureMapper::SecureManualMap(DWORD pid, BYTE* pData, size_t dataSize) {
    if (!pData || dataSize < 0x1000)
        return false;

    // Initialize syscalls
    if (!nt::g_Syscalls.Initialize()) {
        return false;
    }

    // Validate PE
    auto* pDos = (IMAGE_DOS_HEADER*)pData;
    if (pDos->e_magic != 0x5A4D)
        return false;

    auto* pNt = (IMAGE_NT_HEADERS*)(pData + pDos->e_lfanew);
    if (pNt->Signature != 0x4550)
        return false;

    SIZE_T imageSize = pNt->OptionalHeader.SizeOfImage;
    SIZE_T contextSize = sizeof(MAPPER_CONTEXT);
    SIZE_T shellcodeSize = (SIZE_T)ShellcodeEnd - (SIZE_T)ShellcodeEntry;
    SIZE_T totalSize = imageSize + contextSize + shellcodeSize + 0x1000;  // Extra padding

    // Open target process
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess)
        return false;

    // -------------------------------------------------------------------------
    // Create Shared Memory Section
    // -------------------------------------------------------------------------
    void* hSection = nullptr;
    LARGE_INTEGER sectionSize;
    sectionSize.QuadPart = totalSize;

    int32_t status = nt::g_Syscalls.NtCreateSection(&hSection, nt::NT_SECTION_ALL_ACCESS, nullptr, &sectionSize,
                                                    PAGE_EXECUTE_READWRITE, nt::NT_SEC_COMMIT, nullptr);

    if (status != nt::STATUS_SUCCESS || !hSection) {
        CloseHandle(hProcess);
        return false;
    }

    // -------------------------------------------------------------------------
    // Map Section to Local Process (for writing)
    // -------------------------------------------------------------------------
    void* pLocalBase = nullptr;
    SIZE_T viewSize = 0;

    status = nt::g_Syscalls.NtMapViewOfSection(hSection, GetCurrentProcess(), &pLocalBase, 0, 0, nullptr, &viewSize,
                                               2,  // ViewUnmap
                                               0, PAGE_READWRITE);

    if (status != nt::STATUS_SUCCESS || !pLocalBase) {
        nt::g_Syscalls.NtClose(hSection);
        CloseHandle(hProcess);
        return false;
    }

    // -------------------------------------------------------------------------
    // Map Section to Target Process (same physical memory!)
    // -------------------------------------------------------------------------
    void* pRemoteBase = nullptr;
    SIZE_T remoteViewSize = 0;

    status = nt::g_Syscalls.NtMapViewOfSection(hSection, hProcess, &pRemoteBase, 0, 0, nullptr, &remoteViewSize,
                                               2,  // ViewUnmap
                                               0, PAGE_EXECUTE_READWRITE);

    if (status != nt::STATUS_SUCCESS || !pRemoteBase) {
        nt::g_Syscalls.NtUnmapViewOfSection(GetCurrentProcess(), pLocalBase);
        nt::g_Syscalls.NtClose(hSection);
        CloseHandle(hProcess);
        return false;
    }

    // -------------------------------------------------------------------------
    // Write PE to Shared Memory (locally - appears in remote!)
    // -------------------------------------------------------------------------
    BYTE* pLocal = (BYTE*)pLocalBase;
    BYTE* pRemote = (BYTE*)pRemoteBase;

    // Copy PE headers
    memcpy(pLocal, pData, pNt->OptionalHeader.SizeOfHeaders);

    // Copy sections
    auto* pSection = IMAGE_FIRST_SECTION(pNt);
    for (WORD i = 0; i < pNt->FileHeader.NumberOfSections; i++) {
        if (pSection[i].SizeOfRawData) {
            memcpy(pLocal + pSection[i].VirtualAddress, pData + pSection[i].PointerToRawData,
                   pSection[i].SizeOfRawData);
        }
    }

    // -------------------------------------------------------------------------
    // Setup Context and Shellcode
    // -------------------------------------------------------------------------
    BYTE* pContextLocal = pLocal + imageSize;
    BYTE* pShellcodeLocal = pContextLocal + contextSize;

    BYTE* pContextRemote = pRemote + imageSize;
    BYTE* pShellcodeRemote = pContextRemote + contextSize;

    // Initialize context
    MAPPER_CONTEXT* ctx = (MAPPER_CONTEXT*)pContextLocal;
    ctx->moduleBase = (uint64_t)pRemote;
    ctx->ntdllBase = GetRemoteModuleBase(hProcess, L"ntdll.dll");
    ctx->kernel32Base = GetRemoteModuleBase(hProcess, L"kernel32.dll");
    ctx->completionFlag = 0;
    ctx->lastError = 0;

    if (!ctx->kernel32Base) {
        nt::g_Syscalls.NtUnmapViewOfSection(GetCurrentProcess(), pLocalBase);
        nt::g_Syscalls.NtUnmapViewOfSection(hProcess, pRemoteBase);
        nt::g_Syscalls.NtClose(hSection);
        CloseHandle(hProcess);
        return false;
    }

    // Copy shellcode
    memcpy(pShellcodeLocal, (void*)ShellcodeEntry, shellcodeSize);

    // -------------------------------------------------------------------------
    // Execute Shellcode in Target via NtCreateThreadEx
    // -------------------------------------------------------------------------
    void* hThread = nullptr;

    status = nt::g_Syscalls.NtCreateThreadEx(&hThread, nt::NT_THREAD_ALL_ACCESS, nullptr, hProcess, pShellcodeRemote,
                                             pContextRemote,
                                             0,  // No flags
                                             0, 0, 0, nullptr);

    if (status != nt::STATUS_SUCCESS || !hThread) {
        nt::g_Syscalls.NtUnmapViewOfSection(GetCurrentProcess(), pLocalBase);
        nt::g_Syscalls.NtUnmapViewOfSection(hProcess, pRemoteBase);
        nt::g_Syscalls.NtClose(hSection);
        CloseHandle(hProcess);
        return false;
    }

    // Wait for completion (check shared memory flag)
    DWORD timeout = 0;
    while (ctx->completionFlag == 0 && timeout < 10000) {
        Sleep(10);
        timeout += 10;
    }

    bool success = (ctx->completionFlag == 0xC0BB1E);

    // Cleanup - unmap local view (remote stays mapped for DLL to run)
    nt::g_Syscalls.NtClose(hThread);
    nt::g_Syscalls.NtUnmapViewOfSection(GetCurrentProcess(), pLocalBase);
    nt::g_Syscalls.NtClose(hSection);
    CloseHandle(hProcess);

    return success;
}

bool SecureMapper::IsAlreadyInjected(DWORD pid) {
    std::wstring mutexName = std::wstring(_xorw_(L"Global\\Cobble_Instance_")) + std::to_wstring(pid);
    HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, mutexName.c_str());
    if (hMutex) {
        CloseHandle(hMutex);
        return true;
    }
    return false;
}
