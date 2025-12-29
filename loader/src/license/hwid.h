#pragma once
#include <string>

namespace HWID {
    // Get unique hardware ID (SHA256 hash of CPU + Disk + MAC)
    std::string Generate();
    
    // Individual component getters (for debugging)
    std::string GetCPUID();
    std::string GetDiskSerial();
    std::string GetMACAddress();
}
