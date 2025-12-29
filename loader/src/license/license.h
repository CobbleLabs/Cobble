#pragma once
#include <string>

namespace License {
    // Check if current HWID is licensed
    // Returns: true if licensed, false otherwise
    bool Verify(const std::string& serverUrl, const std::string& apiKey);
    
    // Get last error message
    std::string GetLastError();
}
