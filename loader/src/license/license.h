#pragma once
#include <string>

namespace License {
    // Core verification (HWID-based, for returning users)
    bool Verify(const std::string& serverUrl);
    
    // Login with credentials (first-time or new device)
    bool LoginWithCredentials(const std::string& serverUrl, 
                              const std::string& username, 
                              const std::string& password);
    
    // Check if last Verify failure was due to unregistered HWID
    bool RequiresLogin();
    
    // Legacy registration (deprecated)
    bool Register(const std::string& serverUrl, const std::string& username);
    
    // Fetch payload decryption key from server (requires valid session)
    bool FetchPayloadKey(const std::string& serverUrl);
    
    // Status getters
    std::string GetLastError();
    bool NeedsUpdate();
    std::string GetMinVersion();
    std::string GetUsername();
    int GetDaysRemaining();
    std::string GetSessionToken();
    std::string GetInjectionSecret();
    std::string GetPayloadKey();  // AES-256 key for DLL decryption
}
