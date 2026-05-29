#pragma once

#include <Windows.h>
#include "cobble.hpp"
#include "util/inject_trace.hpp"

DWORD WINAPI MainThread(LPVOID lpParam) {
    COBBLE_TRACE("MainThread:enter");
    // Top-level safety net: if anything inside initialize() throws (malformed
    // config, std::bad_alloc on a module ctor, etc.) catch it here so the
    // worker thread dies cleanly instead of escaping into the host's
    // unhandled-exception filter and tearing down javaw.exe.
    try {
        cobble::context::initialize(static_cast<HMODULE>(lpParam));
    } catch (const std::exception& e) {
        COBBLE_TRACE("MainThread:std::exception caught");
        COBBLE_TRACE(e.what());
    } catch (...) {
        COBBLE_TRACE("MainThread:unknown exception caught");
    }
    COBBLE_TRACE("MainThread:exit");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        COBBLE_TRACE_RESET();
        COBBLE_TRACE("DllMain:DLL_PROCESS_ATTACH");
        bool isManualMap = (lpReserved != nullptr);
        COBBLE_TRACE(isManualMap ? "DllMain:isManualMap=true" : "DllMain:isManualMap=false");

        if (!isManualMap) {
            DisableThreadLibraryCalls(hModule);
        }

        HANDLE hThread = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        COBBLE_TRACE(hThread ? "DllMain:CreateThread=ok" : "DllMain:CreateThread=fail");
        if (hThread)
            CloseHandle(hThread);
        COBBLE_TRACE("DllMain:return TRUE");
    }

    return TRUE;
}
