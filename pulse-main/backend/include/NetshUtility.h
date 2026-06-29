#pragma once
#include <string>

class NetshUtility {
public:
    static std::wstring QueryGlobalRscState();
    static bool SetRscState(bool enable);
};
