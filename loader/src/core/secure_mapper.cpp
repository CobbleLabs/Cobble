#include "secure_mapper.hpp"
#include <TlHelp32.h>
#include <string>
#include <vector>
#include "../util/aes_string.hpp"
#include "syscalls.hpp"

// ============================================================================
// SHELLCODE - Position Independent Code executed in target process
// Handles relocations, imports, TLS callbacks, and DllMain
// ============================================================================

#pragma code_seg(push, ".shell")
#pragma runtime_checks("", off)
#pragma optimize("", off)
#pragma strict_gs_check(push, off)

// Shellcode entry — called via CreateRemoteThread in the target process.
// __declspec(safebuffers) is REQUIRED: it suppresses /GS cookie emission so
// the function contains no RIP-relative refs to __security_cookie /
// __security_check_cookie in the loader's address space (those would AV when
// the shellcode is byte-copied into javaw.exe). Do not remove.
__declspec(safebuffers) void __stdcall ShellcodeEntry(SecureMapper::MAPPER_CONTEXT* ctx) {
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
    // 3. Register Exception Unwind Tables (.pdata)
    //
    // x64 Windows uses table-driven exception handling — RtlLookupFunctionEntry
    // walks ntdll's per-process function-table list to find the unwind info for
    // any RIP. A manually mapped image is invisible to that list, so any C++
    // throw or SEH unwind across a frame in this DLL fast-fails via
    // RaiseFailFastException, terminating the host process.
    //
    // Register the .pdata before TLS callbacks (which run CRT _initterm and
    // C++ thread_local constructors — both can throw).
    //
    // CAVEAT: ntdll keeps a pointer to the table forever; if the DLL is ever
    // unmapped, RtlDeleteFunctionTable must be called first or the next
    // exception in the host will fault on a freed VA. Cobble currently never
    // unmaps, so we don't track the table pointer for teardown.
    // -------------------------------------------------------------------------
    if (ctx->pRtlAddFunctionTable) {
        auto& excDir = pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (excDir.Size && excDir.VirtualAddress) {
            using RtlAddFunctionTable_t = BOOLEAN(__stdcall*)(PRUNTIME_FUNCTION, DWORD, DWORD64);
            auto pAdd = (RtlAddFunctionTable_t)ctx->pRtlAddFunctionTable;
            auto* pFunc = (PRUNTIME_FUNCTION)(pBase + excDir.VirtualAddress);
            DWORD count = excDir.Size / sizeof(RUNTIME_FUNCTION);
            pAdd(pFunc, count, (DWORD64)pBase);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Execute TLS Callbacks
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
    // 5. Call DllMain
    // -------------------------------------------------------------------------
    if (pOpt->AddressOfEntryPoint) {
        using DllMain_t = BOOL(__stdcall*)(HINSTANCE, DWORD, LPVOID);
        auto DllMain = (DllMain_t)(pBase + pOpt->AddressOfEntryPoint);
        DllMain((HINSTANCE)pBase, DLL_PROCESS_ATTACH, nullptr);
    }

    // -------------------------------------------------------------------------
    // 6. ANTI-DUMP: Capture section info, erase headers, apply correct perms.
    // -------------------------------------------------------------------------
    {
        // Get VirtualProtect from kernel32
        using VirtualProtect_t = BOOL(__stdcall*)(LPVOID, SIZE_T, DWORD, PDWORD);
        VirtualProtect_t pVirtualProtect = nullptr;

        for (DWORD i = 0; i < pK32Export->NumberOfNames; i++) {
            char* name = (char*)((BYTE*)ctx->kernel32Base + pNames[i]);
            if (name[0] == 'V' && name[7] == 'P' && name[14] == 't') {  // VirtualProtect
                pVirtualProtect = (VirtualProtect_t)((BYTE*)ctx->kernel32Base + pFunctions[pOrdinals[i]]);
                break;
            }
        }

        if (pVirtualProtect) {
            // --- Capture section info BEFORE wiping headers ---
            struct SectionInfo {
                DWORD rva;
                DWORD size;
                DWORD characteristics;
            };

            SectionInfo sections[16];
            WORD numSections = pNt->FileHeader.NumberOfSections;
            if (numSections > 16) numSections = 16;

            auto* pSec = IMAGE_FIRST_SECTION(pNt);
            for (WORD i = 0; i < numSections; i++) {
                sections[i].rva = pSec[i].VirtualAddress;
                sections[i].size = pSec[i].Misc.VirtualSize;
                sections[i].characteristics = pSec[i].Characteristics;
            }

            // --- Wipe PE headers ---
            DWORD oldProtect = 0;
            SIZE_T headerSize = pOpt->SizeOfHeaders;

            if (pVirtualProtect(pBase, headerSize, PAGE_READWRITE, &oldProtect)) {
                volatile BYTE* p = (volatile BYTE*)pBase;
                for (SIZE_T i = 0; i < headerSize; i++) {
                    p[i] = 0;
                }

                // Mark headers as PAGE_NOACCESS
                pVirtualProtect(pBase, headerSize, PAGE_NOACCESS, &oldProtect);
            }

            // --- Apply correct permissions per section ---
            for (WORD i = 0; i < numSections; i++) {
                DWORD chars = sections[i].characteristics;
                DWORD prot;

                if ((chars & IMAGE_SCN_MEM_EXECUTE) && (chars & IMAGE_SCN_MEM_READ)) {
                    prot = PAGE_EXECUTE_READ;
                } else if ((chars & IMAGE_SCN_MEM_READ) && (chars & IMAGE_SCN_MEM_WRITE)) {
                    prot = PAGE_READWRITE;
                } else if (chars & IMAGE_SCN_MEM_READ) {
                    prot = PAGE_READONLY;
                } else {
                    prot = PAGE_NOACCESS;
                }

                DWORD old = 0;
                pVirtualProtect(pBase + sections[i].rva, sections[i].size, prot, &old);
            }
        }
    }

    // Signal completion
    ctx->completionFlag = 0xC0BB1E;
}

__declspec(safebuffers) void __stdcall ShellcodeEnd() {}

#pragma strict_gs_check(pop)
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
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me;
        me.dwSize = sizeof(me);

        if (Module32FirstW(hSnap, &me)) {
            do {
                if (_wcsicmp(me.szModule, moduleName) == 0) {
                    CloseHandle(hSnap);
                    return (uint64_t)me.modBaseAddr;
                }
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }

    // System modules (ntdll, kernel32, etc.) are mapped at identical base addresses across all 64-bit processes
    return (uint64_t)GetModuleHandleW(moduleName);
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
    if (pDos->e_magic != 0x5A4D) {
        return false;
    }

    auto* pNt = (IMAGE_NT_HEADERS*)(pData + pDos->e_lfanew);
    if (pNt->Signature != 0x4550) {
        return false;
    }

    SIZE_T imageSize = pNt->OptionalHeader.SizeOfImage;
    SIZE_T contextSize = sizeof(MAPPER_CONTEXT);
    SIZE_T shellcodeSize = (SIZE_T)ShellcodeEnd - (SIZE_T)ShellcodeEntry;
    SIZE_T totalSize = imageSize + contextSize + shellcodeSize + 0x1000;  // Extra padding

    // Open target process with MINIMAL rights to avoid anti-cheat detection
    // PROCESS_ALL_ACCESS is a red flag for many security tools
    HANDLE hProcess = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return false;
    }

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

    // Resolve RtlAddFunctionTable from the loader's own ntdll. Because ntdll
    // is mapped at the same base address in every process on a given boot,
    // this pointer is valid in the target — no per-target resolution needed.
    // Without this the shellcode skips .pdata registration and any unwind
    // inside the mapped DLL fast-fails the host. See step 3 in ShellcodeEntry.
    ctx->pRtlAddFunctionTable = nullptr;
    if (HMODULE hLocalNtdll = GetModuleHandleW(L"ntdll.dll")) {
        ctx->pRtlAddFunctionTable = (void*)GetProcAddress(hLocalNtdll, "RtlAddFunctionTable");
    }
    ctx->pLdrGetProcedureAddress = nullptr;
    ctx->pRtlInitAnsiString = nullptr;
    ctx->pRtlInitUnicodeString = nullptr;

    if (!ctx->kernel32Base) {
        nt::g_Syscalls.NtUnmapViewOfSection(GetCurrentProcess(), pLocalBase);
        nt::g_Syscalls.NtUnmapViewOfSection(hProcess, pRemoteBase);
        nt::g_Syscalls.NtClose(hSection);
        CloseHandle(hProcess);
        return false;
    }

    // Copy shellcode
    memcpy(pShellcodeLocal, (void*)ShellcodeEntry, shellcodeSize);

    // =========================================================================
    // EXECUTE SHELLCODE — Thread Hijacking (primary, Guard-evasion)
    // Falls back to NtCreateThreadEx if no suitable thread found.
    //
    // Thread hijacking works by:
    //   1. Enumerate existing threads in the target process (javaw.exe)
    //   2. Pick a worker thread (skip index 0 = main/render thread)
    //   3. Suspend → save CONTEXT → overwrite RIP + RCX → resume
    //   4. Wait for ctx->completionFlag == 0xC0BB1E
    //   5. Suspend → restore original CONTEXT → resume
    //
    // This produces zero NtCreateThreadEx calls — the syscall Guard hooks.
    // =========================================================================
    bool success = false;

    // --- Attempt 1: Thread Hijack (primary, no new thread created) ---
    if (nt::g_Syscalls.NtSuspendThread && nt::g_Syscalls.NtResumeThread &&
        nt::g_Syscalls.NtGetContextThread && nt::g_Syscalls.NtSetContextThread) {

        // Collect thread IDs belonging to the target process
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te;
            te.dwSize = sizeof(te);

            std::vector<DWORD> candidates;
            if (Thread32First(hSnap, &te)) {
                do {
                    if (te.th32OwnerProcessID == pid)
                        candidates.push_back(te.th32ThreadID);
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);

            // Skip index 0 (most likely the Minecraft main/render thread).
            // Hijacking the main thread causes a freeze/crash. Worker threads
            // (Java GC, netIO, sound, etc.) are safe to momentarily redirect.
            size_t startIdx = (candidates.size() > 1) ? 1 : 0;

            for (size_t i = startIdx; i < candidates.size() && !success; i++) {
                DWORD tid = candidates[i];
                HANDLE hThread = nullptr;
                if (nt::g_Syscalls.NtOpenThread) {
                    nt::CLIENT_ID cid = { (void*)(uintptr_t)pid, (void*)(uintptr_t)tid };
                    nt::OBJECT_ATTRIBUTES oa = { sizeof(oa) };
                    nt::g_Syscalls.NtOpenThread(&hThread, nt::THREAD_HIJACK_ACCESS, &oa, &cid);
                }
                if (!hThread) {
                    hThread = OpenThread(nt::THREAD_HIJACK_ACCESS, FALSE, tid);
                }
                if (!hThread)
                    continue;

                // Suspend the chosen thread
                uint32_t prevSuspend = 0;
                if (nt::g_Syscalls.NtSuspendThread(hThread, &prevSuspend) != nt::STATUS_SUCCESS) {
                    CloseHandle(hThread);
                    continue;
                }

                // Save the original execution context (full register state)
                CONTEXT origCtx = {};
                origCtx.ContextFlags = CONTEXT_FULL;
                if (nt::g_Syscalls.NtGetContextThread(hThread, &origCtx) != nt::STATUS_SUCCESS) {
                    nt::g_Syscalls.NtResumeThread(hThread, &prevSuspend);
                    CloseHandle(hThread);
                    continue;
                }

                // Build hijack context:
                //   RIP → shellcode entry point in the remote shared section
                //   RCX → MAPPER_CONTEXT pointer (first argument per x64 ABI)
                //   RSP → nudged down & aligned to 16 bytes (maintains call ABI)
                CONTEXT hijackCtx = origCtx;
                hijackCtx.Rip = (DWORD64)pShellcodeRemote;
                hijackCtx.Rcx = (DWORD64)pContextRemote;
                // (RSP - 8) & ~0xF gives 16-byte alignment; subtract 8 more for
                // the implicit "return address" slot that __stdcall expects.
                hijackCtx.Rsp = (origCtx.Rsp - 8) & ~(DWORD64)0xF;

                if (nt::g_Syscalls.NtSetContextThread(hThread, &hijackCtx) != nt::STATUS_SUCCESS) {
                    nt::g_Syscalls.NtResumeThread(hThread, &prevSuspend);
                    CloseHandle(hThread);
                    continue;
                }

                // Let the thread run — it now executes our shellcode
                nt::g_Syscalls.NtResumeThread(hThread, &prevSuspend);

                // Poll completion flag written by ShellcodeEntry (max 10 s)
                DWORD waited = 0;
                while (ctx->completionFlag == 0 && waited < 10000) {
                    Sleep(10);
                    waited += 10;
                }

                success = (ctx->completionFlag == 0xC0BB1E);

                // Restore the original thread context so the game continues
                // normally (Java GC / netIO threads keep running as before).
                if (success) {
                    nt::g_Syscalls.NtSuspendThread(hThread, &prevSuspend);
                    nt::g_Syscalls.NtSetContextThread(hThread, &origCtx);
                    nt::g_Syscalls.NtResumeThread(hThread, &prevSuspend);
                }

                CloseHandle(hThread);
            }
        }
    }

    // --- Attempt 2: NtCreateThreadEx fallback (Guard may detect this) ---
    if (!success) {
        void* hThread = nullptr;
        status = nt::g_Syscalls.NtCreateThreadEx(&hThread, nt::NT_THREAD_ALL_ACCESS, nullptr, hProcess,
                                                 pShellcodeRemote, pContextRemote,
                                                 0,  // No flags
                                                 0, 0, 0, nullptr);

        if (status == nt::STATUS_SUCCESS && hThread) {
            DWORD timeout = 0;
            while (ctx->completionFlag == 0 && timeout < 10000) {
                Sleep(10);
                timeout += 10;
            }
            success = (ctx->completionFlag == 0xC0BB1E);
            nt::g_Syscalls.NtClose(hThread);
        }
    }

    // Cleanup — unmap local view (remote section stays mapped: DLL keeps running)
    nt::g_Syscalls.NtUnmapViewOfSection(GetCurrentProcess(), pLocalBase);
    nt::g_Syscalls.NtClose(hSection);
    CloseHandle(hProcess);

    return success;
}

bool SecureMapper::IsAlreadyInjected(DWORD pid) {
    std::wstring mutexName = std::wstring(OBFUSCATE_WSTRING(L"Global\\Cobble_Instance_")) + std::to_wstring(pid);
    HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, mutexName.c_str());
    if (hMutex) {
        CloseHandle(hMutex);
        return true;
    }
    return false;
}
