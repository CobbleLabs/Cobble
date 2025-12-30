#pragma once
#include <string>

namespace License {
    // Verify license with server
    bool Verify(const std::string& serverUrl);
    
    // Register new user (returns true if successful)
    bool Register(const std::string& serverUrl, const std::string& username);
    
    // Get last error message
    std::string GetLastError();
    
    // Check if update is required
    bool NeedsUpdate();
    
    // Get minimum required version
    std::string GetMinVersion();
    
    // Get verified username (only valid after successful Verify)
    std::string GetUsername();
    
    // Get days remaining (only valid after successful Verify)
    int GetDaysRemaining();
}
