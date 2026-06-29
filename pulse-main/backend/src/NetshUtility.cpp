#include "NetshUtility.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

static std::wstring RunProcessWithOutput(const std::wstring& exe, const std::wstring& args) {
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return L"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = exe + L" " + args;
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return L"";
    }
    CloseHandle(hWritePipe);

    std::string output;
    char chunk[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, chunk, sizeof(chunk) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        chunk[bytesRead] = '\0';
        output += chunk;
    }
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    // Convert to wstring
    int wlen = MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, nullptr, 0);
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, output.c_str(), -1, result.data(), wlen);
    return result;
}

std::wstring NetshUtility::QueryGlobalRscState() {
    std::wstring output = RunProcessWithOutput(L"netsh.exe", L"interface tcp show global");

    // Split lines
    std::wistringstream ss(output);
    std::wstring line;
    while (std::getline(ss, line)) {
        std::wstring lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

        bool hasRsc = lower.find(L"fusi") != std::wstring::npos ||
                      lower.find(L"coalesc") != std::wstring::npos ||
                      (lower.find(L"segment") != std::wstring::npos &&
                       (lower.find(L"recep") != std::wstring::npos || lower.find(L"recei") != std::wstring::npos));
        if (hasRsc) {
            if (lower.find(L"disabled") != std::wstring::npos || lower.find(L"deshabilitado") != std::wstring::npos)
                return L"disabled";
            if (lower.find(L"enabled") != std::wstring::npos || lower.find(L"habilitado") != std::wstring::npos)
                return L"enabled";
        }
    }
    return L"enabled";
}

bool NetshUtility::SetRscState(bool enable) {
    std::wstring state = enable ? L"enabled" : L"disabled";
    std::wstring args = L"interface tcp set global rsc=" + state;

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"netsh.exe " + args;
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;

    WaitForSingleObject(pi.hProcess, 3000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}
