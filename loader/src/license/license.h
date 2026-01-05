#pragma once
#include <string>

namespace License {
    bool Verify(const std::string& serverUrl);
    bool Register(const std::string& serverUrl, const std::string& username);
    std::string GetLastError();
    bool NeedsUpdate();
    std::string GetMinVersion();
    std::string GetUsername();
    int GetDaysRemaining();
    std::string GetSessionToken();
}
